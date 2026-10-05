#pragma once

#include <CCINIClass.h>
#include <RulesClass.h>
#include <MissionClass.h>
#include <Utilities/Container.h>
#include <Utilities/Constructs.h>
#include <Utilities/Template.h>
#include <Utilities/Enum.h>
#include <Utilities/TemplateDef.h>
#include <Utilities/Debug.h>
#include <Utilities/Anchor.h>

#include <vector>

class AnimTypeClass;
class MouseCursor;
class SuperWeaponTypeClass;
class TechnoTypeClass;
class VocClass;
class WarheadTypeClass;
class DigitalDisplayTypeClass;
class SelectBoxTypeClass;

class RulesExt
{
public:
	using base_type = RulesClass;

	static constexpr DWORD Canary = 0xAAAA1111;

	class ExtData final : public Extension<RulesClass>
	{
	public:
		// Text Box
		Valueable<bool> ShowTextBoxInShroud_Waypoint;
		Valueable<bool> ShowTextBoxInShroud_Techno;

		Valueable<bool> BerzerkRestoreClearTarget;
		Valueable<double> Command_RecruitRange;

		std::vector<std::vector<TechnoTypeClass*>> AITargetTypesLists;

		ExtData(RulesClass* OwnerObject) : Extension<RulesClass>(OwnerObject)
			, ShowTextBoxInShroud_Waypoint { true }
			, ShowTextBoxInShroud_Techno { true }
			, BerzerkRestoreClearTarget { false }
			, Command_RecruitRange { 10 }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI);
		virtual void LoadAfterTypeData(RulesClass* pThis, CCINIClass* pINI);
		virtual void InitializeConstants() override;
		void InitializeAfterTypeData(RulesClass* pThis);
		void InitializeAfterAllLoaded();

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

		void ReplaceVoxelLightSources();

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

private:
	static std::unique_ptr<ExtData> Data;

public:
	static IStream* g_pStm;

	static void Allocate(RulesClass* pThis);
	static void Remove(RulesClass* pThis);

	static void LoadFromINIFile(RulesClass* pThis, CCINIClass* pINI);
	static void LoadBeforeTypeData(RulesClass* pThis, CCINIClass* pINI);
	static void LoadAfterTypeData(RulesClass* pThis, CCINIClass* pINI);

	static ExtData* Global()
	{
		return Data.get();
	}

	static void Clear()
	{
		Allocate(RulesClass::Instance);
	}

	static void PointerGotInvalid(void* ptr, bool removed)
	{
		Global()->InvalidatePointer(ptr, removed);
	}
};
