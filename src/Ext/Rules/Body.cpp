#include "Body.h"
#include <Windows.h>
#include <string.h>
#include <Scaffold.h>
#include <Utilities/TemplateDef.h>
#include <Utilities/Parser.h>
#include <FPSCounter.h>
#include <GameOptionsClass.h>
#include <Ext/TechnoType/Body.h>
#include <New/ChoiceBox/ChoiceBoxTypeClass.h>
#include <New/TextBox/TextBoxTypeClass.h>
#include <New/TriggerGroup/TriggerGroupClass.h>
#include <New/AttachEffect/AttachEffectTypeClass.h>
#include <Utilities/Patch.h>

std::unique_ptr<RulesExt::ExtData> RulesExt::Data = nullptr;

void RulesExt::Allocate(RulesClass* pThis)
{
	Data = std::make_unique<RulesExt::ExtData>(pThis);
}

void RulesExt::Remove(RulesClass* pThis)
{
	Data = nullptr;
}

void RulesExt::LoadFromINIFile(RulesClass* pThis, CCINIClass* pINI)
{
	Data->LoadFromINI(pINI);
}

void RulesExt::LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	TextBoxTypeClass::LoadFromINIList(pINI);
	ChoiceBoxTypeClass::LoadFromINIList(pINI);
	TriggerGroupClass::LoadFromINIList(pINI);
	AttachEffectTypeClass::LoadFromINIList(pINI);

	Data->LoadBeforeTypeData(pThis, pINI);
}

void RulesExt::LoadAfterTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	if (pINI == CCINIClass::INI_Rules)
		Data->InitializeAfterTypeData(pThis);

	Data->LoadAfterTypeData(pThis, pINI);
}

void RulesExt::ExtData::InitializeConstants()
{

}

// earliest loader - can't really do much because nothing else is initialized yet, so lookups won't work
void RulesExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	INI_EX exINI(pINI);

	this->BerzerkRestoreClearTarget.Read(exINI, GameStrings::General, "BerzerkRestoreClearTarget");
	this->Command_RecruitRange.Read(exINI, GameStrings::General, "Command.RecruitRange");
}

void RulesExt::ExtData::LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	INI_EX exINI(pINI);

	this->ShowTextBoxInShroud_Waypoint.Read(exINI, GameStrings::General, "ShowTextBoxInShroud.Waypoint");
	this->ShowTextBoxInShroud_Techno.Read(exINI, GameStrings::General, "ShowTextBoxInShroud.Techno");

}

// this should load everything that TypeData is not dependant on
// i.e. InfantryElectrocuted= can go here since nothing refers to it
// but [GenericPrerequisites] have to go earlier because they're used in parsing TypeData
void RulesExt::ExtData::LoadAfterTypeData(RulesClass* pThis, CCINIClass* pINI)
{
	INI_EX exINI(pINI);

	if (pINI->GetSection("AITargetTypes"))
	{
		std::vector<std::vector<TechnoTypeClass*>> lists;

		int itemsCount = pINI->GetKeyCount("AITargetTypes");
		for (int i = 0; i < itemsCount; ++i)
		{
			std::vector<TechnoTypeClass*> objectsList;
			char* context = nullptr;
			pINI->ReadString("AITargetTypes", pINI->GetKeyName("AITargetTypes", i), "", Scaffold::readBuffer);

			for (char* cur = strtok_s(Scaffold::readBuffer, Scaffold::readDelims, &context); cur; cur = strtok_s(nullptr, Scaffold::readDelims, &context))
			{
				TechnoTypeClass* buffer;
				if (Parser<TechnoTypeClass*>::TryParse(cur, &buffer))
					objectsList.emplace_back(buffer);
				else
					Debug::Log("[Developer warning] AITargetTypes (Count: %d): Error parsing [%s]\n", lists.size(), cur);
			}

			lists.emplace_back(std::move(objectsList));
		}

		this->AITargetTypesLists = std::move(lists);
		Debug::Log("[Scaffold] AITargetTypes: parsed %d lists\n", static_cast<int>(this->AITargetTypesLists.size()));
	}

}

// this runs between the before and after type data loading methods for rules ini
void RulesExt::ExtData::InitializeAfterTypeData(RulesClass* const pThis)
{

}

void RulesExt::ExtData::InitializeAfterAllLoaded()
{

}

// =============================
// load / save

template <typename T>
void RulesExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->AITargetTypesLists)
		.Process(this->ShowTextBoxInShroud_Waypoint)
		.Process(this->ShowTextBoxInShroud_Techno)
		.Process(this->BerzerkRestoreClearTarget)
		.Process(this->Command_RecruitRange)
		;
}

void RulesExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<RulesClass>::LoadFromStream(Stm);
	this->Serialize(Stm);

	this->ReplaceVoxelLightSources();
}

void RulesExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<RulesClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

void RulesExt::ExtData::ReplaceVoxelLightSources()
{

}

// =============================
// container hooks

DEFINE_HOOK(0x667A1D, RulesClass_CTOR, 0x5)
{
	GET(RulesClass*, pItem, ESI);

	RulesExt::Allocate(pItem);

	return 0;
}

DEFINE_HOOK(0x667A30, RulesClass_DTOR, 0x5)
{
	GET(RulesClass*, pItem, ECX);

	RulesExt::Remove(pItem);

	return 0;
}

IStream* RulesExt::g_pStm = nullptr;

DEFINE_HOOK_AGAIN(0x674730, RulesClass_SaveLoad_Prefix, 0x6)
DEFINE_HOOK(0x675210, RulesClass_SaveLoad_Prefix, 0x5)
{
	//GET(RulesClass*, pItem, ECX);
	GET_STACK(IStream*, pStm, 0x4);

	RulesExt::g_pStm = pStm;

	return 0;
}

DEFINE_HOOK(0x678841, RulesClass_Load_Suffix, 0x7)
{
	auto buffer = RulesExt::Global();

	ScaffoldByteStream Stm(0);
	if (Stm.ReadBlockFromStream(RulesExt::g_pStm))
	{
		ScaffoldStreamReader Reader(Stm);

		if (Reader.Expect(RulesExt::Canary) && Reader.RegisterChange(buffer))
			buffer->LoadFromStream(Reader);
	}

	return 0;
}

DEFINE_HOOK(0x675205, RulesClass_Save_Suffix, 0x8)
{
	auto buffer = RulesExt::Global();
	ScaffoldByteStream saver(sizeof(*buffer));
	ScaffoldStreamWriter writer(saver);

	writer.Expect(RulesExt::Canary);
	writer.RegisterChange(buffer);

	buffer->SaveToStream(writer);
	saver.WriteBlockToStream(RulesExt::g_pStm);

	return 0;
}

// DEFINE_HOOK(0x52D149, InitRules_PostInit, 0x5)
// {
// 	LaserTrailTypeClass::LoadFromINIList(&CCINIClass::INI_Art.get());
// 	return 0;
// }

DEFINE_HOOK(0x668BF0, RulesClass_Addition, 0x5)
{
	GET(RulesClass*, pItem, ECX);
	GET_STACK(CCINIClass*, pINI, 0x4);

	//	RulesClass::Initialized = false;
	RulesExt::LoadFromINIFile(pItem, pINI);

	return 0;
}

DEFINE_HOOK(0x679A15, RulesData_LoadBeforeTypeData, 0x6)
{
	GET(RulesClass*, pItem, ECX);
	GET_STACK(CCINIClass*, pINI, 0x4);

	//	RulesClass::Initialized = true;
	RulesExt::LoadBeforeTypeData(pItem, pINI);

	return 0;
}

DEFINE_HOOK(0x679CAF, RulesData_LoadAfterTypeData, 0x5)
{
	RulesClass* pItem = RulesClass::Instance;
	GET(CCINIClass*, pINI, ESI);

	RulesExt::LoadAfterTypeData(pItem, pINI);

	return 0;
}

DEFINE_HOOK(0x668F6A, RulesData_InitializeAfterAllLoaded, 0x5)
{
	RulesExt::Global()->InitializeAfterAllLoaded();
	return 0;
}

// Reenable obsolete [JumpjetControls] in RA2/YR
// Author: Uranusian
DEFINE_HOOK(0x7115AE, TechnoTypeClass_CTOR_JumpjetControls, 0xA)
{
	return 0;
}

DEFINE_HOOK(0x6744E4, RulesClass_ReadJumpjetControls_Extra, 0x7)
{
	return 0;
}

// skip vanilla JumpjetControls and make it earlier load
// DEFINE_JUMP(LJMP, 0x668EB5, 0x668EBD); // RulesClass_Process_SkipJumpjetControls // Really necessary? won't hurt to read again

DEFINE_HOOK(0x48C8B0, ScreenStateMachine_AllowTabBriefing, 0x5)
{
	if (!Scaffold::Config::AllowTabBriefingInSinglePlayer)
		return 0;

	int* const pCurrentUI = reinterpret_cast<int*>(0xA8EDA0);
	if (*pCurrentUI != 0)
		return 0;

	if (*reinterpret_cast<int*>(0xA8B238) != 0)
		return 0;

	static bool lastDown = false;
	const bool down = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
	const bool edge = down && !lastDown;
	lastDown = down;
	if (!edge)
		return 0;

	*pCurrentUI = 9;
	return 0;
}
