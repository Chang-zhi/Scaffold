#include "Body.h"

#include <AnimTypeClass.h>
#include <Utilities/Stream.h>

TeamTypeExt::ExtContainer TeamTypeExt::ExtMap;

// =============================
// ini

void TeamTypeExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	auto pThis = this->OwnerObject();
	const char* pSection = pThis->ID;
	INI_EX exINI(pINI);

	// 与 [TechnoType] RadarAnim 一致：填 Animations 名，而不是动画序号
	this->RadarAnim.Read<true>(exINI, pSection, "RadarAnim");
	this->RadarAnim_ShowInShroud.Read(exINI, pSection, "RadarAnim.ShowInShroud");
	this->RadarAnim_Mode.Read(exINI, pSection, "RadarAnim.Mode");
}

// =============================
// container

TeamTypeExt::ExtContainer::ExtContainer() : Container("TeamTypeClass")
{ }

TeamTypeExt::ExtContainer::~ExtContainer() = default;

// =============================
// load / save

template <typename T>
void TeamTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->OriginalScriptTypeIndex)
		.Process(this->OriginalTaskForceIndex)
		.Process(this->RadarAnim)
		.Process(this->RadarAnim_ShowInShroud)
		.Process(this->RadarAnim_Mode)
		;
}

void TeamTypeExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<TeamTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TeamTypeExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<TeamTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}
