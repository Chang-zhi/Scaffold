#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <TemporalClass.h>
#include <BuildingClass.h>
#include <HouseClass.h>
#include <CellClass.h>

#include <Ext/Techno/Body.h>
#include <Ext/TechnoType/Body.h>
#include <Ext/Event/Body.h>
#include <Utilities/Debug.h>
#include <Utilities/Patch.h>
#include <Unsorted.h>

std::unordered_map<TechnoClass*, std::unordered_set<TechnoClass*>> TechnoExt::DeployWarp::ByProjector;

namespace
{
	inline bool IsAlive(TechnoClass* pTech) noexcept
	{
		return pTech && pTech->Health > 0 && !pTech->InLimbo;
	}

	void SetBuildingWarped(BuildingClass* pBld, bool warped)
	{
		if (!IsAlive(pBld))
			return;

		if (warped)
			TechnoExt::TemporalAOE::MarkBuildingDisabled(pBld);
		else
			TechnoExt::TemporalAOE::ReenableBuilding(pBld);

		if (pBld->Owner)
		{
			pBld->Owner->RecheckPower = true;
			pBld->Owner->RecheckRadar = true;
		}
	}

	void SetTechnoWarped(TechnoClass* pTech, bool warped)
	{
		if (auto pBld = abstract_cast<BuildingClass*>(pTech))
			SetBuildingWarped(pBld, warped);
	}

	// 判断是否落在力场内：建筑按 foundation 多格，其余按中心格；沿用 TemporalAOE 的格距平方算法
	bool IsInFieldRange(TechnoClass* pCand, const CellStruct& center, double radiusSq)
	{
		if (auto pBld = abstract_cast<BuildingClass*>(pCand))
		{
			auto pCell = pBld->GetCell();
			if (!pCell)
				return false;

			const CellStruct base = pCell->MapCoords;
			auto pFoundation = pBld->GetFoundationData(false);
			if (!pFoundation)
				return false;

			const CellStruct end = { 0x7FFF, 0x7FFF };
			while (*pFoundation != end)
			{
				const int dx = (base.X + pFoundation->X) - center.X;
				const int dy = (base.Y + pFoundation->Y) - center.Y;
				if (dx * dx + dy * dy <= radiusSq)
					return true;
				++pFoundation;
			}
			return false;
		}

		const CellStruct candCell = CellClass::Coord2Cell(pCand->GetCoords());
		const int dx = candCell.X - center.X;
		const int dy = candCell.Y - center.Y;
		return dx * dx + dy * dy <= radiusSq;
	}

	// 复刻 0x443410 case 4 里"会不会自己下发 Mission::Unload"的判定，
	// 只有这三类类型会被原函数下发（Dock 走的是另一个事件，不算）。用于避免点击时切换两次。
	bool OriginalWouldQueueUnload(BuildingClass* pBld)
	{
		const auto pType = pBld->GetTechnoType();
		if (!pType)
			return true;   // 判不出来就当原函数会下发，宁可不接管

		const auto pRaw = reinterpret_cast<const char*>(pType);
		return pRaw[0xCD1] != 0                    // GapGenerator
			|| pRaw[0x16AE] != 0                   // 掩体乘客标志 A
			|| pRaw[0x16AF] != 0                   // 掩体乘客标志 B
			|| pBld->GetOccupantCount() > 0;       // 有驻军
	}

	// 非建筑的部署光标与点击，三类各自的 vtable 槽
	//   +0x74 （2 栈参，retn 8）  ：Unit 0x73FD50 / Infantry 0x51E3B0 / Aircraft 0x417CC0
	//   +0x144（3 栈参，retn 0Ch）：Unit 0x738890 / Infantry 0x51F190 / Aircraft 0x417BD0
	// 全库只有 0x4AE844、0x4DCA93 两处调用 +0x144 且都是 3 栈参，故可安全换槽。
	// 只换槽不 patch 代码（0x443410 入口与 Ares/Phobos 叠加曾 call 到未映射地址），并链回槽内原实现。
	// 注意：MSVC 不允许在自由函数上写 __thiscall（C3865），
	// 这里用 __fastcall + 占位 EDX 参数：ECX/EDX 传参等价，栈清理字节数与原函数一致。
	using GetActionFn_t = int(__thiscall*)(TechnoClass*, void*, int);
	using ClickedActionFn_t = char(__thiscall*)(TechnoClass*, int, DWORD, DWORD);

