#include "Body.h"
#include <Ext/TAction/Features/ScriptManipulator.h>
#include <Ext/TAction/Features/TaskForceManipulator.h>

#include <YRpp.h>
#include <TagClass.h>
#include <TagTypeClass.h>
#include <TechnoClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <HouseClass.h>
#include <BuildingClass.h>
#include <FactoryClass.h>
#include <Ext/House/Body.h>
#include <Ext/House/MoneyPool.h>
#include <New/FootPath/FootPathVisualizer.h>
#include <Ext/Scenario/Body.h>
#include <ArrayClasses.h>
#include <MessageListClass.h>
#include <ScenarioClass.h>
#include <GameOptionsClass.h>
#include <DisplayClass.h>

#include <Utilities/SavegameDef.h>
#include <Utilities/SpawnerHelper.h>
#include <Utilities/GeneralUtils.h>

#include <New/TextBox/MapTextBoxClass.h>
#include <New/TextBox/TextBoxTypeClass.h>
#include <New/TextBox/WaypointTextBoxClass.h>
#include <New/TextBox/TechnoTextBoxClass.h>
#include <New/ChoiceBox/ChoiceBoxTypeClass.h>
#include <New/ChoiceBox/WaypointChoiceBoxClass.h>
#include <New/ChoiceBox/ScreenChoiceBoxClass.h>

#include <set>
#include <vector>
#include <string>
#include <cstring>
#include <Unsorted.h>

bool TActionExt::GiveHouseMoney(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	int houseIndex = pThis->Param3;
	int moneyAmount = pThis->Param4;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;
	if (moneyAmount < 0) return false;

	pOwner->GiveMoney(moneyAmount);

	return true;
}

bool TActionExt::TakeHouseMoney(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	int houseIndex = pThis->Param3;
	int moneyAmount = pThis->Param4;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;
	if (moneyAmount < 0) return false;

	long availableMoney = pOwner->Available_Money();

	if(availableMoney >= moneyAmount)
	{
		pOwner->TakeMoney(moneyAmount);
	}
	else // not enough money, take all remaining money
	{
		pOwner->TakeMoney(availableMoney);
	}

	return true;
}

bool TActionExt::SetHouseMoney(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	int houseIndex = pThis->Param3;
	int moneyAmount = pThis->Param4;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;
	if (moneyAmount < 0) return false;

	pOwner->TakeMoney(pOwner->Available_Money());
	pOwner->GiveMoney(moneyAmount);

	return true;
}

// 移除指定所属方的资金连锁，此后其资金独立
bool TActionExt::RemoveHouseMoneyPool(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	HouseClass* pOwner = HouseClass::FindByCountryIndex(pThis->Param3);
	if (!pOwner) return false;

	return MoneyPoolManager::RemoveHouse(pOwner);
}

// 连结两个所属方的资金连锁。一方已入池则另一方加入该池；两方各在独立池则合并两池(资金相加)；
// 两方都未入池时不动
bool TActionExt::LinkHouseMoneyPools(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	HouseClass* pOwnerA = HouseClass::FindByCountryIndex(pThis->Param3);
	HouseClass* pOwnerB = HouseClass::FindByCountryIndex(pThis->Param4);

	if (!pOwnerA || !pOwnerB) return false;

	return MoneyPoolManager::LinkHouses(pOwnerA, pOwnerB);
}

