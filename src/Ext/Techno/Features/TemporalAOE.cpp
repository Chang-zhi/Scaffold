#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <BuildingClass.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>
#include <HouseClass.h>
#include <RulesClass.h>
#include <CellClass.h>
#include <MapClass.h>
#include <set>
#include <cmath>

#include <TemporalClass.h>
#include <Ext/Techno/Body.h>
#include <Ext/WarheadType/Body.h>
#include <Utilities/Debug.h>

// ── 副目标假 Temporal 映射 ──────────────────────────────────────
// 每个副目标对应一个从游戏 Array/链表拆除的 TemporalClass 实例，
// 驱动 TemporalTargetingMe + BeingWarpedOut，游戏不更新它。
std::unordered_map<TechnoClass* /*副目标*/, TechnoExt::TemporalAOE::FakeTemporalEntry> TechnoExt::TemporalAOE::FakeTemporals;

// 副目标独占锁（副目标 → 攻击者）
std::unordered_map<TechnoClass* /*目标*/, TechnoClass* /*攻击者*/> TechnoExt::TemporalAOE::SecondaryClaims;

// 抹除中锁定集合（防止同一目标被多个攻击者同时抹除）
std::unordered_set<TechnoClass* /*正在被抹除的目标*/> TechnoExt::TemporalAOE::WarpingOutTargets;

// 主目标 → 攻击者映射（用于 OpenTopped 乘员目标丢失时恢复）
std::unordered_map<TechnoClass* /*主目标*/, TechnoClass* /*攻击者*/> TechnoExt::TemporalAOE::CachedMainOwners;

// 攻击者 → 副目标集合（逆向映射，O(1) 批量销毁用）
std::unordered_map<TechnoClass* /*攻击者*/, std::unordered_set<TechnoClass* /*副目标*/>> TechnoExt::TemporalAOE::SecondariesByAttacker;

// 读档后第一帧深度清理标记
bool TechnoExt::TemporalAOE::s_PostLoadCleanupNeeded = false;

// ============================================================
// RegisterDestruction 钩子：精确检测主目标被谁击杀
// 被 AOE 武器抹除 → 标记 CachedMainDead，状态机抹除副目标
// 被第三方击杀 → 立即释放副目标（不解冻，仅解冻）
// ============================================================
DEFINE_HOOK(0x702E4E, TechnoClass_RegisterDestruction_TemporalAOE, 0x6)
{
	GET(TechnoClass*, pVictim, ECX);
	GET(TechnoClass*, pKiller, EDI);

	auto it = TechnoExt::TemporalAOE::CachedMainOwners.find(pVictim);
	if (it != TechnoExt::TemporalAOE::CachedMainOwners.end())
	{
		auto pOwner = it->second;
		TechnoExt::TemporalAOE::CachedMainOwners.erase(it);

		if (pKiller == pOwner)
		{
			// 被 AOE 武器自身抹除 → 标记，让状态机抹除副目标
			if (auto pExt = TechnoExt::ExtMap.Find(pOwner))
			{
				pExt->AOEState.CachedMainDead = true;
			}
		}
		else
		{
			// 被第三方击杀 → 释放所有副目标（不解冻）

			if (auto pExt = TechnoExt::ExtMap.Find(pOwner))
			{
				TechnoExt::TemporalAOE::DestroyFakeTemporalsByAttacker(pOwner);
				TechnoExt::TemporalAOE::ClearBuildingsDisabled(pExt->AOEState.BuildingsDisabled);
				TechnoExt::TemporalAOE::ReleaseAttackerLocks(pOwner);
				pExt->AOEState.TargetsInRange.clear();
				pExt->AOEState.BuildingsDisabled.clear();
				pExt->AOEState.ExtraWarpAdded = 0;
				pExt->AOEState.WarpTimer = 0;
				pExt->AOEState.ContributedTargets.clear();
				pExt->AOEState.CachedMain = nullptr;
				pExt->AOEState.CachedMainDead = false;
				pExt->AOEState.Active = false;
			}
		}
	}

	return 0;
}

// 清理某个攻击者的所有副目标独占锁
void TechnoExt::TemporalAOE::ReleaseAttackerLocks(TechnoClass* pAttacker)
{
	if (!pAttacker) return;
	for (auto it = SecondaryClaims.begin(); it != SecondaryClaims.end(); )
	{
		if (it->second == pAttacker)
			it = SecondaryClaims.erase(it);
		else
			++it;
	}
}

// 清理全局副目标锁中涉及指定指针的所有记录（TechnoClass 销毁时调用）
void TechnoExt::TemporalAOE::InvalidatePtr(void* ptr)
{
	if (!ptr) return;
	auto pTechPtr = static_cast<TechnoClass*>(ptr);

	// ptr 是攻击者 → 直接从 SecondariesByAttacker 取所有副目标批量销毁
	{
		auto setIt = SecondariesByAttacker.find(pTechPtr);
		if (setIt != SecondariesByAttacker.end())
		{
			auto targets = setIt->second;
			SecondariesByAttacker.erase(setIt);
			for (auto pTarget : targets)
				DestroyFakeTemporal(pTarget);
		}
	}

	// ptr 是副目标 → 遍历 FakeTemporals 查找并移除
	{
		// 先收集再清理，防迭代失效
		std::vector<TechnoClass*> toRemove;
		for (auto& ft : FakeTemporals)
		{
			if (ft.first == ptr)
				toRemove.push_back(ft.first);
		}
		for (auto pTarget : toRemove)
			DestroyFakeTemporal(pTarget);
	}
	// SecondaryClaims (副目标->攻击者)
	for (auto it = SecondaryClaims.begin(); it != SecondaryClaims.end(); )
	{
		if (it->first == ptr || it->second == ptr)
			it = SecondaryClaims.erase(it);
		else
			++it;
	}
	// WarpingOutTargets (正在被抹除的目标的集合)
	for (auto it = WarpingOutTargets.begin(); it != WarpingOutTargets.end(); )
	{
		if (*it == ptr)
			it = WarpingOutTargets.erase(it);
		else
			++it;
	}
	DisabledByUs.erase(pTechPtr);
}

// 全局检测所有副目标独占锁的合法性，释放无效记录并解冻对应单位
// 读档后第一帧深度清理（此时引擎指针修复已完成）
void TechnoExt::TemporalAOE::PostLoadCleanup()
{

	// 1. 扫描 TemporalClass::Array 中残存的假 Temporal 实例
	//    此时指针已修复，可安全访问 pTemp->Target
	SecondariesByAttacker.clear();
	{
		std::vector<TemporalClass*> fakes;
		for (int i = 0; i < TemporalClass::Array.Count; ++i)
		{
			auto pTemp = TemporalClass::Array.Items[i];
			if (!pTemp) continue;
			if (pTemp->WarpPerStep == 0 && pTemp->WarpRemaining == 0x7FFFFFFF)
				fakes.push_back(pTemp);
		}
		for (auto pTemp : fakes)
		{
			if (pTemp->Target)
			{
				pTemp->Target->TemporalTargetingMe = nullptr;
				pTemp->Target->BeingWarpedOut = false;
			}
			pTemp->Owner = nullptr;
			pTemp->Target = nullptr;
			GameDelete(pTemp);
		}
	}

	// 2. 扫描 TechnoClass::Array 清理孤儿 BeingWarpedOut
	for (int i = 0; i < TechnoClass::Array.Count; ++i)
	{
		auto pTech = TechnoClass::Array.Items[i];
		if (!pTech) continue;
		if (pTech->BeingWarpedOut && !pTech->TemporalTargetingMe)
		{
			pTech->BeingWarpedOut = false;
		}
	}

	// 3. 恢复被禁用的建筑 + 刷新所属方感知
	for (int i = 0; i < TechnoClass::Array.Count; ++i)
	{
		auto pTech = TechnoClass::Array.Items[i];
		if (!pTech) continue;
		if (auto pBld = abstract_cast<BuildingClass*>(pTech))
		{
			pBld->EnableTemporal();
			if (pBld->Owner)
			{
				pBld->Owner->RecheckPower = true;
				pBld->Owner->RecheckRadar = true;
			}
		}
	}
}