	DWORD g_origGetAction[3] = { 0, 0, 0 };       // Unit / Infantry / Aircraft 的 +0x74 原实现
	DWORD g_origClickedAction[3] = { 0, 0, 0 };   // Unit / Infantry / Aircraft 的 +0x144 原实现

	// 只有鼠标指着的就是投影者自己时才给 Self_Deploy，
	// 否则选中投影者后"点空地/点别人"也会变成部署光标。
	bool WantSelfDeploy(TechnoClass* pThis, void* pTarget)
	{
		return TechnoExt::DeployWarp::IsProjectorType(pThis)
			&& reinterpret_cast<void*>(pThis) == pTarget;
	}

	int __fastcall GetAction_Unit(void* pThisV, int, void* pTarget, int a3)
	{
		auto pThis = static_cast<TechnoClass*>(pThisV);

		if (WantSelfDeploy(pThis, pTarget))
			return static_cast<int>(Action::Self_Deploy);

		if (TechnoExt::DeployWarp::IsProjectorType(pThis))
			TechnoExt::DeployWarp::DebugGetAction(pThis, pTarget);

		return reinterpret_cast<GetActionFn_t>(g_origGetAction[0])(pThis, pTarget, a3);
	}

	int __fastcall GetAction_Infantry(void* pThisV, int, void* pTarget, int a3)
	{
		auto pThis = static_cast<TechnoClass*>(pThisV);

		if (WantSelfDeploy(pThis, pTarget))
			return static_cast<int>(Action::Self_Deploy);

		return reinterpret_cast<GetActionFn_t>(g_origGetAction[1])(pThis, pTarget, a3);
	}

	int __fastcall GetAction_Aircraft(void* pThisV, int, void* pTarget, int a3)
	{
		auto pThis = static_cast<TechnoClass*>(pThisV);

		if (WantSelfDeploy(pThis, pTarget))
			return static_cast<int>(Action::Self_Deploy);

		return reinterpret_cast<GetActionFn_t>(g_origGetAction[2])(pThis, pTarget, a3);
	}

	char __fastcall ClickedAction_Unit(void* pThisV, int, int action, DWORD a2, DWORD a3)
	{
		auto pThis = static_cast<TechnoClass*>(pThisV);

		if (TechnoExt::DeployWarp::OnActionClicked(pThis, action))
			return 1;

		return reinterpret_cast<ClickedActionFn_t>(g_origClickedAction[0])(pThis, action, a2, a3);
	}

	char __fastcall ClickedAction_Infantry(void* pThisV, int, int action, DWORD a2, DWORD a3)
	{
		auto pThis = static_cast<TechnoClass*>(pThisV);

		if (TechnoExt::DeployWarp::OnActionClicked(pThis, action))
			return 1;

		return reinterpret_cast<ClickedActionFn_t>(g_origClickedAction[1])(pThis, action, a2, a3);
	}

	char __fastcall ClickedAction_Aircraft(void* pThisV, int, int action, DWORD a2, DWORD a3)
	{
		auto pThis = static_cast<TechnoClass*>(pThisV);

		if (TechnoExt::DeployWarp::OnActionClicked(pThis, action))
			return 1;

		return reinterpret_cast<ClickedActionFn_t>(g_origClickedAction[2])(pThis, action, a2, a3);
	}
}

// ============================================================
// 公开接口
// ============================================================

bool TechnoExt::DeployWarp::IsProjectorType(TechnoClass* pTech)
{
	if (!pTech)
		return false;

	auto pTypeExt = TechnoTypeExt::ExtMap.Find(pTech->GetTechnoType());
	return pTypeExt && pTypeExt->DeployWarp_Enable;
}

// 部署键切换：开 → 立即扫描；关 → 收起力场
void TechnoExt::DeployWarp::Toggle(TechnoClass* pThis)
{
	auto pExt = TechnoExt::ExtMap.Find(pThis);
	if (!pExt)
		return;

	auto& state = pExt->WarpField;
	state.Active = !state.Active;

	Debug::Log("[DeployWarp] Toggle %s -> Active=%d\n",
		pThis->GetTechnoType() ? pThis->GetTechnoType()->ID : "?", state.Active ? 1 : 0);

	if (state.Active)
	{
		state.ScanCounter = ScanInterval;
		state.TargetsInField.clear();
	}
	else
	{
		ReleaseAll(pThis);
		state.TargetsInField.clear();
	}
}