bool TActionExt::AddBaseNodeForHouseAtWaypoint(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	const int houseIndex = pThis->Param3;
	const int waypointIndex = pThis->Param4;
	const int buildTypeIndex = pThis->Param5;
	const int forceAtFront = pThis->Param6;

	// ===== 基础信息 =====
	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;

	CellStruct cell = ScenarioClass::Instance->GetWaypointCoords(waypointIndex);
	if (cell.X < 0 || cell.Y < 0) return false;

	BaseNodeClass newNode = { buildTypeIndex, cell, false, 0 };

	// ===== 强制放到最前面 =====
	if (forceAtFront)
	{
	    // 1.清除工厂序列
	    for (BuildingClass* pBuilding : BuildingClass::Array)
	    {
	    	if (!pBuilding || pBuilding->Owner != pOwner) continue;
	    	if (!pBuilding->Factory
	    		|| !pBuilding->Factory->Object
	    		|| pBuilding->Factory->Object->WhatAmI() != AbstractType::Building) continue;

	    	pBuilding->Factory->AbandonProduction();
	    	pBuilding->Factory->QueuedObjects.Clear();
	    }

		// 2.强制插入到最前面
		DynamicVectorClass<BaseNodeClass>& nodes = pOwner->Base.BaseNodes;

		// 扩容
		if (nodes.Count >= nodes.Capacity)
		{
			if (nodes.CapacityIncrement <= 0) return false;
			if (!nodes.SetCapacity(nodes.Capacity + nodes.CapacityIncrement, nullptr))
				return false;
		}

		// 直接拷贝赋值后
		for (int i = nodes.Count; i > 0; --i)
		{
			nodes.Items[i] = nodes.Items[i - 1];
		}

		nodes.Items[0] = newNode;
		++nodes.Count;
	}
	// ===== 直接加就好了, 不管他什么时候====
	else
		pOwner->Base.BaseNodes.AddItem(newNode);

	return true;
}

bool TActionExt::RemoveAllBaseNodeForHouseAtWaypoint(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	const int houseIndex = pThis->Param3;
	const int waypointIndex = pThis->Param4;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;

	CellStruct cell = ScenarioClass::Instance->GetWaypointCoords(waypointIndex);
	if (cell.X < 0 || cell.Y < 0) return false;

	// 1. 收集需要删除的节点索引及对应的建筑类型（去重）
	std::vector<int> indicesToRemove;
	std::set<int> uniqueBuildingTypes;
	for (int i = 0; i < pOwner->Base.BaseNodes.Count; ++i)
	{
		const auto& node = pOwner->Base.BaseNodes[i];
		if (node.MapCoords == cell)
		{
			indicesToRemove.push_back(i);
			uniqueBuildingTypes.insert(node.BuildingTypeIndex);
		}
	}

	if (indicesToRemove.empty())
		return true; // 无节点需要删除

	// 2. 清理工厂生产队列(仅影响被删除节点相关的建筑类型)
	for (int buildTypeIndex : uniqueBuildingTypes)
	{
		if (buildTypeIndex < 0 || buildTypeIndex >= BuildingTypeClass::Array.Count)
		{
			// Debug::Log("Invalid buildTypeIndex %d at waypoint %d\n", buildTypeIndex, waypointIndex);
			continue;
		}
		const char* buildTypeID = BuildingTypeClass::Array[buildTypeIndex]->get_ID();

		for (BuildingClass* pBuilding : BuildingClass::Array)
		{
			if (!pBuilding || pBuilding->Owner != pOwner) continue;
			if (!pBuilding->Factory
				|| !pBuilding->Factory->Object
				|| pBuilding->Factory->Object->WhatAmI() != AbstractType::Building) continue;

			TechnoTypeClass* pFactObjType = pBuilding->Factory->Object->GetTechnoType();
			if (pFactObjType && strcmp(pFactObjType->get_ID(), buildTypeID) == 0)
			{
				pBuilding->Factory->AbandonProduction();
				break;
			}
			pBuilding->Factory->QueuedObjects.Clear();
		}
	}

	// 3. 在原容器中倒序删除节点
	for (auto it = indicesToRemove.rbegin(); it != indicesToRemove.rend(); ++it)
	{
		pOwner->Base.BaseNodes.RemoveItem(*it);
	}

	return true;
}