// 每帧由全局 hook 调用，不依赖具体攻击者的 AI 是否运行
// 兜底：全局检测未被攻击但仍处于冻结状态的残存单位
void TechnoExt::TemporalAOE::ValidateGlobals()
{
	// 读档后第一帧深度清理（引擎指针修复完成后）
	if (s_PostLoadCleanupNeeded)
	{
		s_PostLoadCleanupNeeded = false;
		PostLoadCleanup();
	}

	// 递归防护：防止级联回调，每帧重置
	static int s_RecursionGuard = 0;
	static int s_lastRecFrame = 0;
	if (Unsorted::CurrentFrame != s_lastRecFrame)
	{
		s_lastRecFrame = Unsorted::CurrentFrame;
		s_RecursionGuard = 0;
	}
	struct RecursionCounter { ~RecursionCounter() { --s_RecursionGuard; } };
	if (++s_RecursionGuard > 10) return;
	RecursionCounter guard;

	// 清理上一帧积累的 WarpingOutTargets（墓碑条目）
	// 这些条目在本帧内的 WarpOutTarget 中会重新按需插入，旧条目安全清空
	WarpingOutTargets.clear();

	// 开始清理 SecondaryClaims(副目标->攻击者) 中无效的记录
	for (auto it = SecondaryClaims.begin(); it != SecondaryClaims.end(); )
	{
		bool invalid = false;
		TechnoClass* pTarget = it->first;
		TechnoClass* pAttacker = it->second;

		// 目标无效
		if (!pTarget || pTarget->InLimbo)
			invalid = true;
		// 攻击者无效（OpenTopped 乘员虽然 InLimbo 但仍然活跃）
		else if (!pAttacker || pAttacker->Health <= 0
			|| (pAttacker->InLimbo
				&& !(pAttacker->Transporter && pAttacker->Transporter->GetTechnoType()
					&& pAttacker->Transporter->GetTechnoType()->OpenTopped)))
			invalid = true;
		// 攻击者被冻住
		else if (pAttacker->BeingWarpedOut)
			invalid = true;
		// 攻击者的 AOE 状态已失效（不再活跃）
		else if (auto pExt = TechnoExt::ExtMap.Find(pAttacker))
		{
			if (!pExt->AOEState.Active)
				invalid = true;
			// 攻击者的时间束目标已死或不存在
			// OpenTopped 乘员 TemporalImUsing->Target 可能临时丢失，检查 CachedMain 兜底
			else if (!pAttacker->TemporalImUsing || !pAttacker->TemporalImUsing->Target
				|| pAttacker->TemporalImUsing->Target->Health <= 0)
			{
				// 有缓存且缓存正在被冻住（BeingWarpedOut）→ 继续保留
				// 无缓存或缓存没被冻住（攻击者停火）→ 释放
				bool hasValidCache = pExt->AOEState.CachedMain
					&& pExt->AOEState.CachedMain->Health > 0
					&& !pExt->AOEState.CachedMain->InLimbo
					&& pExt->AOEState.CachedMain->BeingWarpedOut;
				if (!hasValidCache)
					invalid = true;
			}
		}
		else
		{
			invalid = true;
		}

		if (invalid)
		{
			// 同 scan 循环：先推进迭代器（erase(iterator)），再销毁。
			// DestroyFakeTemporal 内部还会 erase 同一个 key，顺序反了就是野迭代器二次删除。
			TechnoClass* const pBadTarget = it->first;
			it = SecondaryClaims.erase(it);
			DestroyFakeTemporal(pBadTarget);
		}
		else
		{
			++it;
		}
	}

	// 清理 CachedMainOwners 中不一致的条目
	for (auto it = CachedMainOwners.begin(); it != CachedMainOwners.end(); )
	{
		auto pTarget = it->first;
		auto pOwner = it->second;

		// 先用 ExtMap 验证 pOwner 是否仍有有效扩展（防悬挂指针访问成员崩溃）
		bool invalid = !pTarget || pTarget->Health <= 0 || pTarget->InLimbo;
		if (!invalid)
		{
			auto pExt = TechnoExt::ExtMap.Find(pOwner);
			if (!pExt || !pExt->AOEState.Active || pExt->AOEState.CachedMain != pTarget)
				invalid = true;
			else if (pOwner->Health <= 0 || pOwner->BeingWarpedOut)
				invalid = true;
			else if (pOwner->InLimbo
				&& !(pOwner->Transporter && pOwner->Transporter->GetTechnoType()
					&& pOwner->Transporter->GetTechnoType()->OpenTopped))
				invalid = true;
		}
		if (invalid)
			it = CachedMainOwners.erase(it);
		else
			++it;
	}

	// 兜底（每 15 帧）：清理 FakeTemporals 中失效的条目 + 孤立 BeingWarpedOut
	{
		static int cleanupCounter = 0;
		if (++cleanupCounter >= 15)
		{
			cleanupCounter = 0;

			// ── 保底对账 ──
			// 解冻正常由各自的状态机在转场时完成；这里按"当前状态"重推一次，
			// 防止攻击者/投影者的状态机停跑后（未正常停火、投影者离场）留下永久冻结。

			// 1) 力场：投影者已失效 → 收回（其 AI 可能已停跑，UpdateDeployWarpField 不会再执行）
			for (int i = 0; i < TechnoClass::Array.Count; ++i)
			{
				auto pTech = TechnoClass::Array.Items[i];
				if (!pTech || !DeployWarp::IsProjectorType(pTech))
					continue;

				auto pExt = TechnoExt::ExtMap.Find(pTech);
				if (!pExt || !pExt->WarpField.Active)
					continue;

				if (pTech->Health <= 0 || pTech->InLimbo || pTech->BeingWarpedOut)
				{
					DeployWarp::ReleaseAll(pTech);
					pExt->WarpField.Active = false;
					pExt->WarpField.TargetsInField.clear();
				}
			}

			// 2) 假 Temporal：所属攻击者已失效 → 销毁（销毁内部会解冻并恢复建筑）
			{
				std::vector<TechnoClass*> stale;
				for (auto& ft : FakeTemporals)
				{
					auto pAtk = ft.second.Attacker;
					if (!pAtk || pAtk->Health <= 0 || pAtk->BeingWarpedOut
						|| (pAtk->InLimbo
							&& !(pAtk->Transporter && pAtk->Transporter->GetTechnoType()
								&& pAtk->Transporter->GetTechnoType()->OpenTopped)))
					{
						stale.push_back(ft.first);
					}
				}
				for (auto pTarget : stale)
					DestroyFakeTemporal(pTarget);
			}

			// 3) 我们禁用过、当前却不再被冻着的建筑 → 恢复（否则永久停摆，工厂停产）
			{
				std::vector<TechnoClass*> leaked;
				for (auto pTech : DisabledByUs)
				{
					if (!pTech || pTech->Health <= 0 || pTech->InLimbo)
						continue;   // 死亡由 InvalidatePtr 注销；InLimbo 等回到场上再处理

					if (!pTech->BeingWarpedOut && !pTech->TemporalTargetingMe
						&& !FakeTemporals.count(pTech))
					{
						leaked.push_back(pTech);
					}
				}
				for (auto pTech : leaked)
					ReenableBuilding(pTech);
			}

			// 清理 FakeTemporals 中目标已死的条目
			for (auto it = FakeTemporals.begin(); it != FakeTemporals.end(); )
			{
				auto pTarget = it->first;
				if (!pTarget || pTarget->Health <= 0 || pTarget->InLimbo)
				{
					// 手动清理（不用 DestroyFakeTemporal 避免递归）
					auto pTemp = it->second.FakeTemporal;
					if (pTemp)
					{
						if (pTarget)
						{
							pTarget->TemporalTargetingMe = nullptr;
							pTarget->BeingWarpedOut = false;
							ForceTechnoRedraw(pTarget);
						}
						pTemp->Target = nullptr;
						pTemp->Owner = nullptr;
						TemporalClass::Array.AddItem(pTemp);
						GameDelete(pTemp);
					}

					if (auto* pAtk = it->second.Attacker)
					{
						auto setIt = SecondariesByAttacker.find(pAtk);
						if (setIt != SecondariesByAttacker.end())
						{
							setIt->second.erase(pTarget);
							if (setIt->second.empty())
								SecondariesByAttacker.erase(setIt);
						}
					}

					it = FakeTemporals.erase(it);
				}
				else
				{
					++it;
				}
			}

			// 清理孤立 BeingWarpedOut（没有 FakeTemporal 也没有 TemporalTargetingMe）
			for (int i = 0; i < TechnoClass::Array.Count; ++i)
			{
				auto pTech = TechnoClass::Array.Items[i];
				if (!pTech || pTech->Health <= 0 || pTech->InLimbo || !pTech->BeingWarpedOut)
					continue;
				if (FakeTemporals.count(pTech))
					continue;
				if (SecondaryClaims.find(pTech) != SecondaryClaims.end())
					continue;
				if (pTech->TemporalTargetingMe)
					continue;

				pTech->BeingWarpedOut = false;

				// 建筑：原版时间束（主目标）异常结束时不会走 detach，停摆要在这里补恢复
				ReenableBuilding(pTech);
			}
		}
	}
}

