#include <TechnoClass.h>
#include <Fundamentals.h>

#include <Ext/Techno/Body.h>
#include <Ext/WarheadType/Body.h>
#include <Utilities/Debug.h>
#include <Utilities/SyncState.h>

#include <map>

// 定义独占映射
std::unordered_map<TechnoClass*, TechnoClass*> TechnoExt::TemporalExclusive::TargetsMap;

void TechnoExt::TemporalExclusive::InvalidatePtr(void* ptr)
{
	SyncState::InvalidatePointer(TargetsMap, ptr);
}

// Check if a unit has an exclusive temporal weapon
bool TechnoExt::TemporalExclusive::IsCurrentUseExclusiveTemporalWeapon(TechnoClass* pTechno)
{
    if (!pTechno) return false;

    // 使用 GetCurrentWeapon 获取当前正在使用的武器
    WeaponTypeClass* pWeapon = TechnoExt::GetCurrentWeapon(pTechno);

    if (pWeapon && pWeapon->Warhead)
    {
        if (pWeapon->Warhead->Temporal)
        {
            auto pWHExt = WarheadTypeExt::ExtMap.Find(pWeapon->Warhead);
            if (pWHExt && pWHExt->Temporal_Exclusive)
            {
                return true;
            }
        }
    }

    return false;
}

// 辅助函数：清理映射中所有无效的占用记录
void TechnoExt::TemporalExclusive::CleanupInvalidTemporalLocks()
{
	// Never inspect a cached pointer until it is present in the live array.
	std::unordered_set<TechnoClass*> live;
	live.reserve(TechnoClass::Array.Count);
	for (auto pTechno : TechnoClass::Array)
		if (pTechno)
			live.insert(pTechno);

    for (auto it = TargetsMap.begin(); it != TargetsMap.end(); )
    {
        TechnoClass* pTarget = it->first;
        TechnoClass* pAttacker = it->second;

        bool isValid = true;

        // 1. 检查目标是否还存在且存活
        if (!live.contains(pTarget) || pTarget->Health <= 0 || pTarget->InLimbo)
        {
            isValid = false;
        }
        // 2. 检查攻击者是否还存在、存活、且在地图上（未进入运输工具/未死亡）
        else if (!live.contains(pAttacker) || pAttacker->Health <= 0 || pAttacker->InLimbo)
        {
            isValid = false;
        }
        // 3. 检查攻击者是否仍然锁定着这个目标
        // 如果攻击者的 Target 变了，或者为空，说明它已经放弃了，我们应该释放占用
        else if (abstract_cast<TechnoClass*>(pAttacker->Target) != pTarget)
        {
            isValid = false;
        }
		// 4. 检查攻击者是否仍然拥有"独占超时空"武器
        // 如果它切换了武器，或者被卸下了武器，应该释放占用
        else if (!IsCurrentUseExclusiveTemporalWeapon(pAttacker))
        {
            isValid = false;
        }

        if (!isValid)
        {
            it = TargetsMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// 处理互斥超时空武器的目标独占逻辑
void TechnoExt::TemporalExclusive::HandleTemporalExclusiveTargeting(TechnoClass* pThis)
{

	TechnoClass* pCurrentTarget = abstract_cast<TechnoClass*>(pThis->Target);
	if (!pCurrentTarget)
		return;

	// 当前武器只解析一次，AOE 拦截与独占仲裁共用（原本会解析两次）
	WeaponTypeClass* pWeapon = TechnoExt::GetCurrentWeapon(pThis);
	if (!pWeapon || !pWeapon->Warhead || !pWeapon->Warhead->Temporal)
		return;

	// AOE 主/副目标拦截：所有超时空武器都不能选择已被其他 AOE 锁定的目标
	{
		// 检查是否是其他 AOE 的副目标
		auto claimIt = TemporalAOE::SecondaryClaims.find(pCurrentTarget);
		if (claimIt != TemporalAOE::SecondaryClaims.end() && claimIt->second != pThis)
		{
			pThis->SetTarget(nullptr);
			return;
		}

		// 检查是否是其他 AOE 的主目标（其他 CLEG 正在攻击的目标）
		auto mainIt = TemporalAOE::CachedMainOwners.find(pCurrentTarget);
		if (mainIt != TemporalAOE::CachedMainOwners.end() && mainIt->second != pThis)
		{
			pThis->SetTarget(nullptr);
			return;
		}
	}

	// 非互斥超时空不参与独占仲裁
	auto const pWHExt = WarheadTypeExt::ExtMap.Find(pWeapon->Warhead);
	if (!pWHExt || !pWHExt->Temporal_Exclusive)
		return;

	auto it = TargetsMap.find(pCurrentTarget);

	// 目标未被任何人占用 -> 声明占用
	if (it == TargetsMap.end())
	{
		TargetsMap[pCurrentTarget] = pThis;
		return;
	}

	TechnoClass* pOccupier = it->second;

	// 占用者是我自己 -> 保持现状，无需操作
	if (pOccupier == pThis)
		return;

	// 双重检查：确保占用者真的还在攻击这个目标（防止竞态条件）
	if (abstract_cast<TechnoClass*>(pOccupier->Target) == pCurrentTarget &&
		IsCurrentUseExclusiveTemporalWeapon(pOccupier))
	{
		// 确实发生冲突，强制当前单位放弃
		pThis->SetTarget(nullptr);
	}
	else
	{
		// 占用者其实已经无效了（虽然 Cleanup 跑过了，但可能刚好在这一帧变化）
		// 我们抢占这个目标
		TargetsMap[pCurrentTarget] = pThis;
	}
}

// 更新新互斥超时空武器的独占逻辑
// 如果TemporalExclusive的实例已经多对一攻击, 保留一个实例, 其他全部放弃
void TechnoExt::TemporalExclusive::UpdateTemporalExclusive()
{
	auto& array = TemporalClass::Array;
	int count = array.Count;

	if (count <= 1) return;

	std::unordered_map<TechnoClass*, TemporalClass*> lockedTargets;
	std::vector<TemporalClass*> toRelease;

	for (int i = 0; i < count; ++i)
	{
		TemporalClass* pCurrent = array.Items[i];

		if (!pCurrent || !pCurrent->Target) continue;

		// 非互斥超时空（原版 CLEG、超武 Temporal 等）不参与仲裁，
		// 必须 continue 跳过，不能 return —— 否则任意一个都会让整轮去重失效
		if (!IsCurrentUseExclusiveTemporalWeapon(pCurrent->Owner))
		{
			continue;
		}

		TechnoClass* pTarget = pCurrent->Target;

		auto it = lockedTargets.find(pTarget);
		if (it == lockedTargets.end())
		{
			lockedTargets[pTarget] = pCurrent;
		}
		else
		{
			toRelease.push_back(pCurrent);
		}
	}

	for (auto pInst : toRelease)
	{
		if (!pInst || !pInst->Target)
			continue;

		if (pInst->Target->TemporalTargetingMe == pInst && pInst->NextTemporal)
			pInst->Target->TemporalTargetingMe = pInst->NextTemporal;

		pInst->LetGo();
	}
}