// 点击执行入口（vtable+0x144 包装调用）：只有"部署动作 + 投影类型"才接管。
// 点击是本机私有输入 → 只投递事件，状态由各客户端在事件响应里各自切换（联机同步）。
bool TechnoExt::DeployWarp::OnActionClicked(TechnoClass* pThis, int action)
{
	if (action != static_cast<int>(Action::Self_Deploy) || !IsProjectorType(pThis))
		return false;

	Debug::Log("[DeployWarp] click %s -> request toggle\n",
		pThis->GetTechnoType() ? pThis->GetTechnoType()->ID : "?");

	EventExt::RaiseDeployWarpToggle(pThis);
	return true;
}

// 诊断：投影类型被求动作但目标不是自己时，打印一次（每帧最多一行），
// 用来确认"悬停判定"的入参形态。
void TechnoExt::DeployWarp::DebugGetAction(TechnoClass* pThis, void* pTarget)
{
	static int s_lastFrame = -1;

	if (s_lastFrame == Unsorted::CurrentFrame)
		return;

	s_lastFrame = Unsorted::CurrentFrame;
	Debug::Log("[DeployWarp] getaction %s: target=%p self=%p\n",
		pThis->GetTechnoType() ? pThis->GetTechnoType()->ID : "?", pTarget, pThis);
}

// 启动时（Scaffold::ExeRun）替换 vtable 槽。保存原实现后替换，
// 这样即使 Ares/Phobos 已经换过这两个槽，我们的包装也会把调用链下去。
void TechnoExt::DeployWarp::InstallActionWrappers()
{
	auto swapSlot = [](DWORD slot, DWORD wrapper, DWORD& original)
	{
		original = *reinterpret_cast<DWORD*>(slot);

		if (original == wrapper)
			return;

		Patch::Apply_VTABLE(slot, wrapper);
		Debug::Log("[DeployWarp] vtable %X: %X -> %X\n", slot, original, wrapper);
	};

	// +0x74：决定动作（光标 + 点击时下发的动作）
	swapSlot(0x7F5CE4, reinterpret_cast<DWORD>(&GetAction_Unit), g_origGetAction[0]);         // UnitClass
	swapSlot(0x7EB0CC, reinterpret_cast<DWORD>(&GetAction_Infantry), g_origGetAction[1]);     // InfantryClass
	swapSlot(0x7E2318, reinterpret_cast<DWORD>(&GetAction_Aircraft), g_origGetAction[2]);     // AircraftClass

	// +0x144：执行点击
	swapSlot(0x7F5DB4, reinterpret_cast<DWORD>(&ClickedAction_Unit), g_origClickedAction[0]);
	swapSlot(0x7EB19C, reinterpret_cast<DWORD>(&ClickedAction_Infantry), g_origClickedAction[1]);
	swapSlot(0x7E23E8, reinterpret_cast<DWORD>(&ClickedAction_Aircraft), g_origClickedAction[2]);
}

// 收起力场：解冻全部目标并恢复建筑
void TechnoExt::DeployWarp::ReleaseAll(TechnoClass* pProjector)
{
	if (!pProjector)
		return;

	auto it = ByProjector.find(pProjector);
	if (it == ByProjector.end())
		return;

	auto targets = std::move(it->second);
	ByProjector.erase(it);

	for (auto pTarget : targets)
	{
		if (!pTarget)
			continue;

		TemporalAOE::DestroyFakeTemporal(pTarget);
		SetTechnoWarped(pTarget, false);
		TemporalAOE::ForceTechnoRedraw(pTarget);
	}
}

// 指针失效清理：投影者收起力场，目标从各投影者集合移除
void TechnoExt::DeployWarp::InvalidatePtr(void* ptr)
{
	if (!ptr)
		return;

	auto pTech = static_cast<TechnoClass*>(ptr);

	// 投影者失效（DTOR 钩子会先 ReleaseAll，这里兜底）
	if (ByProjector.find(pTech) != ByProjector.end())
		ReleaseAll(pTech);

	// 目标失效：假 Temporal 由 TemporalAOE 统一回收
	for (auto& pair : ByProjector)
		pair.second.erase(pTech);
}

