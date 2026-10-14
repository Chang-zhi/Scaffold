#include "Body.h"

#include <Utilities/SavegameDef.h>
#include <Utilities/Debug.h>
#include <Utilities/GeneralUtils.h>
#include <Helpers/Macro.h>
#include <cstdlib>
#include <ScenarioClass.h>
#include <BuildingClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <AircraftClass.h>
#include <CellClass.h>
#include <HouseClass.h>
#include <TriggerClass.h>
#include <GeneralStructures.h>
#include <Fundamentals.h>
// #include <Ext/Techno/DetectKiller.h>

#include <New/ChoiceBox/WaypointChoiceBoxClass.h>
#include <New/ChoiceBox/ScreenChoiceBoxClass.h>
#include <New/ChoiceBox/MapChoiceBoxClass.h>

#include <string>
#include <map>

//Static init
TEventExt::ExtContainer TEventExt::ExtMap;

// =============================
// load / save

template <typename T>
void TEventExt::ExtData::Serialize(T& Stm)
{
	//Stm;
}

void TEventExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<TEventClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TEventExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<TEventClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// by Fly-Star
int TEventExt::GetFlags(int iEvent)
{
	// 0x0 : If it has to have an AttachedObject in order to use it, then let it return 0.
	// 0x0 : 这个事件需要有"附加对象"才能使用，所以返回0。
    // 		 这意味着事件必须挂在一个对象（单位/建筑）上才能工作。
    // 		 比如"被摧毁"事件，只有挂载在某个单位上才有意义。

	// 0x4 : In MapClass, ZoneEntryBy uses it. borrowed from 0x684D61.
	// 0x4 : 在地图类(MapClass)中使用，ZoneEntryBy(进入区域)事件用这个。
	//       0x684D61 是原版游戏中这个逻辑的参考地址

	// 0x8 : In HouseClass, It will be added to the RelatedTags of the specified house. Ares' TriggerEvent 75/77 uses it. borrowed from 0x684E34.
	// 0x8 : 在所属方类(HouseClass)中使用。
	//       这个事件会被添加到指定所属方的 RelatedTags 列表中。
	//       Ares平台的 TriggerEvent 75/77 用了这个。
	//       0x684E34 是原版游戏中这个逻辑的参考地址。

	// 0x10 : In LogicClass. borrowed from 0x684DCA.
	// 0x10: 在逻辑类(LogicClass)中使用。
	//       这类事件每帧都在逻辑更新循环中被检查。
	//       0x684DCA 是原版游戏中这个逻辑的参考地址。

	switch (static_cast<ScaffoldTriggerEvent>(iEvent))
	{
	//case ScaffoldTriggerEvent::TechnoDestroyedByHouse:
	//	return 0;
	//case
	//	return 0x4;
	//case
	//	return 0x8;
	default:
		return 0x10;
	}
}

std::optional<bool> TEventExt::Execute(TEventClass* pThis, int iEvent, HouseClass* pHouse,
	ObjectClass* pObject, CDTimerClass* pTimer, bool* isPersitant, TechnoClass* pSource,
	TriggerClass* pTrigger)
{
	const auto eventKind = static_cast<ScaffoldTriggerEvent>(pThis->EventKind);

	// They must be the same, but for other triggers to take effect normally, this cannot be judged outside case.
	auto isSameEvent = [&]() { return eventKind == static_cast<ScaffoldTriggerEvent>(iEvent); };

	switch (eventKind)
	{
	// The triggering conditions that need to be checked at any time are written here

		// helper struct
		struct and_with { bool operator()(int a, int b) { return a & b; } };

	case ScaffoldTriggerEvent::TechnoTypeOfHouseNearWaypoint:
		return TEventExt::TechnoTypeOfHouseNearWaypoint(pThis, pHouse);
	case ScaffoldTriggerEvent::TechnoTypeOfHouseAllLeavesWaypoint:
		return !TEventExt::TechnoTypeOfHouseNearWaypoint(pThis, pHouse);
	case ScaffoldTriggerEvent::TechnoTypeOfHouseExistsAtWaypoint:
		return TEventExt::TechnoTypeOfHouseExistsAtWaypoint(pThis, pHouse);
	case ScaffoldTriggerEvent::TechnoTypeOfHouseNotExistsAtWaypoint:
		return !TEventExt::TechnoTypeOfHouseExistsAtWaypoint(pThis, pHouse);
	case ScaffoldTriggerEvent::ElapsedTimeFrames:
		return TEventExt::ElapsedTimeFramesFunc(pThis);

	case ScaffoldTriggerEvent::MissionTimerGreater:
		return TEventExt::MissionTimerGreaterFunc(pThis);
	case ScaffoldTriggerEvent::MissionTimerLess:
		return TEventExt::MissionTimerLessFunc(pThis);

	case ScaffoldTriggerEvent::ChoiceBoxButtonClicked:
		return TEventExt::ChoiceBoxButtonClickedFunc(pThis, pHouse);
	case ScaffoldTriggerEvent::ChoiceBoxAnyButtonClicked:
		return TEventExt::ChoiceBoxAnyButtonClickedFunc(pThis, pHouse);
	case ScaffoldTriggerEvent::ChoiceBoxTimedOut:
		return TEventExt::ChoiceBoxTimedOutFunc(pThis, pHouse);

	case ScaffoldTriggerEvent::HousePowerOutputMuch:
		return TEventExt::PowerHander(pThis, pHouse, PowerEventMode::Output, true);
	case ScaffoldTriggerEvent::HousePowerOutputLess:
		return TEventExt::PowerHander(pThis, pHouse, PowerEventMode::Output, false);
	case ScaffoldTriggerEvent::HousePowerDrainMuch:
		return TEventExt::PowerHander(pThis, pHouse, PowerEventMode::Drain, true);
	case ScaffoldTriggerEvent::HousePowerDrainLess:
		return TEventExt::PowerHander(pThis, pHouse, PowerEventMode::Drain, false);
	case ScaffoldTriggerEvent::HousePowerSurplusMuch:
		return TEventExt::PowerHander(pThis, pHouse, PowerEventMode::Surplus, true);
	case ScaffoldTriggerEvent::HousePowerSurplusLess:
		return TEventExt::PowerHander(pThis, pHouse, PowerEventMode::Surplus, false);


	default:
		return std::nullopt;
	};
}