// ============================================================
// 辅助函数
// ============================================================

// 强制单位重绘（刷新 BeingWarpedOut 视觉状态）
void TechnoExt::TemporalAOE::ForceTechnoRedraw(TechnoClass* pTechno)
{
	if (!pTechno) return;

	// 建筑物：遍历地基所有 Cell
	if (auto pBld = abstract_cast<BuildingClass*>(pTechno))
	{
		if (!pBld->Type) return;
		auto pCell = pBld->GetCell();
		if (!pCell) return;
		CellStruct baseCell = pCell->MapCoords;
		CellStruct const* pFoundation = pBld->GetFoundationData(false);
		if (!pFoundation) return;
		int occupyHeight = pBld->Type->OccupyHeight;
		if (occupyHeight <= 0) occupyHeight = 1;
		CellStruct end = { 0x7FFF, 0x7FFF };
		while (*pFoundation != end)
		{
			auto actualCell = baseCell + *pFoundation;
			for (int i = occupyHeight; i > 0; --i)
			{
				if (auto pRedraw = MapClass::Instance.TryGetCellAt(actualCell))
					pRedraw->MarkForRedraw();
				--actualCell.X; --actualCell.Y;
			}
			++pFoundation;
		}
	}
	// 其他单位：重绘所在格
	else
	{
		auto pCell = MapClass::Instance.TryGetCellAt(pTechno->GetCoords());
		if (pCell)
			pCell->MarkForRedraw();
	}
}

// 释放攻击者的所有副目标（恢复建筑功能）
void TechnoExt::TemporalAOE::ReleaseAOESecondaries(TechnoClass* pAttacker, TechnoExt::TemporalAOEState& state)
{
	if (!pAttacker) return;
	ReleaseAttackerLocks(pAttacker);
	DestroyFakeTemporalsByAttacker(pAttacker);
	ClearBuildingsDisabled(state.BuildingsDisabled); // 恢复被禁用的建筑
	state.TargetsInRange.clear();
	state.ExtraWarpAdded = 0;
	state.ContributedTargets.clear();
	state.WarpTimer = 0;
}

// 完全停用攻击者的 AOE 状态
void TechnoExt::TemporalAOE::DeactivateAOE(TechnoClass* pAttacker, TechnoExt::TemporalAOEState& state)
{
	ReleaseAOESecondaries(pAttacker, state);
	ClearBuildingsDisabled(state.BuildingsDisabled);
	CachedMainOwners.erase(state.CachedMain);
	state.CachedMain = nullptr;
	state.CachedMainDead = false;
	state.Active = false;
}

// 我们禁用过的建筑（见 Body.h 的说明）：给周期对账用的登记表
std::unordered_set<TechnoClass*> TechnoExt::TemporalAOE::DisabledByUs;

void TechnoExt::TemporalAOE::MarkBuildingDisabled(TechnoClass* pTech)
{
	if (!pTech)
		return;

	if (auto pBld = abstract_cast<BuildingClass*>(pTech))
	{
		pBld->DisableTemporal();
		DisabledByUs.insert(pBld);
	}
}

// 恢复建筑：仍被任何时间束冻着（含原版时间束）就不动，交给对方恢复。
// 不要求预先登记 —— 原版时间束（主目标）异常结束时也走这里恢复。
void TechnoExt::TemporalAOE::ReenableBuilding(TechnoClass* pTech)
{
	if (!pTech)
		return;

	auto pBld = abstract_cast<BuildingClass*>(pTech);
	if (!pBld)
	{
		DisabledByUs.erase(pTech);
		return;
	}

	if (pBld->Health <= 0 || pBld->InLimbo)
		return;

	if (pBld->BeingWarpedOut || pBld->TemporalTargetingMe)
		return;

	pBld->EnableTemporal();
	ForceTechnoRedraw(pBld);

	if (pBld->Owner)
	{
		pBld->Owner->RecheckPower = true;
		pBld->Owner->RecheckRadar = true;
	}

	DisabledByUs.erase(pTech);
}

// 安全清除建筑禁用列表（逐个恢复后清空）
void TechnoExt::TemporalAOE::ClearBuildingsDisabled(std::unordered_set<TechnoClass*>& set)
{
	for (auto pTech : set)
		ReenableBuilding(pTech);

	set.clear();
}

void TechnoExt::TemporalAOE::PlayWarpAwayAnim(TechnoClass* pTarget)
{
	if (!pTarget) return;

	auto const pWarpAway = RulesClass::Instance ? RulesClass::Instance->WarpAway : nullptr;
	if (pWarpAway)
	{
		auto pAnim = GameCreate<AnimClass>(pWarpAway, pTarget->Location);
		if (pAnim && pTarget->Owner)
			pAnim->Owner = pTarget->Owner;
	}
}

// ============================================================
// 假 Temporal 管理
// ============================================================