// ============================================================
// 每帧更新（TechnoClass::AI 调用）
// ============================================================

void TechnoExt::ExtData::UpdateDeployWarpField()
{
	auto pThis = this->OwnerObject();
	auto& state = this->WarpField;

	// 非投影者类型：确保力场已收起
	if (!DeployWarp::IsProjectorType(pThis))
	{
		if (state.Active)
		{
			state.Active = false;
			DeployWarp::ReleaseAll(pThis);
			state.TargetsInField.clear();
		}
		return;
	}

	if (!state.Active)
		return;

	// 投影者失效 → 收起
	if (!IsAlive(pThis) || pThis->BeingWarpedOut)
	{
		state.Active = false;
		DeployWarp::ReleaseAll(pThis);
		state.TargetsInField.clear();
		return;
	}

	auto pTypeExt = TechnoTypeExt::ExtMap.Find(pThis->GetTechnoType());
	if (!pTypeExt || pTypeExt->DeployWarp_Range <= 0.0)
	{
		DeployWarp::ReleaseAll(pThis);
		state.TargetsInField.clear();
		return;
	}

	// 托管条目保活：假 Temporal 永不归零（防任何路径让它触发主目标抹除）
	for (auto pTarget : state.TargetsInField)
	{
		auto ftIt = TemporalAOE::FakeTemporals.find(pTarget);
		if (ftIt != TemporalAOE::FakeTemporals.end()
			&& ftIt->second.Attacker == pThis && ftIt->second.FakeTemporal)
		{
			ftIt->second.FakeTemporal->WarpRemaining = 0x7FFFFFFF;
			ftIt->second.FakeTemporal->WarpPerStep = 0;
		}
	}

	// 清理已消亡的目标
	for (auto it = state.TargetsInField.begin(); it != state.TargetsInField.end(); )
	{
		auto pTarget = *it;
		if (!pTarget || pTarget->Health <= 0 || pTarget->InLimbo)
		{
			TemporalAOE::DestroyFakeTemporal(pTarget);
			it = state.TargetsInField.erase(it);
		}
		else
		{
			++it;
		}
	}

	if (++state.ScanCounter < DeployWarp::ScanInterval)
		return;
	state.ScanCounter = 0;

	HouseClass* pOwner = pThis->Owner;
	const bool affectsEnemies = pTypeExt->DeployWarp_AffectsEnemies;
	const bool affectsAllies = pTypeExt->DeployWarp_AffectsAllies;
	const bool affectsAirborne = pTypeExt->DeployWarp_AffectsAirborne;
	const double radius = pTypeExt->DeployWarp_Range;
	const double radiusSq = radius * radius;
	const CellStruct center = CellClass::Coord2Cell(pThis->GetCoords());

	Debug::Log("[DeployWarp] scan %s center=(%d,%d) range=%.2f ownerIdx=%d\n",
		pThis->GetTechnoType()->ID, center.X, center.Y, radius,
		pOwner ? pOwner->ArrayIndex : -1);

	std::vector<TechnoClass*> newTargets;
	int skippedAirborne = 0;

	for (int i = 0; i < TechnoClass::Array.Count; ++i)
	{
		auto pCand = TechnoClass::Array.Items[i];
		if (!pCand || pCand == pThis)
			continue;
		if (pCand->Health <= 0 || pCand->InLimbo)
			continue;

		// 硬编码：带 DeployWarp.Enable 的类型永不被冻结
		if (DeployWarp::IsProjectorType(pCand))
			continue;

		// 空中目标（飞行器、悬停中的步兵/车辆）：由 DeployWarp.AffectsAirborne 决定，默认不纳入
		if (!affectsAirborne && pCand->IsInAir())
		{
			++skippedAirborne;
			continue;
		}

		// 占用仲裁：别人（其他力场 / 超时空 AOE / 真实时间束）占着的不抢
		auto ftIt = TemporalAOE::FakeTemporals.find(pCand);
		const bool ownedByMe = ftIt != TemporalAOE::FakeTemporals.end()
			&& ftIt->second.Attacker == pThis;
		if (!ownedByMe)
		{
			if (ftIt != TemporalAOE::FakeTemporals.end())
				continue;
			if (pCand->TemporalTargetingMe)
				continue;
		}

		// 阵营：投影者自己恒纳入；盟友 / 敌军各自由开关决定（无主的归入敌军一档）
		const bool isSelf = pOwner && pCand->Owner == pOwner;
		if (!isSelf)
		{
			const bool isAlly = pOwner && pOwner->IsAlliedWith(pCand);
			if (isAlly ? !affectsAllies : !affectsEnemies)
				continue;
		}

		if (!IsInFieldRange(pCand, center, radiusSq))
			continue;

		newTargets.push_back(pCand);
	}

	const std::unordered_set<TechnoClass*> newSet(newTargets.begin(), newTargets.end());
	const std::unordered_set<TechnoClass*> oldSet(state.TargetsInField.begin(), state.TargetsInField.end());

	// 离开范围：解冻并恢复
	for (auto pOld : std::vector<TechnoClass*>(state.TargetsInField))
	{
		if (!pOld || newSet.count(pOld))
			continue;

		TemporalAOE::DestroyFakeTemporal(pOld);
		SetTechnoWarped(pOld, false);
		TemporalAOE::ForceTechnoRedraw(pOld);

		auto byIt = DeployWarp::ByProjector.find(pThis);
		if (byIt != DeployWarp::ByProjector.end())
			byIt->second.erase(pOld);
	}

	// 场内目标：登记并断言冻结（含新进入的与已有的）
	for (auto pNew : newTargets)
	{
		if (!pNew)
			continue;

		if (oldSet.find(pNew) == oldSet.end())
		{
			// 竞争失败（别的时间束先占）→ 放弃
			if (TemporalAOE::FakeTemporals.count(pNew))
				continue;

			SetTechnoWarped(pNew, true);
		}

		DeployWarp::ByProjector[pThis].insert(pNew);
		TemporalAOE::CreateFakeTemporal(pThis, pNew);
		TemporalAOE::ForceTechnoRedraw(pNew);
	}

	state.TargetsInField = std::move(newTargets);

	Debug::Log("[DeployWarp] scan %s done: old=%d new=%d airSkip=%d\n",
		pThis->GetTechnoType()->ID, (int)oldSet.size(), (int)state.TargetsInField.size(),
		skippedAirborne);
}