bool TEventExt::TechnoTypeOfHouseNearWaypoint(TEventClass* pThis, HouseClass* pHouse)
{
	int range = pThis->Value;
	int wayPointIndex = std::atoi(pThis->String);

	CellStruct cell = ScenarioClass::Instance->GetWaypointCoords(wayPointIndex);

	for (auto pTechno : TechnoClass::Array)
	{
		if(pTechno && pTechno->Owner == pHouse)
		{
			if (GeneralUtils::IsTechnoNearCell(pTechno, cell, range))
			{
				return true;
			}
		}
	}

	return false;
}

bool TEventExt::TechnoTypeOfHouseExistsAtWaypoint(TEventClass* pThis, HouseClass* pHouse)
{
	int wayPointIndex = pThis->Value;
	const char* technoID = pThis->String;
	// Debug::Log("[TEventExt] Value is \"%d\" , String is \"%s\", pHouse is \"%s\".\n", wayPointIndex, technoID, pHouse->Type->get_ID());

	CellStruct cell = ScenarioClass::Instance->GetWaypointCoords(wayPointIndex);

	for(TechnoClass* pTechno : TechnoClass::Array)
	{
		//if(pTechno)
		//	Debug::Log("[TEventExt] Checking Techno \"%s\"\n", pTechno->get_ID());

		if (pTechno
			&& pTechno->Owner == pHouse
			&& strcmp(pTechno->get_ID(), technoID) == 0)
		{
			// Debug::Log("[TEventExt] Found Techno common ID and HOUSE.\n");
			if (pTechno->WhatAmI() == AbstractType::Building) // 是建筑, 需要判断坐标是否在建筑基底范围内
			{
				// Debug::Log("[TEventExt] Techno is a building.\n");
				if (BuildingClass* pBuilding = abstract_cast<BuildingClass*>(pTechno))
				{
					// Debug::Log("[TEventExt] Checking building foundation.\n");
					if (GeneralUtils::IsCellInBuildingFoundation(pBuilding, cell))
					{
						// Debug::Log("[TEventExt] TechnoTypeOfHouseExistsAtWaypoint: Found Building at waypoint.\n");
						return true;
					}
				}
			}
			else
			{
				// Debug::Log("[TEventExt] Techno is not a building.\n");
				if (CellClass::Coord2Cell(pTechno->GetCoords()) == cell) // 不是建筑类型, 直接判断坐标即可
				{
					// Debug::Log("[TEventExt] TechnoTypeOfHouseExistsAtWaypoint: Found Techno at waypoint.\n");
					return true;
				}
			}
		}
	}

	return false;
}

namespace
{
	std::map<const TEventClass*, int> ElapsedTimeFrames_StartFrames;
	struct ElapsedFrameRecord
	{
		TEventClass* Event = nullptr;
		int StartFrame = 0;
	};
	std::vector<ElapsedFrameRecord> LoadedStartFrames;
}

void TEventExt::Clear()
{
	ElapsedTimeFrames_StartFrames.clear();
	LoadedStartFrames.clear();
	ExtMap.Clear();
}

void TEventExt::PointerGotInvalid(void* ptr, bool)
{
	ElapsedTimeFrames_StartFrames.erase(static_cast<TEventClass*>(ptr));
}

bool TEventExt::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	std::vector<ElapsedFrameRecord> records;
	for (auto event : TEventClass::Array)
	{
		const auto it = ElapsedTimeFrames_StartFrames.find(event);
		if (it != ElapsedTimeFrames_StartFrames.end())
			records.push_back({ event, it->second });
	}
	Stm.Save(records.size());
	for (auto& record : records)
		Stm.Process(record.Event).Process(record.StartFrame);
	return Stm.Success();
}

bool TEventExt::LoadGlobals(ScaffoldStreamReader& Stm)
{
	Clear();
	size_t count = 0;
	if (!Stm.Load(count) || count > 1024 * 1024)
		return false;
	LoadedStartFrames.resize(count);
	for (auto& record : LoadedStartFrames)
		Stm.Process(record.Event).Process(record.StartFrame);
	return Stm.Success();
}

