#include "Body.h"

#include <AircraftTrackerClass.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>
#include <FlyLocomotionClass.h>
#include <JumpjetLocomotionClass.h>
#include <TechnoTypeClass.h>
#include <StringTable.h>

#include <Utilities/Debug.h>
#include <Utilities/GeneralUtils.h>

TechnoTypeExt::ExtContainer TechnoTypeExt::ExtMap;

//TODO: YRpp this with proper casting
TechnoTypeClass* TechnoTypeExt::GetTechnoType(ObjectTypeClass* pType)
{
	enum class IUnknownVtbl : DWORD
	{
		AircraftType = 0x7E2868,
		BuildingType = 0x7E4570,
		InfantryType = 0x7EB610,
		UnitType = 0x7F6218,
	};
	if (!pType)
		return nullptr;

	auto const vtThis = static_cast<IUnknownVtbl>(VTable::Get(pType));
	if (vtThis == IUnknownVtbl::InfantryType
		|| vtThis == IUnknownVtbl::UnitType
		|| vtThis == IUnknownVtbl::AircraftType
		|| vtThis == IUnknownVtbl::BuildingType)
	{
		return static_cast<TechnoTypeClass*>(pType);
	}

	return nullptr;
}

// used for more WeaponX added by Ares.
WeaponTypeClass* TechnoTypeExt::GetWeaponType(TechnoTypeClass* pThis, int weaponIndex, bool isElite)
{
	if (isElite)
	{
		if (const auto pEliteStruct = pThis->GetEliteWeapon(weaponIndex))
		{
			if (const auto pEliteWeapon = pEliteStruct->WeaponType)
				return pEliteWeapon;
		}
	}

	const auto pWeaponStruct = pThis->GetWeapon(weaponIndex);

	return pWeaponStruct ? pWeaponStruct->WeaponType : nullptr;
}

// =============================
// load / save

void TechnoTypeExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	auto pThis = this->OwnerObject();
	const char* pSection = pThis->ID;
	INI_EX exINI(pINI);

	this->AutoHunt.Read(exINI, pSection, "AutoHunt");
	this->LegalTargetWhenAIOwner.Read(exINI, pSection, "LegalTargetWhenAIOwner");
	this->PassengersJoinTeam.Read(exINI, pSection, "PassengersJoinTeam");

	this->TargetZoneScanType.Read(exINI, pSection, "TargetZoneScanType");
	this->RadarJamRadius.Read(exINI, pSection, "RadarJamRadius");
	this->InhibitorRange.Read(exINI, pSection, "InhibitorRange");

	// SmartVHPScan
	this->SmartVHPScan.Read(exINI, pSection, "SmartVHPScan");
	this->SmartVHPScan_Count.Read(exINI, pSection, "SmartVHPScan.Count");
	this->SmartVHPScan_Damage.Read(exINI, pSection, "SmartVHPScan.Damage");
	this->SmartVHPScan_Bias.Read(exINI, pSection, "SmartVHPScan.Bias");
	this->SmartVHPScan_UnknownFactor.Read(exINI, pSection, "SmartVHPScan.UnknownFactor");
	this->SmartVHPScan_ExcludeFraction.Read(exINI, pSection, "SmartVHPScan.ExcludeFraction");

	// 中央调度
	this->SmartVHPScan_Overflow.Read(exINI, pSection, "SmartVHPScan.Overflow");
	this->SmartVHPScan_SwitchThreshold.Read(exINI, pSection, "SmartVHPScan.SwitchThreshold");
	this->SmartVHPScan_IncludeInflight.Read(exINI, pSection, "SmartVHPScan.IncludeInflight");
	this->SmartVHPScan_CountIdleFrames.Read(exINI, pSection, "SmartVHPScan.CountIdleFrames");

	// 部署超时空力场
	this->DeployWarp_Enable.Read(exINI, pSection, "DeployWarp.Enable");
	this->DeployWarp_Range.Read(exINI, pSection, "DeployWarp.Range");
	this->DeployWarp_AffectsEnemies.Read(exINI, pSection, "DeployWarp.AffectsEnemies");
	this->DeployWarp_AffectsAllies.Read(exINI, pSection, "DeployWarp.AffectsAllies");
	this->DeployWarp_AffectsAirborne.Read(exINI, pSection, "DeployWarp.AffectsAirborne");

	// 小地图标记。
	this->RadarAnim.Read<true>(exINI, pSection, "RadarAnim");
	this->RadarAnim_ShowInShroud.Read(exINI, pSection, "RadarAnim.ShowInShroud");

	if (this->DeployWarp_Enable)
	{
		Debug::Log("[DeployWarp] type %s enabled: Range=%.2f Allies=%d Enemies=%d Airborne=%d\n",
			pSection, (double)this->DeployWarp_Range,
			(int)this->DeployWarp_AffectsAllies, (int)this->DeployWarp_AffectsEnemies,
			(int)this->DeployWarp_AffectsAirborne);
	}
}

