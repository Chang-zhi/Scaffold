#include "Body.h"

#include <AircraftClass.h>
#include <HouseClass.h>
#include <ScenarioClass.h>
#include <JumpjetLocomotionClass.h>
#include <MapClass.h>
#include <CellClass.h>
#include <WeaponTypeClass.h>
#include <TechnoTypeClass.h>

#include <Utilities/AresFunctions.h>
#include <TemporalClass.h>

TechnoExt::ExtContainer TechnoExt::ExtMap;

void TechnoExt::PointerGotInvalid(void* ptr, bool removed)
{
	if (!ptr)
		return;
	TemporalExclusive::InvalidatePtr(ptr);
	ExtMap.PointerGotInvalid(ptr, removed);
}

TechnoExt::ExtData::~ExtData()
{
}

// =============================
// effects

void TechnoExt::ExtData::UpdateEffects()
{
	// 更新所有效果
	for (auto& pEffect : this->Effects)
	{
		if (pEffect)
			pEffect->OnUpdate();
	}

	// 清理已死亡的效果
	for (auto it = this->Effects.begin(); it != this->Effects.end(); )
	{
		if (!(*it) || !(*it)->IsAlive())
			it = this->Effects.erase(it);
		else
			++it;
	}
}

// =============================
// load / save

template <typename T>
void TechnoExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->TypeExtData)
		.Process(this->AOEState.Active)
		.Process(this->AOEState.CellSpread)
		.Process(this->AOEState.SecondaryWeight)
		.Process(this->AOEState.WeaponDamage)
		.Process(this->AOEState.ExtraWarpAdded)
		.Process(this->AOEState.WarpTimer)
		.Process(this->AOEState.CachedMainDead)
		.Process(this->AOEState.WarpingOut)
		.Process(this->AOEState.ScanInterval)
		.Process(this->AOEState.ScanCounter)
		.Process(this->AOEState.CachedMain)
		.Process(this->AOEState.TargetsInRange)
		.Process(this->WarpField.Active)
		.Process(this->WarpField.ScanCounter)
		.Process(this->WarpField.TargetsInField)
		;

	if constexpr (std::is_same_v<T, ScaffoldStreamReader>)
	{
		Stm.Process(LoadedBuildingsDisabled).Process(LoadedContributedTargets);
	}
	else
	{
		std::vector<TechnoClass*> disabled;
		std::vector<TechnoClass*> contributed;
		for (auto pTechno : TechnoClass::Array)
		{
			if (AOEState.BuildingsDisabled.contains(pTechno))
				disabled.push_back(pTechno);
			if (AOEState.ContributedTargets.contains(pTechno))
				contributed.push_back(pTechno);
		}
		Stm.Process(disabled).Process(contributed);
	}
}

void TechnoExt::ExtData::CompleteLoad()
{
	AOEState.BuildingsDisabled.clear();
	AOEState.ContributedTargets.clear();
	for (auto target : LoadedBuildingsDisabled)
		if (target)
			AOEState.BuildingsDisabled.insert(target);
	for (auto target : LoadedContributedTargets)
		if (target)
			AOEState.ContributedTargets.insert(target);
	LoadedBuildingsDisabled.clear();
	LoadedContributedTargets.clear();
}

void TechnoExt::ExtData::InvalidatePointer(void* ptr, bool bRemoved)
{
	if (!ptr) return;
	auto& state = this->AOEState;

	// The state machine retains the destruction flag, not a dangling address.
	if (state.CachedMain == ptr)
	{
		state.CachedMainDead = true;
		state.CachedMain = nullptr;
	}

	// 副目标列表（由 InvalidateAOESecondaryClaims 统一处理指针失效）
	for (auto it = state.TargetsInRange.begin(); it != state.TargetsInRange.end(); )
	{
		if (*it == ptr)
			it = state.TargetsInRange.erase(it);
		else
			++it;
	}

	// 建筑禁用列表（遍历删除）
	for (auto it = state.BuildingsDisabled.begin(); it != state.BuildingsDisabled.end(); )
	{
		if (*it == ptr)
			it = state.BuildingsDisabled.erase(it);
		else
			++it;
	}

	// 已贡献副目标列表（指针失效时移除）
	state.ContributedTargets.erase(static_cast<TechnoClass*>(ptr));

	auto& warpField = this->WarpField;
	for (auto it = warpField.TargetsInField.begin(); it != warpField.TargetsInField.end(); )
	{
		if (*it == ptr)
			it = warpField.TargetsInField.erase(it);
		else
			++it;
	}
}

void TechnoExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<TechnoClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TechnoExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<TechnoClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// =============================
// 目标是否落在本单位可抵达的移动区域
// zoneScanType 由 TechnoTypeExt::TargetZoneScanType 提供

bool TechnoExt::AllowedTargetByZone(TechnoClass* pThis, TechnoClass* pTarget, TargetZoneScanType zoneScanType, WeaponTypeClass* pWeapon, bool useZone, int zone)
{
	if (!pThis || !pTarget)
		return false;

	if (pThis->WhatAmI() == AbstractType::Aircraft)
		return true;

	auto const pType = pThis->GetTechnoType();
	auto const mZone = pType->MovementZone;
	const int currentZone = useZone ? zone : MapClass::Instance.GetMovementZoneType(pThis->GetMapCoords(), mZone, pThis->OnBridge);

	if (currentZone != -1)
	{
		if (zoneScanType == TargetZoneScanType::Any)
			return true;

		const int targetZone = MapClass::Instance.GetMovementZoneType(pTarget->GetMapCoords(), mZone, pTarget->OnBridge);

		if (zoneScanType == TargetZoneScanType::Same)
		{
			if (currentZone != targetZone)
				return false;
		}
		else
		{
			if (currentZone == targetZone)
				return true;

			auto const speedType = pType->SpeedType;
			auto const cellStruct = MapClass::Instance.NearByLocation(CellClass::Coord2Cell(pTarget->Location),
				speedType, -1, mZone, false, 1, 1, true,
				false, false, speedType != SpeedType::Float, CellStruct::Empty, false, false);

			if (cellStruct == CellStruct::Empty)
				return false;

			auto const pCell = MapClass::Instance.TryGetCellAt(cellStruct);

			if (!pCell)
				return false;

			if (!pWeapon)
			{
				const int weaponIndex = pThis->SelectWeapon(pTarget);

				if (weaponIndex < 0)
					return false;

				const auto pWeaponStruct = pThis->GetWeapon(weaponIndex);

				if (!pWeaponStruct)
					return false;

				pWeapon = pWeaponStruct->WeaponType;

				if (!pWeapon)
					return false;
			}

			const double distanceSq = pCell->GetCoordsWithBridge().DistanceFromSquared(pTarget->GetCenterCoords());
			const double range = (double)pWeapon->Range;

			if (distanceSq > range * range)
				return false;
		}
	}

	return true;
}

// =============================
// container

TechnoExt::ExtContainer::ExtContainer() : Container("TechnoClass") { }

TechnoExt::ExtContainer::~ExtContainer() = default;


// =============================
// container hooks

DEFINE_HOOK(0x6F3260, TechnoClass_CTOR, 0x5)
{
	GET(TechnoClass*, pItem, ESI);

	TechnoExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x6F4500, TechnoClass_DTOR, 0x5)
{
	GET(TechnoClass*, pItem, ECX);

	TechnoExt::TemporalExclusive::InvalidatePtr(pItem);
	// Destruction may already have changed the object's runtime type to Foot
	// or Techno, so do not let the usual derived-type filter skip this notice.
	TechnoExt::ExtMap.InvalidateTechno(pItem);
	TechnoExt::TemporalAOE::InvalidatePtr(pItem);
	TechnoExt::DeployWarp::InvalidatePtr(pItem);
	TechnoExt::BerzerkRestorePointerInvalidate(pItem);
	// 清理 CachedMainOwners 中指向已销毁对象的条目
	for (auto it = TechnoExt::TemporalAOE::CachedMainOwners.begin(); it != TechnoExt::TemporalAOE::CachedMainOwners.end(); )
	{
		if (it->first == pItem || it->second == pItem)
			it = TechnoExt::TemporalAOE::CachedMainOwners.erase(it);
		else
			++it;
	}
	TechnoExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x70C250, TechnoClass_SaveLoad_Prefix, 0x8)
DEFINE_HOOK(0x70BF50, TechnoClass_SaveLoad_Prefix, 0x5)
{
	GET_STACK(TechnoClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	TechnoExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x70C249, TechnoClass_Load_Suffix, 0x5)
{
	TechnoExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x70C264, TechnoClass_Save_Suffix, 0x5)
{
	TechnoExt::ExtMap.SaveStatic();

	return 0;
}