bool TEventExt::CompleteLoad()
{
	for (const auto& record : LoadedStartFrames)
	{
		if (!record.Event || !ElapsedTimeFrames_StartFrames.emplace(record.Event, record.StartFrame).second)
			return false;
	}
	LoadedStartFrames.clear();
	return true;
}

bool TEventExt::ElapsedTimeFramesFunc(TEventClass* pThis)
{
	int waitFrames = pThis->Value;

	auto it = ElapsedTimeFrames_StartFrames.find(pThis);
	if (it == ElapsedTimeFrames_StartFrames.end())
	{
		ElapsedTimeFrames_StartFrames[pThis] = Unsorted::CurrentFrame;
		// Debug::Log("[TEventExt] ElapsedTimeFrames: New trigger registered, start frame set to %d, waiting %d frames.\n", Unsorted::CurrentFrame, waitFrames);
	}

	int startFrame = ElapsedTimeFrames_StartFrames[pThis];
	int elapsed = Unsorted::CurrentFrame - startFrame;
	bool result = elapsed >= waitFrames;

	// Debug::Log("[TEventExt] ElapsedTimeFrames: startFrame=%d, currentFrame=%d, elapsed=%d, waitFrames=%d, result=%s.\n",
		// startFrame, Unsorted::CurrentFrame, elapsed, waitFrames, result ? "true" : "false");

	return result;
}

// ============================================================================
// 555: MissionTimer > N seconds
// ============================================================================
bool TEventExt::MissionTimerGreaterFunc(TEventClass* pThis)
{
	auto const pTimer = &ScenarioClass::Instance->MissionTimer;
	int thresholdFrames = pThis->Value * 15;

	return pTimer->GetTimeLeft() > thresholdFrames;
}

// ============================================================================
// 556: MissionTimer < N seconds
// ============================================================================
bool TEventExt::MissionTimerLessFunc(TEventClass* pThis)
{
	auto const pTimer = &ScenarioClass::Instance->MissionTimer;
	int thresholdFrames = pThis->Value * 15;

	return pTimer->GetTimeLeft() < thresholdFrames;
}

// ============================================================================
// 557: ChoiceBox 指定按钮被点击
// FA2 参数映射：第 1 个编辑器参数 → Value，第 2 个 → String
// 557 参数顺序为 [按钮序号(从1开始), 选择框编号]，因此 Value = 按钮序号，String = 选择框编号
// ============================================================================
bool TEventExt::ChoiceBoxButtonClickedFunc(TEventClass* pThis, HouseClass* pHouse)
{
	int targetID = std::atoi(pThis->String);
	int targetButtonIndex = pThis->Value - 1;

	auto* pBox = MapChoiceBoxClass::FindByID(targetID);
	if (!pBox || pBox->IsExpired || pBox->ClickedConsumed)
		return false;

	if (pBox->ClickedIndex == targetButtonIndex)
	{
		pBox->ClickedConsumed = true;
		return true;
	}
	return false;
}

// ============================================================================
// 558: ChoiceBox 任意按钮被点击
// 唯一参数：选择框编号（→ Value）
// ============================================================================
bool TEventExt::ChoiceBoxAnyButtonClickedFunc(TEventClass* pThis, HouseClass* pHouse)
{
	int targetID = pThis->Value;

	auto* pBox = MapChoiceBoxClass::FindByID(targetID);
	if (!pBox || pBox->IsExpired || pBox->ClickedConsumed)
		return false;

	if (pBox->ClickedIndex >= 0)
	{
		pBox->ClickedConsumed = true;
		return true;
	}
	return false;
}

// ============================================================================
// 559: ChoiceBox 超时未选
// 唯一参数：选择框编号（→ Value）
// 仅在"Duration 耗尽且从未点击"时触发；点击后销毁的选择框不触发超时
// ============================================================================
bool TEventExt::ChoiceBoxTimedOutFunc(TEventClass* pThis, HouseClass* pHouse)
{
	int targetID = pThis->Value;

	auto* pBox = MapChoiceBoxClass::FindByID(targetID);
	if (!pBox)
		return false;

	return pBox->IsTimedOut();
}

bool TEventExt::PowerHander(TEventClass* pThis, HouseClass* pHouse, PowerEventMode mode, bool isMuch)
{
	if (!pHouse) return false;
	if (Unsorted::CurrentFrame == 0) return false;

	int val = pThis->Value;
	int target = 0;

	switch (mode)
	{
	case PowerEventMode::Output:
		target = pHouse->PowerOutput;
		break;
	case PowerEventMode::Drain:
		target = pHouse->PowerDrain;
		break;
	case PowerEventMode::Surplus:
		target = pHouse->PowerOutput - pHouse->PowerDrain;
		break;
	default:
		break;
	}

	return isMuch ? (val < target) : (val > target);
}

// =============================
// container

TEventExt::ExtContainer::ExtContainer() : Container("TEventClass") { }

TEventExt::ExtContainer::~ExtContainer() = default;