void TechnoTypeExt::ExtData::LoadFromINIByWhatAmI(INI_EX& exINI, const char* pSection, INI_EX& exArtINI, const char* pArtSection)
{
	AbstractType abs = this->OwnerObject()->WhatAmI();

	switch (abs)
	{
	case AbstractType::UnitType:
	{
		break;
	}
	default:
		break;
	}
}

template <typename T>
void TechnoTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->AutoHunt)
		.Process(this->LegalTargetWhenAIOwner)
		.Process(this->PassengersJoinTeam)
		.Process(this->TargetZoneScanType)
		.Process(this->RadarJamRadius)
		.Process(this->InhibitorRange)
		.Process(this->SmartVHPScan)
		.Process(this->SmartVHPScan_Count)
		.Process(this->SmartVHPScan_Damage)
		.Process(this->SmartVHPScan_Bias)
		.Process(this->SmartVHPScan_UnknownFactor)
		.Process(this->SmartVHPScan_ExcludeFraction)
		.Process(this->SmartVHPScan_Overflow)
		.Process(this->SmartVHPScan_SwitchThreshold)
		.Process(this->SmartVHPScan_IncludeInflight)
		.Process(this->SmartVHPScan_CountIdleFrames)
		.Process(this->DeployWarp_Enable)
		.Process(this->DeployWarp_Range)
		.Process(this->DeployWarp_AffectsEnemies)
		.Process(this->DeployWarp_AffectsAllies)
		.Process(this->DeployWarp_AffectsAirborne)
		.Process(this->RadarAnim)
		.Process(this->RadarAnim_ShowInShroud)
		;
}
void TechnoTypeExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<TechnoTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TechnoTypeExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<TechnoTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// =============================
// container

TechnoTypeExt::ExtContainer::ExtContainer() : Container("TechnoTypeClass") { }
TechnoTypeExt::ExtContainer::~ExtContainer() = default;

// =============================
// container hooks

DEFINE_HOOK(0x711835, TechnoTypeClass_CTOR, 0x5)
{
	GET(TechnoTypeClass*, pItem, ESI);

	TechnoTypeExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x711AE0, TechnoTypeClass_DTOR, 0x5)
{
	GET(TechnoTypeClass*, pItem, ECX);

	TechnoTypeExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x716DC0, TechnoTypeClass_SaveLoad_Prefix, 0x5)
DEFINE_HOOK(0x7162F0, TechnoTypeClass_SaveLoad_Prefix, 0x6)
{
	GET_STACK(TechnoTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	TechnoTypeExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x716DAC, TechnoTypeClass_Load_Suffix, 0xA)
{
	TechnoTypeExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x717094, TechnoTypeClass_Save_Suffix, 0x5)
{
	TechnoTypeExt::ExtMap.SaveStatic();

	return 0;
}

//DEFINE_HOOK_AGAIN(0x716132, TechnoTypeClass_LoadFromINI, 0x5)// Section dont exist!
DEFINE_HOOK(0x716123, TechnoTypeClass_LoadFromINI, 0x5)
{
	GET(TechnoTypeClass*, pItem, EBP);
	GET_STACK(CCINIClass*, pINI, 0x380);

	TechnoTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}

#if ANYONE_ACTUALLY_USE_THIS
DEFINE_HOOK(0x679CAF, RulesClass_LoadAfterTypeData_CompleteInitialization, 0x5)
{
	//GET(CCINIClass*, pINI, ESI);

	for (auto const& [pType, pExt] : BuildingTypeExt::ExtMap)
	{
		pExt->CompleteInitialization();
	}

	return 0;
}
#endif

//DEFINE_HOOK(0x747E90, UnitTypeClass_LoadFromINI, 0x5)
//{
//	GET(UnitTypeClass*, pItem, ESI);
//	return 0;
//}