// 为副目标创建假 TemporalClass（从游戏 Array/链表拆除，游戏不更新它）
void TechnoExt::TemporalAOE::CreateFakeTemporal(TechnoClass* pAttacker, TechnoClass* pTarget)
{
	if (!pAttacker || !pTarget)
		return;

	// 已有假 Temporal → 跳过
	if (FakeTemporals.count(pTarget))
		return;

	// 创建 TemporalClass（构造会自动加入 Array 和 linked list）
	// 注意：引擎构造体是 TemporalClass(TechnoClass* pOwner)，它会把 pOwner 的
	// TemporalImUsing 指向新实例、并挂进 pOwner 的时间束链。假实例必须完全摘除，
	// 链首也必须还原：否则攻击者的 TemporalImUsing 会指向假实例，假实例销毁后
	// 即成悬垂指针（状态机随后还会把"副目标"误当主目标）。
	TemporalClass* const pSavedImUsing = pAttacker->TemporalImUsing;

	auto pTemp = GameCreate<TemporalClass>(pAttacker);
	if (!pTemp)
	{
		pAttacker->TemporalImUsing = pSavedImUsing;
		return;
	}

	// 从游戏 Array 拆除（不让游戏每帧更新它）
	TemporalClass::Array.Remove(pTemp);

	// 从 linked list 拆除
	if (pTemp->PrevTemporal)
		pTemp->PrevTemporal->NextTemporal = pTemp->NextTemporal;
	if (pTemp->NextTemporal)
		pTemp->NextTemporal->PrevTemporal = pTemp->PrevTemporal;
	pTemp->NextTemporal = nullptr;
	pTemp->PrevTemporal = nullptr;

	// 还原攻击者的时间束链首（引擎构造把它改成了假实例）
	pAttacker->TemporalImUsing = pSavedImUsing;

	// 配置假 Temporal
	pTemp->Target = pTarget;
	pTemp->WarpRemaining = 0x7FFFFFFF; // 永不归零
	pTemp->WarpPerStep = 0;            // 每帧不扣减

	// 设目标关联，驱动游戏渲染
	pTarget->TemporalTargetingMe = pTemp;
	pTarget->BeingWarpedOut = true;

	// 强制重绘，立即呈现冻结效果
	ForceTechnoRedraw(pTarget);

	// 登记到映射
	FakeTemporals[pTarget] = { pTemp, pAttacker };
	SecondariesByAttacker[pAttacker].insert(pTarget);

}

// 销毁副目标的假 Temporal（安全版：目标可能已被其他攻击者销毁）
void TechnoExt::TemporalAOE::DestroyFakeTemporal(TechnoClass* pTarget)
{
	if (!pTarget)
		return;

	auto it = FakeTemporals.find(pTarget);
	if (it == FakeTemporals.end())
		return;

	auto pTemp = it->second.FakeTemporal;
	if (pTemp)
	{
		// 仅当目标未被其他攻击者抹除时才安全地访问它
		bool targetAlive = !WarpingOutTargets.count(pTarget)
			&& pTarget->Health > 0 && !pTarget->InLimbo;

		if (targetAlive)
		{
			if (pTemp->Target == pTarget)
			{
				pTarget->TemporalTargetingMe = nullptr;
				pTarget->BeingWarpedOut = false;
				pTemp->Target = nullptr;
			}
			// 建筑：解冻后必须恢复供电/生产（引擎只会在原版时间束 detach 时替我们恢复）
			ReenableBuilding(pTarget);

			// 强制重绘，刷新视觉状态
			ForceTechnoRedraw(pTarget);
		}
		// 目标已死/正在被抹除：跳过所有对 pTarget 的访问（悬垂指针）
		else if (pTemp->Target == pTarget)
		{
			pTemp->Target = nullptr;
		}

		// 清除 Owner 防止析构时访问
		pTemp->Owner = nullptr;

		// 插回 Array（让析构函数安全地移除自己）
		TemporalClass::Array.AddItem(pTemp);

		// 销毁
		GameDelete(pTemp);
	}

	// 副目标独占锁：销毁假 Temporal 就必须放锁，否则该目标会被永远判为"已被别人锁定"，
	// 之后既冻结不了它，又表现为莫名其妙的互斥（ValidateGlobals 的对账也会调进这里）
	SecondaryClaims.erase(pTarget);

	// 从 SecondariesByAttacker 移除（在 erase 前提取 attacker）
	if (auto* pAtk = it->second.Attacker)
	{
		auto setIt = SecondariesByAttacker.find(pAtk);
		if (setIt != SecondariesByAttacker.end())
		{
			setIt->second.erase(pTarget);
			if (setIt->second.empty())
				SecondariesByAttacker.erase(setIt);
		}
	}

	FakeTemporals.erase(it);
}

// 销毁某个攻击者的所有假 Temporal（用逆向映射 O(1) 取列表，无需遍历全表）
void TechnoExt::TemporalAOE::DestroyFakeTemporalsByAttacker(TechnoClass* pAttacker)
{
	if (!pAttacker) return;

	// 直接从 SecondariesByAttacker 取列表，O(1)，无需遍历全表
	auto setIt = SecondariesByAttacker.find(pAttacker);
	if (setIt != SecondariesByAttacker.end())
	{
		auto targetsToRemove = setIt->second;
		SecondariesByAttacker.erase(setIt);
		for (auto pTarget : targetsToRemove)
			DestroyFakeTemporal(pTarget);
	}
}

// 批量销毁列表中目标的假 Temporal
void TechnoExt::TemporalAOE::DestroyFakeTemporalsByTargetList(const std::vector<TechnoClass*>& targets)
{
	for (auto pTarget : targets)
		DestroyFakeTemporal(pTarget);
}

// 销毁所有假 Temporal（用于存档前清理）
void TechnoExt::TemporalAOE::DestroyAllFakeTemporals()
{
	// 清空逆向映射，防迭代失效
	SecondariesByAttacker.clear();
	// 拷贝键列表，防迭代失效
	std::vector<TechnoClass*> toRemove;
	for (auto& pair : FakeTemporals)
		toRemove.push_back(pair.first);
	for (auto pTarget : toRemove)
		DestroyFakeTemporal(pTarget);
}

void TechnoExt::TemporalAOE::WarpOutTarget(TechnoClass* pTarget, TechnoClass* pKiller, TechnoExt::TemporalAOEState& state)
{
	if (!pTarget)
	{
		return;
	}

	// 核心防护：目标已在抹除集合中 → 跳过，确保每个对象只销毁一次
	// 必须放在最前面，连 GetTechnoType() 都不能调用（悬垂指针上调用虚函数=崩溃）
	if (WarpingOutTargets.count(pTarget))
	{
		return;
	}

	// 加入抹除集合：标记此目标正在被销毁，阻止其他攻击者再次进入
	WarpingOutTargets.insert(pTarget);
	// 注意：早期返回（非销毁路径）必须 erase(pTarget)；成功销毁后不 erase（墓碑保护）

	// 不能抹除攻击者自己
	if (pKiller && pTarget == pKiller)
	{
		WarpingOutTargets.erase(pTarget);
		return;
	}

	// 多层防护：确保目标可被安全地销毁（InLimbo/GetTechnoType 检查）
	// 注意：走到这里 pTarget 已被 WarpingOutTargets 标记保护，即使后续检查失败也不会被二次销毁
	if (pTarget->InLimbo)
	{
		WarpingOutTargets.erase(pTarget);
		return;
	}

	// 检查 TechnoType 是否仍然有效（防止野指针）
	// 崩了好多次怕了怕了
	if (!pTarget->GetTechnoType())
	{
		WarpingOutTargets.erase(pTarget);
		return;
	}

	// 确定击杀者（经验归属）：优先用 killer，如果是 OpenTopped 乘客经验归载具
	TechnoClass* pSource = pTarget;
	if (pKiller && pKiller->Health > 0 && pKiller->GetTechnoType())
	{
		if (!pKiller->InLimbo)
		{
			pSource = pKiller;
		}
		else if (pKiller->Transporter && pKiller->Transporter->GetTechnoType()
			&& pKiller->Transporter->GetTechnoType()->OpenTopped
			&& pKiller->Transporter->Health > 0 && !pKiller->Transporter->InLimbo)
		{
			pSource = pKiller->Transporter;
		}
	}

	pTarget->BeingWarpedOut = true;
	PlayWarpAwayAnim(pTarget);

	if (BuildingClass* pBld = abstract_cast<BuildingClass*>(pTarget))
		state.BuildingsDisabled.erase(pBld);

	// 逐步骤抹除，每次都重新确认目标仍然有效
	if (pTarget && !pTarget->InLimbo)
	{
		pTarget->KillPassengers(pSource);
	}

	if (pTarget && !pTarget->InLimbo)
	{
		pTarget->RegisterDestruction(pSource);
	}

	if (pTarget && !pTarget->InLimbo)
	{
		pTarget->UnInit();
	}

	//   不擦除集合条目！销毁后的指针值作为墓碑保留在集合中，
	//   防止同帧内其他攻击者再次对同一地址调用本函数（悬垂指针保护）。
	//   由 InvalidatePtr（指针失效时）清理集合中的旧条目。
}