bool TActionExt::RemoveBaseNodesOfBuildingTypeForHouse(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	// AI 真好用
	const int houseIndex = pThis->Param3;
	const int buildTypeIndex = pThis->Param4;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;

	if (buildTypeIndex < 0 || buildTypeIndex >= BuildingTypeClass::Array.Count)
	{
		// Debug::Log("Invalid buildTypeIndex %d\n", buildTypeIndex);
		return false;
	}

	const char* buildTypeID = BuildingTypeClass::Array[buildTypeIndex]->get_ID();
	// Debug::Log("[Start]: Removing base nodes for building type \"%s\".\n", buildTypeID);

	// 1. 收集需要删除的节点索引
	std::vector<int> indicesToRemove;
	for (int i = 0; i < pOwner->Base.BaseNodes.Count; ++i)
	{
		if (pOwner->Base.BaseNodes[i].BuildingTypeIndex == buildTypeIndex)
			indicesToRemove.push_back(i);
	}

	if (indicesToRemove.empty())
	{
		// Debug::Log("[End]: No base nodes found for type \"%s\".\n", buildTypeID);
		return true;
	}

	// 2. 清理工厂生产队列
	for (BuildingClass* pBuilding : BuildingClass::Array)
	{
		if (!pBuilding || pBuilding->Owner != pOwner) continue;
		if (!pBuilding->Factory
			|| !pBuilding->Factory->Object
			|| pBuilding->Factory->Object->WhatAmI() != AbstractType::Building) continue;

		TechnoTypeClass* pFactObjType = pBuilding->Factory->Object->GetTechnoType();
		if (pFactObjType && strcmp(pFactObjType->get_ID(), buildTypeID) == 0)
		{
			pBuilding->Factory->AbandonProduction();
			break;
		}
		pBuilding->Factory->QueuedObjects.Clear();
	}

	// 3. 倒序删除节点
	for (auto it = indicesToRemove.rbegin(); it != indicesToRemove.rend(); ++it)
	{
		pOwner->Base.BaseNodes.RemoveItem(*it);
	}

	return true;
}

bool TActionExt::UpdateAllBuildingAnims(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	for(BuildingClass* pBuilding : BuildingClass::Array)
	{
		if (!pBuilding) continue;
		pBuilding->DisableStuff();
		pBuilding->EnableStuff();
	}

	return true;
}

bool TActionExt::UpdateAssociatedBuildingsAnims(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	for (BuildingClass* pBuilding : BuildingClass::Array)
	{
		if (!pBuilding) continue;
		if (!pBuilding->AttachedTag) continue;

		if(pBuilding->AttachedTag->ContainsTrigger(pTrigger))
		{
			pBuilding->DisableStuff();
			pBuilding->EnableStuff();
		}
	}

	return true;
}

bool TActionExt::UpdateOwnerBuildingsAnimations(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	int houseIndex = pThis->Param3;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;

	for (BuildingClass* pBuilding : BuildingClass::Array)
	{
		if (!pBuilding) continue;

		if(pBuilding->Owner == pOwner)
		{
			pBuilding->DisableStuff();
			pBuilding->EnableStuff();
		}
	}

	return true;
}

// =============================
// 687: Sell All Buildings Of House
// 变卖指定所属方的全部建筑。

bool TActionExt::SellAllBuildingsOfHouse(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	const int houseIndex = pThis->Param3;

	HouseClass* pOwner = HouseClass::FindByCountryIndex(houseIndex);
	if (!pOwner) return false;

	std::vector<BuildingClass*> targets;
	for (BuildingClass* pBuilding : BuildingClass::Array)
	{
		if (!pBuilding)
			continue;
		if (pBuilding->Owner != pOwner)
			continue;
		if (!pBuilding->IsAlive || !pBuilding->IsOnMap || pBuilding->InLimbo)
			continue;
		if (!pBuilding->Type || pBuilding->Type->Unsellable)
			continue;

		targets.push_back(pBuilding);
	}

	if (targets.empty())
		return true;

	for (BuildingClass* pBuilding : targets)
	{
		if (!pBuilding || !pBuilding->IsAlive || pBuilding->InLimbo)
			continue;
		if (pBuilding->Owner != pOwner)
			continue;

		pBuilding->Sell(1);
	}

	return true;
}

