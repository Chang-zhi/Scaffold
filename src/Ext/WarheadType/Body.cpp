#include "Body.h"

#include <BulletClass.h>
#include <HouseClass.h>

#include <Ext/Techno/Body.h>
#include <Utilities/EnumFunctions.h>

WarheadTypeExt::ExtContainer WarheadTypeExt::ExtMap;

// =============================
// load / save

void WarheadTypeExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	auto pThis = this->OwnerObject();
	const char* pSection = pThis->ID;
	INI_EX exINI(pINI);

	this->Temporal_Exclusive.Read(exINI, pSection, "Temporal.Exclusive");

	// Temporal AOE
	this->TemporalAOE_Enable.Read(exINI, pSection, "TemporalAOE.Enable");
	this->TemporalAOE_CellSpread.Read(exINI, pSection, "TemporalAOE.CellSpread");
	this->TemporalAOE_SecondaryWeight.Read(exINI, pSection, "TemporalAOE.SecondaryWeight");
	this->TemporalAOE_AffectsAllies.Read(exINI, pSection, "TemporalAOE.AffectsAllies");
	this->TemporalAOE_AffectsAirborne.Read(exINI, pSection, "TemporalAOE.AffectsAirborne");
	this->TemporalAOE_CenterOnAttacker.Read(exINI, pSection, "TemporalAOE.CenterOnAttacker");

	// BerserkReduce
	this->BerserkReduce.Read(exINI, pSection, "BerserkReduce");

}

template <typename T>
void WarheadTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->Temporal_Exclusive)
		.Process(this->TemporalAOE_Enable)
		.Process(this->TemporalAOE_CellSpread)
		.Process(this->TemporalAOE_SecondaryWeight)
		.Process(this->TemporalAOE_AffectsAllies)
		.Process(this->TemporalAOE_AffectsAirborne)
		.Process(this->TemporalAOE_CenterOnAttacker)
		.Process(this->BerserkReduce)
		;
}

void WarheadTypeExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<WarheadTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void WarheadTypeExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<WarheadTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

bool WarheadTypeExt::LoadGlobals(ScaffoldStreamReader& Stm)
{
	return Stm.Success();
}

bool WarheadTypeExt::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	return Stm.Success();
}

// =============================
// container

WarheadTypeExt::ExtContainer::ExtContainer() : Container("WarheadTypeClass") { }

WarheadTypeExt::ExtContainer::~ExtContainer() = default;

// =============================
// container hooks

DEFINE_HOOK(0x75D1A9, WarheadTypeClass_CTOR, 0x7)
{
	GET(WarheadTypeClass*, pItem, EBP);

	WarheadTypeExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x75E5C8, WarheadTypeClass_SDDTOR, 0x6)
{
	GET(WarheadTypeClass*, pItem, ESI);

	WarheadTypeExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x75E2C0, WarheadTypeClass_SaveLoad_Prefix, 0x5)
DEFINE_HOOK(0x75E0C0, WarheadTypeClass_SaveLoad_Prefix, 0x8)
{
	GET_STACK(WarheadTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	WarheadTypeExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x75E2AE, WarheadTypeClass_Load_Suffix, 0x7)
{
	WarheadTypeExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x75E39C, WarheadTypeClass_Save_Suffix, 0x5)
{
	WarheadTypeExt::ExtMap.SaveStatic();

	return 0;
}

//DEFINE_HOOK_AGAIN(0x75DEAF, WarheadTypeClass_LoadFromINI, 0x5)// Section dont exist!
DEFINE_HOOK(0x75DEA0, WarheadTypeClass_LoadFromINI, 0x5)
{
	GET(WarheadTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0x150);

	WarheadTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}