// ============================================================
// 公开接口
// ============================================================

// 初始化攻击者的 AOE 状态
void TechnoExt::TemporalAOE::InitAOEState(TechnoClass* pAttacker)
{
	if (!pAttacker)
		return;

	auto pExt = TechnoExt::ExtMap.Find(pAttacker);
	if (!pExt)
		return;

	// 获取当前武器的弹头配置
	WeaponTypeClass* pWeapon = TechnoExt::GetCurrentWeapon(pAttacker);
	if (!pWeapon || !pWeapon->Warhead || !pWeapon->Warhead->Temporal)
	{
		pExt->AOEState.Active = false;
		return;
	}

	auto pWHExt = WarheadTypeExt::ExtMap.Find(pWeapon->Warhead);
	if (!pWHExt || !pWHExt->TemporalAOE_Enable)
	{
		pExt->AOEState.Active = false;
		return;
	}

	// 配置 AOE 状态
	auto& state = pExt->AOEState;
	state.Active = true;
	state.CellSpread = pWHExt->TemporalAOE_CellSpread;
	state.SecondaryWeight = pWHExt->TemporalAOE_SecondaryWeight;
	state.WeaponDamage = pWeapon->Damage;
	// ExtraWarpAdded + WarpTimer 保留存档值，不重置
	// 如果 WarpTimer==0（新激活），计时器块中首次扫描后会设初始值
	state.CachedMain = nullptr;
	state.CachedMainDead = false;
	state.ScanInterval = 5;
	state.ScanCounter = state.ScanInterval; // 初始化后第一次 ++ 即触发扫描，避免读档后延迟
	state.TargetsInRange.clear();
	state.BuildingsDisabled.clear();
}

// 检查攻击者当前武器是否有 TemporalAOE
bool TechnoExt::TemporalAOE::HasAOEWeapon(TechnoClass* pAttacker)
{
	if (!pAttacker)
		return false;

	auto pWeapon = TechnoExt::GetCurrentWeapon(pAttacker);
	if (!pWeapon || !pWeapon->Warhead || !pWeapon->Warhead->Temporal)
		return false;

	auto pWHExt = WarheadTypeExt::ExtMap.Find(pWeapon->Warhead);
	return pWHExt && pWHExt->TemporalAOE_Enable;
}