// 部署快捷键 / 侧边栏"部署"命令：0x730AF0（四类通用入口）对每个选中对象要过 5 道"原生可部署"门禁，
// 建筑的 [+0x314] 分支是 0x465D30（xor al,al / retn，恒 0），所以挂本功能的类型全被挡在门外。
// 在第 1 道门禁（0x730C38：call dword ptr [eax+0A0h]，6 字节，ECX = 对象、EAX = vtable）前接管：
// 投影类型切换力场并跳到"跳过本对象"落点（不下发 EventType::Deploy，故不打断单位指令）；其它类型手工执行原调用。
DEFINE_HOOK(0x730C38, DeployCommand_DeployWarp, 0x6)
{
	GET(TechnoClass*, pThis, ESI);
	GET(DWORD, vtbl, EAX);

	if (!pThis || !vtbl)
		return 0;   // 原样（trampoline 会补执行被覆盖的 call）

	if (TechnoExt::DeployWarp::IsProjectorType(pThis))
	{
		Debug::Log("[DeployWarp] deploy cmd: %s -> request toggle\n",
			pThis->GetTechnoType() ? pThis->GetTechnoType()->ID : "?");

		// 部署键是本机私有输入，且这里跳过了原版的 QueueEvent(Deploy)，
		// 所以必须自己投递事件，状态由各客户端在事件响应里各自切换（联机同步）
		EventExt::RaiseDeployWarpToggle(pThis);
		return 0x730D28;   // 跳过该对象剩余门禁与事件下发
	}

	// 非本功能类型：原样执行被覆盖的 call [eax+0A0h]，再跳回 0x730C3E 的 test al,al
	using CanDeploy_t = bool(__thiscall*)(TechnoClass*);
	const bool allowed = reinterpret_cast<CanDeploy_t>(
		*reinterpret_cast<DWORD*>(reinterpret_cast<char*>(vtbl) + 0xA0))(pThis);

	R->EAX(allowed ? 1 : 0);
	return 0x730C3E;
}

