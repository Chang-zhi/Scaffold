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
		Valueable<bool> LocomotorRestoreTask;
		Valueable<double> Command_RecruitRange;

		Valueable<bool> AllowRepairAlliedBuildings;
		Valueable<bool> AllowPlanningAfterTerminalCommand;
		Valueable<bool> SelectionQueue_Extend;
		Valueable<int> SelectionLimit;

		Valueable<bool> Passengers_ImprovedQueue;
		Valueable<bool> Passengers_NoQueueEnter;
		Valueable<bool> Passengers_NoQueueUnload;
		Valueable<int> Passengers_NoQueueEnter_MaxCount;
		Valueable<int> Passengers_NoQueueUnload_Delay;

		// [General] 可收起建筑（建筑 ⇄ 载具）的基地节点按所属方科技类型数量判定
		Valueable<bool> BaseNodeUseDeployableCount;

		// FullMap 战术地图
		Valueable<bool> FullMap_Enabled;
		Valueable<int> FullMap_MaxMarks;

		std::vector<std::vector<TechnoTypeClass*>> AITargetTypesLists;

		ExtData(RulesClass* OwnerObject) : Extension<RulesClass>(OwnerObject)
			, ShowTextBoxInShroud_Waypoint { true }
			, ShowTextBoxInShroud_Techno { true }
			, BerzerkRestoreClearTarget { false }
			, LocomotorRestoreTask { false }
			, Command_RecruitRange { 10 }
			, AllowRepairAlliedBuildings { true }
			, AllowPlanningAfterTerminalCommand { false }
			, SelectionQueue_Extend { false }
			, SelectionLimit { 0 }
			, Passengers_ImprovedQueue { false }
			, Passengers_NoQueueEnter { false }
			, Passengers_NoQueueUnload { false }
			, Passengers_NoQueueEnter_MaxCount { 0 }
			, Passengers_NoQueueUnload_Delay { 0 }
			, BaseNodeUseDeployableCount { false }
			, FullMap_Enabled { true }
			, FullMap_MaxMarks { 200 }
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
		if (auto* const pGlobal = Global())
			pGlobal->InvalidatePointer(ptr, removed);
	}
};