// ============================================================
// TechnoExt::ExtData::UpdateTemporalAOE() 实现
// ============================================================
void TechnoExt::ExtData::UpdateTemporalAOE()
{
	// 递归防护：防止级联回调导致无限递归
	// 每帧重置的全局计数器，确保一帧内不会无限递归
	static int s_RecursionGuard = 0;
	static int s_lastGuardFrame = 0;
	if (Unsorted::CurrentFrame != s_lastGuardFrame)
	{
		s_lastGuardFrame = Unsorted::CurrentFrame;
		s_RecursionGuard = 0;
	}
	struct RecursionCounter { ~RecursionCounter() { --s_RecursionGuard; } };
	if (++s_RecursionGuard > 10)
	{
		return;
	}
	RecursionCounter guard;

	auto pThis = this->OwnerObject();
	auto& state = this->AOEState;

	//Debug: 路径跟踪（isOT = Is OpenTopped）
	//bool isOT = pThis && pThis->Transporter && pThis->Transporter->GetTechnoType()->OpenTopped;

	if (state.WarpingOut) { return; }

	if (pThis && (pThis->BeingWarpedOut
		|| (pThis->Transporter && pThis->Transporter->BeingWarpedOut)))
	{
		TemporalAOE::DeactivateAOE(pThis, state);
		return;
	}

	auto pTemporal = pThis ? pThis->TemporalImUsing : nullptr;

	if (!pThis || pThis->Health <= 0
		|| (pThis->InLimbo && !(pThis->Transporter && pThis->Transporter->GetTechnoType()
			&& pThis->Transporter->GetTechnoType()->OpenTopped)))
	{
		TemporalAOE::DeactivateAOE(pThis, state);
		return;
	}

	if (!state.Active)
	{
		if (TemporalAOE::HasAOEWeapon(pThis))
		{
			TemporalAOE::InitAOEState(pThis);
		}
		else
		{
			return;
		}
	}

	if (!TemporalAOE::HasAOEWeapon(pThis))
	{
		TemporalAOE::DeactivateAOE(pThis, state);
		return;
	}

	// ═══════════════════════════════════════════════════════════════
	// 缓存主目标状态机（CachedMain + CachedMainDead）
	// curMain = TemporalImUsing->Target（当前游戏时间束目标）
	// CachedMain = 上一帧缓存的主目标（不由 InvalidatePointer 清空）
	// CachedMainDead = RegisterDestruction 钩子或 InvalidatePointer 标记（缓存已销毁）
	//
	// 状态表：
	// curMain | 副目标 | CachedMain | CachedMainDead → 动作
	// ───────┼───────┼───────────┼───────────────┼──────
	//   null  |   有   |   任意     |     true       → 抹除副目标（主目标被游戏抹除）
	//   null  |   有   |   非空(BWO) |     false      → 用 CachedMain 继续（OpenTopped 目标临时丢失）
	//   null  |   有   |   非空(!BWO)|     false      → 释放副目标（攻击者主动停止）
	//   null  |   有   |   空       |     false      → 释放副目标（异常状态）
	//   null  |   空   |   空       |     false      → 闲置
	//   null  |   空   |   非空     |     false      → 闲置，释放缓存
	//   有    |   有   |   空       |     false      → 记录缓存（首次）
	//   有    |   有   |   相同     |     false      → 继续攻击
	//   有    |   有   |   不同     |     false      → 释放旧+重新记录，释放旧副目标
	//   有    |   空   |   空       |     false      → 记录缓存，等待扫描
	//   有    |   空   |   任意     |     false      → 对比缓存，等待扫描
	// ═══════════════════════════════════════════════════════════════
	TechnoClass* curMain = (pThis->TemporalImUsing && pThis->TemporalImUsing->Target
		&& pThis->TemporalImUsing->Target->Health > 0)
		? pThis->TemporalImUsing->Target : nullptr;

	// Debug: OpenTopped 乘员 curMain 诊断
	//if (pThis && pThis->Transporter && pThis->Transporter->GetTechnoType()->OpenTopped)
	//{
	//}

	// 每次进入状态机前修复可能丢失的全局映射（读档/反序列化后 OwnerObject 可能为空）
	if (state.CachedMain && state.CachedMain->Health > 0 && !state.CachedMain->InLimbo)
	{
		auto mapIt = TemporalAOE::CachedMainOwners.find(state.CachedMain);
		if (mapIt == TemporalAOE::CachedMainOwners.end() || mapIt->second != pThis)
			TemporalAOE::CachedMainOwners[state.CachedMain] = pThis;
	}

	bool hasSecondaries = !state.TargetsInRange.empty();

	if (state.CachedMainDead)
	{
		// 缓存的主目标已被游戏抹除（RegisterDestruction 钩子或 InvalidatePointer 触发）
		if (hasSecondaries)
		{

			state.BuildingsDisabled.clear();

			if (state.ExtraWarpAdded > 0 && pTemporal)
			{
				pTemporal->WarpRemaining -= state.ExtraWarpAdded;
				if (pTemporal->WarpRemaining < 1) pTemporal->WarpRemaining = 1;
			}

			state.WarpingOut = true;
			auto targetsToWarp = std::move(state.TargetsInRange);
			state.TargetsInRange.clear();

			// 先销毁所有假 Temporal，再真抹除
			TemporalAOE::DestroyFakeTemporalsByTargetList(targetsToWarp);

			for (auto pSec : targetsToWarp)
			{
				TemporalAOE::WarpOutTarget(pSec, pThis, state);
			}

			// 抹除完成后释放锁（防止其他单位提前解冻副目标）
			TemporalAOE::ReleaseAttackerLocks(pThis);

			state.ExtraWarpAdded = 0;
			state.WarpingOut = false;
		}
		state.CachedMain = nullptr;
		state.CachedMainDead = false;
		state.WarpTimer = 0;
		state.ContributedTargets.clear();
		state.Active = false;
		return;
	}

	if (!curMain)
	{
		// 无当前 TemporalImUsing->Target
		if (state.CachedMain && !state.CachedMainDead)
		{
			// 有缓存但 TemporalImUsing->Target 暂时丢失（常见于 OpenTopped 乘员）
			// 用 CachedMain 继续扫描，保持副目标冻结状态
			if (state.CachedMain->BeingWarpedOut
				&& state.CachedMain->Health > 0 && !state.CachedMain->InLimbo)
			{
				curMain = state.CachedMain;
			}
			else if (state.CachedMain->BeingWarpedOut)
			{
				// 目标正在被抹除 → 等待 InvalidatePointer
				return;
			}
			else
			{
				// 目标未被冻结 → 攻击者已停止攻击，释放副目标
				TemporalAOE::ReleaseAOESecondaries(pThis, state);
				state.CachedMain = nullptr;
				return;
			}
		}
		else if (!state.CachedMain && !state.CachedMainDead)
		{
			// 没有缓存 → 异常或闲置
			if (hasSecondaries)
			{
				TemporalAOE::DeactivateAOE(pThis, state);
			}
			return;
		}
		else
		{
			return;
		}
	}

	// curMain 有效，且真的切了目标（不是 CLEG 停止攻击导致 curMain=null）
	if (curMain && state.CachedMain && state.CachedMain != curMain)
	{
		// 主目标切换：释放旧副目标
		if (state.ExtraWarpAdded > 0 && pTemporal)
		{
			pTemporal->WarpRemaining -= state.ExtraWarpAdded;
			if (pTemporal->WarpRemaining < 1) pTemporal->WarpRemaining = 1;
		}
		TemporalAOE::ReleaseAOESecondaries(pThis, state);
		TemporalAOE::CachedMainOwners.erase(state.CachedMain);
		state.CachedMain = nullptr;
		state.CachedMainDead = false;
		// 不 return，继续往下走到扫描逻辑
	}

	if (!state.CachedMain)
	{
		// 首次记录缓存
		state.CachedMain = curMain;
		state.CachedMainDead = false;
		TemporalAOE::CachedMainOwners[curMain] = pThis;
	}
	else
	{
		// CachedMain == curMain → 继续攻击，同时修复可能丢失的全局映射（读档等场景）
		auto mapIt = TemporalAOE::CachedMainOwners.find(state.CachedMain);
		if (mapIt == TemporalAOE::CachedMainOwners.end() || mapIt->second != pThis)
		{
			TemporalAOE::CachedMainOwners[state.CachedMain] = pThis;
		}
	}

	// =========================================================================
	// 第1步：每 N 帧扫描一次范围（ScanInterval 帧，默认每 5 帧）
	// 在主目标周围寻找副目标，管理进入/离开范围的单位
	// =========================================================================
	if (++state.ScanCounter >= state.ScanInterval)
	{
		state.ScanCounter = 0;

		// 清理全局副目标锁中无效的记录
		// 条件：目标已死 / 攻击者已死 / 攻击者自己被冻 / 攻击者已停止攻击
		for (auto it = TemporalAOE::SecondaryClaims.begin(); it != TemporalAOE::SecondaryClaims.end(); )
		{
			// 检查攻击者是否有 CachedMain 兜底（OpenTopped 乘员 TemporalImUsing->Target 可能临时丢失）
			bool hasCachedFallback = false;
			if (it->second)
			{
				auto pAtkExt = TechnoExt::ExtMap.Find(it->second);
				if (pAtkExt && pAtkExt->AOEState.CachedMain
					&& pAtkExt->AOEState.CachedMain->Health > 0
					&& !pAtkExt->AOEState.CachedMain->InLimbo)
					hasCachedFallback = true;
			}
			bool invalid = !it->first || it->first->Health <= 0
				|| !it->second || it->second->Health <= 0
				|| it->second->BeingWarpedOut;
			if (!invalid && !hasCachedFallback)
			{
				invalid = !it->second->TemporalImUsing || !it->second->TemporalImUsing->Target;
			}
			if (invalid)
			{
				// 必须先推进迭代器再销毁：DestroyFakeTemporal 内部会对同一个 key 再做一次
				// SecondaryClaims.erase，若先调用它，it 指向的节点已被释放，随后 erase(it)
				// 就是对野迭代器二次删除，会破坏哈希表（后续 rehash 抛 length_error / 崩溃）。
				TechnoClass* const pBadTarget = it->first;
				it = TemporalAOE::SecondaryClaims.erase(it);
				TemporalAOE::DestroyFakeTemporal(pBadTarget);
			}
			else
			{
				++it;
			}
		}

		// 获取当前被时间束攻击的目标（TemporalImUsing->Target 丢失时用 curMain/CachedMain 兜底）
		auto pTarget = (pThis->TemporalImUsing && pThis->TemporalImUsing->Target)
			? pThis->TemporalImUsing->Target
			: ((state.CachedMain && state.CachedMain->Health > 0 && !state.CachedMain->InLimbo)
				? state.CachedMain : nullptr);

		// ──────────────────────────────────────────────────────────────
		// 情况 1：目标指针存在（CLEG 正在攻击某个目标）
		//   a) 换了目标且旧目标死了 → 抹除旧副目标，重新扫描
		//   b) 换了目标但旧目标活着 → 释放旧副目标，重新扫描（手动切目标）
		//   c) 目标没换 → 继续攻击，正常扫描
		// ──────────────────────────────────────────────────────────────
		if (pTarget && pTarget->Health > 0 && !pTarget->InLimbo)
		{
			// 目标切换已由前面的缓存状态机处理，此处直接扫描

			// 获取弹头配置（包含 TemporalExclusive 标志）
			bool isExclusive = false;
			bool affectsAllies = false;
			bool affectsAirborne = false;
			bool centerOnAttacker = false;
			auto pWeaponScan = TechnoExt::GetCurrentWeapon(pThis);
			WarheadTypeExt::ExtData* pWHExtScan = nullptr;
			if (pWeaponScan && pWeaponScan->Warhead)
			{
				pWHExtScan = WarheadTypeExt::ExtMap.Find(pWeaponScan->Warhead);
				if (pWHExtScan)
				{
					isExclusive = pWHExtScan->Temporal_Exclusive;
					affectsAllies = pWHExtScan->TemporalAOE_AffectsAllies;
					affectsAirborne = pWHExtScan->TemporalAOE_AffectsAirborne;
					centerOnAttacker = pWHExtScan->TemporalAOE_CenterOnAttacker;
				}
			}

			// 圆心默认取主目标所在格；CenterOnAttacker 时取攻击者自身所在格
			CellStruct targetCell = centerOnAttacker
				? CellClass::Coord2Cell(pThis->GetCoords())
				: CellClass::Coord2Cell(pTarget->GetCoords());
			double cellSpreadSq = state.CellSpread * state.CellSpread;

	// ──────────────────────────────────────────────────────────────
	// 扫描过滤：遍历全场所有 TechnoClass，筛选副目标
	// 排除条件（按顺序）：
	//   1. 自己（攻击者）
	//   2. 主目标本身
	//   3. 攻击者自己的载具（要塞不能被自己的 AOE 冻住）
	//   4. 正在使用超时空武器的单位（防止攻击者之间互相冻结）
	//   5. 已死/InLimbo 的单位
	//   6. 距离超出 CellSpread（使用 2D 格距）
	//   7. 友军（除非 AffectsAllies=true）
	//   8. 空中的单位（除非 AffectsAirborne=true）
	//   9. 被其他 TemporalExclusive 锁定的目标
	// ──────────────────────────────────────────────────────────────
			std::vector<TechnoClass*> newTargets;

			for (int i = 0; i < TechnoClass::Array.Count; ++i)
			{
				auto pCandidate = TechnoClass::Array.Items[i];
				if (!pCandidate) continue;

				if (pCandidate == pThis || pCandidate == pTarget)
				{
					continue;
				}
				// 排除攻击者自己的载具（要塞乘员开火时不能把要塞本身冻住）
				if (pThis->Transporter && pCandidate == pThis->Transporter)
				{
					continue;
				}
				// 防死锁：只排除"自己也是超时空 AOE 攻击者"或"正用时间束指着我们"的单位。
				// 绝不能按 TemporalImUsing 一刀切：任何带时间束武器的普通单位（包括被本 AOE/力场冻过的
				// 投影者）都会持有 TemporalImUsing，一刀切会把"主目标旁边的一整片单位"整体误排除，
				// 表现为"范围里有单位却永远冻不上"。
				bool skipByTemporal = false;
				{
					if (auto pCandExt = TechnoExt::ExtMap.Find(pCandidate))
					{
						if (pCandExt->AOEState.Active)
							skipByTemporal = true;
					}

					if (!skipByTemporal && pCandidate->TemporalImUsing
						&& pCandidate->TemporalImUsing->Target == pThis)
					{
						skipByTemporal = true;
					}
				}

				if (skipByTemporal)
					continue;

				// 互斥：已被其他时间束影响 → 跳过（自己的假 Temporal 除外）
				if (pCandidate->TemporalTargetingMe)
				{
					auto ftIt = TemporalAOE::FakeTemporals.find(pCandidate);
					if (ftIt == TemporalAOE::FakeTemporals.end() || ftIt->second.Attacker != pThis)
						continue;
				}
				if (pCandidate->Health <= 0 || pCandidate->InLimbo)
				{
					continue;
				}

				// 空中的单位（飞行器、悬停中的步兵/车辆）：由 TemporalAOE.AffectsAirborne 决定，默认不纳入
				if (!affectsAirborne && pCandidate->IsInAir())
				{
					continue;
				}

				// 建筑用 foundation 多格检测，其他用中心格
				bool inRange = false;
				if (auto pBld = abstract_cast<BuildingClass*>(pCandidate))
				{
					auto pCell = pBld->GetCell();
					if (pCell)
					{
						CellStruct baseCell = pCell->MapCoords;
						CellStruct const* pFoundation = pBld->GetFoundationData(false);
						if (pFoundation)
						{
							CellStruct end = { 0x7FFF, 0x7FFF };
							while (*pFoundation != end)
							{
								int dx = (baseCell.X + pFoundation->X) - targetCell.X;
								int dy = (baseCell.Y + pFoundation->Y) - targetCell.Y;
								if (dx * dx + dy * dy <= cellSpreadSq) { inRange = true; break; }
								++pFoundation;
							}
						}
					}
				}
				else
				{
					CellStruct candCell = CellClass::Coord2Cell(pCandidate->GetCoords());
					int dx = candCell.X - targetCell.X;
					int dy = candCell.Y - targetCell.Y;
					inRange = (dx * dx + dy * dy <= cellSpreadSq);
				}

				if (!inRange)
				{
					continue;
				}

				if (!affectsAllies && (!pThis->Owner || pThis->Owner->IsAlliedWith(pCandidate)))
				{
					continue;
				}

				if (isExclusive)
				{
					auto lockIt = TemporalExclusive::TargetsMap.find(pCandidate);
					if (lockIt != TemporalExclusive::TargetsMap.end() && lockIt->second != pThis)
					{
						continue;
					}
				}

				// 检查是否已被其他 AOE 武器锁定为副目标（仅 Exclusive 武器不能抢）
				{
					auto claimIt = TemporalAOE::SecondaryClaims.find(pCandidate);
					if (isExclusive && claimIt != TemporalAOE::SecondaryClaims.end() && claimIt->second != pThis)
					{
						continue;
					}
				}

				newTargets.push_back(pCandidate);
			}

			std::unordered_set<TechnoClass*> newTargetSet(newTargets.begin(), newTargets.end());
			std::unordered_set<TechnoClass*> oldTargetSet(state.TargetsInRange.begin(), state.TargetsInRange.end());

			// ---------------------------------------------------------------
			// 更新 TargetsInRange 列表，处理副目标进出范围
			// ---------------------------------------------------------------
			bool targetsChanged = false;

			// 离开范围的目标：清理 BuildingsDisabled 记录
			// 注意：拷贝迭代，防止 InvalidatePointer 并发修改原容器
			for (auto pOld : std::vector<TechnoClass*>(state.TargetsInRange))
			{
				if (!pOld) continue;
				// 跳过正在被其他攻击者抹除的目标（悬垂指针不能调用 WhatAmI/GetTechnoType）
				if (TemporalAOE::WarpingOutTargets.count(pOld))
					continue;
				bool stillInRange = newTargetSet.find(pOld) != newTargetSet.end();
				if (!stillInRange)
				{
					targetsChanged = true;
					if (auto pBld = abstract_cast<BuildingClass*>(pOld))
					{
						TemporalAOE::ReenableBuilding(pBld);   // 仍被别的时间束冻着时会自动跳过
						state.BuildingsDisabled.erase(pBld);
					}
				}
			}

			// 新进入范围的目标
			for (auto pNew : newTargets)
			{
				if (!pNew) continue;
				// 跳过正在被其他攻击者抹除的目标
				if (TemporalAOE::WarpingOutTargets.count(pNew))
					continue;
				bool isNew = oldTargetSet.find(pNew) == oldTargetSet.end();
				if (!isNew) continue;

				targetsChanged = true;

				// 新目标进入范围 → 累加其时间贡献（永不扣减）
				if (state.ContributedTargets.find(pNew) == state.ContributedTargets.end())
				{
					auto const pNewType = pNew->GetTechnoType();
					int contribution = pNewType
						? static_cast<int>(10.0 * pNewType->Strength * state.SecondaryWeight)
						: 0;
					state.ExtraWarpAdded += contribution;
					state.WarpTimer += contribution; // 同步展开计时器
					state.ContributedTargets.insert(pNew);
				}

				if (auto pBld = abstract_cast<BuildingClass*>(pNew))
				{
					if (pBld->Health > 0 && !pBld->InLimbo)
					{
						TemporalAOE::MarkBuildingDisabled(pBld);   // DisableTemporal + 登记，供周期对账兜底
						TemporalAOE::ForceTechnoRedraw(pBld);
						if (pBld->Owner)
						{
							pBld->Owner->RecheckPower = true;
							pBld->Owner->RecheckRadar = true;
						}
					}
					state.BuildingsDisabled.insert(pBld);
				}
			}

			// ---------------------------------------------------------------
			// 管理 BeingWarpedOut + 独占锁
			// 每次扫描都重新断言（即使 targetsChanged=false，防止其他实例释放后未重新冻结）
			// ---------------------------------------------------------------

			// 离开范围的副目标：释放独占锁 + 清除 BeingWarpedOut
			// 拷贝迭代，防 InvalidatePointer 并发修改原容器
			for (auto pOld : std::vector<TechnoClass*>(state.TargetsInRange))
			{
				if (!pOld) continue;
				bool stillExists = newTargetSet.find(pOld) != newTargetSet.end();
				if (!stillExists)
				{
					TemporalAOE::SecondaryClaims.erase(pOld);
					// 如果目标正在被其他攻击者抹除，跳过剩余的访问
					if (!TemporalAOE::WarpingOutTargets.count(pOld) && pOld->Health > 0 && !pOld->InLimbo)
					{
						TemporalAOE::DestroyFakeTemporal(pOld);
						TemporalAOE::ForceTechnoRedraw(pOld);
					}
				}
			}

			// 所有当前副目标：重新断言冻结状态（包括新进入的 + 已有的）
			for (auto pNew : newTargets)
			{
				if (!pNew) continue;
				// 双重检查：确保目标仍然存活且指针有效
				if (pNew->Health <= 0 || pNew->InLimbo)
					continue;
				// 跳过正在被其他攻击者抹除的目标（避免访问悬垂指针）
				if (TemporalAOE::WarpingOutTargets.count(pNew))
					continue;
				// 绝对禁止：攻击者自己或自己的载具不能冻结
				if (pNew == pThis || (pThis->Transporter && pNew == pThis->Transporter))
					continue;
				if (isExclusive)
				{
					// Exclusive 武器：只在未被其他 AOE 锁定时才冻结
					auto claimIt = TemporalAOE::SecondaryClaims.find(pNew);
					if (claimIt != TemporalAOE::SecondaryClaims.end() && claimIt->second != pThis)
						continue;
				}
				// 断言独占锁 + 冻结效果
				TemporalAOE::SecondaryClaims[pNew] = pThis;
				TemporalAOE::CreateFakeTemporal(pThis, pNew);
				TemporalAOE::ForceTechnoRedraw(pNew);
			}

			// ---------------------------------------------------------------
			// 副目标有变化 → 日志记录
			// ExtraWarpAdded 和 WarpTimer 已在上述新目标进入时同步更新。
			// WarpTimer 的每帧扣减由下方的计时器块统一管理。
			// ---------------------------------------------------------------
			if (targetsChanged)
			{
			}

			state.TargetsInRange = std::move(newTargets);
		}
		// ──────────────────────────────────────────────────────────────
		// 情况 2, 3：目标指针不存在，对应状态机已在顶部处理
		// 此处仅做兜底清理
		// ──────────────────────────────────────────────────────────────
		else
		{
			// 兜底：异常状态下仍有副目标残留 → 释放
			if (!state.TargetsInRange.empty())
			{
				TemporalAOE::ReleaseAOESecondaries(pThis, state);
			}
			state.CachedMain = nullptr;
			state.CachedMainDead = false;
		}
	}

	// ═══════════════════════════════════════════════════════════════
	// 每帧兜底检查（状态机 + 扫描已处理核心逻辑，此处做冗余清理）
	// ═══════════════════════════════════════════════════════════════

	// 同步 CachedMain（状态机在顶部已处理，此处仅做冗余同步）
	if (state.Active && pThis->TemporalImUsing)
	{
		auto pTemporalTarget = pThis->TemporalImUsing->Target;
		if (pTemporalTarget && pTemporalTarget->Health > 0 && !pTemporalTarget->InLimbo)
		{
			if (pTemporalTarget != state.CachedMain)
			{
				state.CachedMain = pTemporalTarget;
				state.CachedMainDead = false;
			}
		}
	}

	if (state.Active && pThis->TemporalImUsing)
	{
		{
			int baseWarp = 0;
			auto const pWarpTarget = pThis->TemporalImUsing->Target;
			if (pWarpTarget && pWarpTarget->Health > 0)
			{
				if (auto const pWarpType = pWarpTarget->GetTechnoType())
					baseWarp = 10 * pWarpType->Strength;
			}
			if (state.WarpTimer == 0)
			{
				state.WarpTimer = baseWarp + state.ExtraWarpAdded;
			}
		}

		// 每帧扣减（自由倒计时，不再被顶满）
		if (state.WarpTimer > 0)
		{
			const int drainPerFrame = state.WeaponDamage;
			state.WarpTimer -= drainPerFrame;
			if (state.WarpTimer < 0) state.WarpTimer = 0;
		}

		// WarpRemaining：与内部计时器完全隔离
		// 永远写 0x7FFFFFFF 保活，引擎永不因 WarpRemaining 归零而抹除主目标
		// 计时器归零时，一起抹除所有副目标 + 写小值触发主目标死亡
		if (state.WarpTimer <= 0)
		{
			// 抹除所有副目标
			if (!state.TargetsInRange.empty())
			{
				state.WarpingOut = true;
				auto targetsToWarp = std::move(state.TargetsInRange);
				state.TargetsInRange.clear();
				state.ExtraWarpAdded = 0;
				state.ContributedTargets.clear();
				TemporalAOE::DestroyFakeTemporalsByTargetList(targetsToWarp);
				for (auto pSec : targetsToWarp)
					TemporalAOE::WarpOutTarget(pSec, pThis, state);
				TemporalAOE::ReleaseAttackerLocks(pThis);
				state.WarpingOut = false;
			}
			// 触发主目标死亡
			pThis->TemporalImUsing->WarpRemaining = 1;
		}
		else
		{
			pThis->TemporalImUsing->WarpRemaining = 0x7FFFFFFF;
		}

		{
			auto secIt = TemporalAOE::SecondariesByAttacker.find(pThis);
			if (secIt != TemporalAOE::SecondariesByAttacker.end())
			{
				for (auto pSec : secIt->second)
				{
					auto ftIt = TemporalAOE::FakeTemporals.find(pSec);
					if (ftIt == TemporalAOE::FakeTemporals.end())
						continue;
					if (auto pFake = ftIt->second.FakeTemporal)
					{
						pFake->WarpRemaining = 0x7FFFFFFF;
						pFake->WarpPerStep = 0;
					}
				}
			}
		}
	}

	//// ═══ 每帧 Log：内部计时器状态 + 引擎 WarpRemaining ═══
	//if (state.Active && pThis->TemporalImUsing)
	//{
	//}

	// 遍历副目标列表，清除已死/无效的指针
	for (auto it = state.TargetsInRange.begin(); it != state.TargetsInRange.end(); )
	{
		auto pT = *it;
		if (!pT || pT->Health <= 0 || pT->InLimbo)
		{
			TemporalAOE::SecondaryClaims.erase(pT);
			TemporalAOE::DestroyFakeTemporal(pT);
			state.ContributedTargets.erase(pT);
			it = state.TargetsInRange.erase(it);
		}
		else
		{
			++it;
		}
	}

	// 兜底：CachedMainDead 标记仍有副目标（状态机应已处理，此处冗余）
	if (state.CachedMainDead && !state.TargetsInRange.empty())
	{
		TemporalAOE::ReleaseAttackerLocks(pThis);
		state.WarpingOut = true;
		auto targetsToWarp = state.TargetsInRange;
		state.TargetsInRange.clear();
		state.ExtraWarpAdded = 0;
		TemporalAOE::DestroyFakeTemporalsByTargetList(targetsToWarp);
		for (auto pTarget : targetsToWarp)
			TemporalAOE::WarpOutTarget(pTarget, pThis, state);
		state.WarpingOut = false;
		state.CachedMain = nullptr;
		state.CachedMainDead = false;
	}

	// 异常恢复：没有主目标但有副目标残留 → 释放
	if (!state.CachedMain && !state.CachedMainDead && !state.TargetsInRange.empty())
	{
		TemporalAOE::ReleaseAOESecondaries(pThis, state);
	}
}