// 钩子布局原则：不钩函数入口，尤其入口同时是 vtable 目标时（Ares/Phobos 也在那打补丁，
// 实测 0x443410 入口叠加后会 call 到未映射的 0xE9F00000）；下面都落在函数内部并覆盖整条指令。

// 部署切换：BuildingClass::Mission_Unload（vtable +0x23C = 0x44D880）内部。
// 入口前 7 字节是 sub esp,40h / push ebx / push ebp / mov ebp,ecx，
// 钩在 0x44D889（mov eax,[ebp+0] + call [eax+408h]，共 9 字节），此时 EBP = this。
DEFINE_HOOK(0x44D889, BuildingClass_Mission_Unload_DeployWarp, 0x9)
{
	GET(BuildingClass*, pThis, EBP);

	// MissionClass::NextMission 会把 MissionStatus 归零；只读判定，不改引擎状态
	const bool isProjector = TechnoExt::DeployWarp::IsProjectorType(pThis);

	if (isProjector)
	{
		Debug::Log("[DeployWarp] Mission_Unload %s: status=%d cur=%d queued=%d\n",
			pThis->GetTechnoType()->ID, pThis->MissionStatus,
			static_cast<int>(pThis->CurrentMission), static_cast<int>(pThis->QueuedMission));

		// 这条是"原版同步事件 → QueueMission(Unload) → 任务处理"的路径：各客户端都走到这里，
		// 直接本地切换即可，不能再投递事件（否则切换两次）
		if (pThis->MissionStatus == 0)
			TechnoExt::DeployWarp::Toggle(pThis);
	}

	return 0;
}

// 部署光标：0x447210 里对"非间隙发生器/非掩体/无驻军/无 Dock"的建筑会把 Action 抹成 0，
// 导致鼠标悬停自家建筑时拿不到 Action::Self_Deploy。这里为力场投影类型放行。
// 覆盖 0x44734E 的 mov ecx,[eax+0EB8h]（6 字节），两个分支都显式跳转，不依赖被覆盖字节。
DEFINE_HOOK(0x44734E, BuildingClass_GetSelfAction_DeployWarp, 0x6)
{
	GET(BuildingClass*, pThis, ESI);
	GET(TechnoTypeClass*, pType, EAX);

	if (TechnoExt::DeployWarp::IsProjectorType(pThis))
	{
		static int s_lastCursorFrame = -1;
		if (Unsorted::CurrentFrame != s_lastCursorFrame)
		{
			s_lastCursorFrame = Unsorted::CurrentFrame;
			Debug::Log("[DeployWarp] cursor %s -> Self_Deploy\n", pType ? pType->ID : "?");
		}
		return 0x4472E7;   // mov ebx, 4
	}

	// 原样：ecx = Type->Dock，随后 0x447354 的 test ecx,ecx / jnz
	R->ECX(pType ? *reinterpret_cast<const DWORD*>(reinterpret_cast<const char*>(pType) + 0xEB8) : 0);
	return 0x447354;
}

// 点击切换：0x443410（vtable +0x144）内部，switch 之前。
// 原函数只在 GapGenerator / 掩体 / 有驻军 / 有 Dock 时才下发 Mission::Unload，
// 普通建筑点击什么都不做，这里补上，使"点击部署"与间隙发生器一致。
// 覆盖 0x44341A 的 cmp eax,5 / push ebx / push esi（5 字节），此时 ECX = this、EAX = Action。
DEFINE_HOOK(0x44341A, BuildingClass_ObjectClickedAction_DeployWarp, 0x5)
{
	GET(BuildingClass*, pThis, ECX);
	GET(int, action, EAX);

	if (action == static_cast<int>(Action::Self_Deploy)
		&& TechnoExt::DeployWarp::IsProjectorType(pThis))
	{
		// 原函数自己会下发 Mission::Unload 的类型交给它，避免切换两次
		const bool original = OriginalWouldQueueUnload(pThis);

		Debug::Log("[DeployWarp] click %s: Self_Deploy, originalQueuesUnload=%d\n",
			pThis->GetTechnoType()->ID, original ? 1 : 0);

		// 点击是本机私有输入 → 投递事件；original 时走原版事件 + Mission_Unload 那条同步路径
		if (!original)
			EventExt::RaiseDeployWarpToggle(pThis);
	}

	return 0;
}
