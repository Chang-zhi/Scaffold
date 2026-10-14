#include "Body.h"
#include "MoneyPool.h"

#include <Ext/TechnoType/Body.h>
#include <Ext/Techno/Body.h>

#include <ScenarioClass.h>
#include <BuildingClass.h>

#include <Utilities/Debug.h>

#include <string>

//Static init

HouseExt::ExtContainer HouseExt::ExtMap;

size_t HouseExt::FindOwnedIndex(
	HouseClass const* const, int const idxParentCountry,
	Iterator<TechnoTypeClass const*> const items, size_t const start)
{
	auto const bitOwner = 1u << idxParentCountry;

	for (size_t i = start; i < items.size(); ++i)
	{
		auto const pItem = items[i];

		if (pItem->InOwners(bitOwner))
			return i;
	}

	return items.size();
}

bool HouseExt::IsDisabledFromShell(
	HouseClass const* const pHouse, BuildingTypeClass const* const pItem)
{
	// SWAllowed does not apply to campaigns any more
	if (SessionClass::IsCampaign()
		|| GameModeOptionsClass::Instance.SWAllowed)
	{
		return false;
	}

	if (pItem->SuperWeapon != -1)
	{
		// allow SWs only if not disableable from shell
		auto const pItem2 = const_cast<BuildingTypeClass*>(pItem);
		auto const& BuildTech = RulesClass::Instance->BuildTech;

		if (BuildTech.FindItemIndex(pItem2) == -1)
		{
			auto const pSuper = pHouse->Supers[pItem->SuperWeapon];
			if (pSuper && pSuper->Type && pSuper->Type->DisableableFromShell)
				return true;
		}
	}

	return false;
}

// This basically gets same cell that AI script action 53 Gather at Enemy Base uses, and code for that (0x6EF700) was used as reference here.
CellClass* HouseExt::GetEnemyBaseGatherCell(HouseClass* pTargetHouse, HouseClass* pCurrentHouse, CoordStruct defaultCurrentCoords, SpeedType speedTypeZone, int extraDistance)
{
	if (!pTargetHouse || !pCurrentHouse)
		return nullptr;

	const auto targetCoords = CellClass::Cell2Coord(pTargetHouse->GetBaseCenter());

	if (targetCoords == CoordStruct::Empty)
		return nullptr;

	auto currentCoords = CellClass::Cell2Coord(pCurrentHouse->GetBaseCenter());

	if (currentCoords == CoordStruct::Empty)
		currentCoords = defaultCurrentCoords;

	const int distance = (RulesClass::Instance->AISafeDistance + extraDistance) * Unsorted::LeptonsPerCell;
	const auto newCoords = GeneralUtils::CalculateCoordsFromDistance(currentCoords, targetCoords, distance);

	auto cellStruct = CellClass::Coord2Cell(newCoords);
	cellStruct = MapClass::Instance.NearByLocation(cellStruct, speedTypeZone, -1, MovementZone::Normal, false, 3, 3, false, false, false, true, cellStruct, false, false);

	if (cellStruct == CellStruct::Empty)
		return nullptr;

	return MapClass::Instance.TryGetCellAt(cellStruct);
}

// 移除指定坐标的所有授权节点
void HouseExt::RemoveAuthorizedNodeByCoord(HouseClass* pHouse, short x, short y)
{
	auto pExt = HouseExt::ExtMap.Find(pHouse);
	if (!pExt) return;

	auto it = pExt->AuthorizedNodeKeys.begin();
	while (it != pExt->AuthorizedNodeKeys.end())
	{
		if (it->X == x && it->Y == y)
			it = pExt->AuthorizedNodeKeys.erase(it);
		else
			++it;
	}

	// 如果当前目标就是这个坐标，清除记录
	if (pExt->LastFrameTargetX == x && pExt->LastFrameTargetY == y)
	{
		pExt->LastFrameTargetType = -1;
		pExt->LastFrameTargetX = -1;
		pExt->LastFrameTargetY = -1;
	}

}

// 移除指定类型的所有授权节点
void HouseExt::RemoveAuthorizedNodeByType(HouseClass* pHouse, int buildingTypeIndex)
{
	auto pExt = HouseExt::ExtMap.Find(pHouse);
	if (!pExt) return;

	auto it = pExt->AuthorizedNodeKeys.begin();
	while (it != pExt->AuthorizedNodeKeys.end())
	{
		if (it->BuildingTypeIndex == buildingTypeIndex)
			it = pExt->AuthorizedNodeKeys.erase(it);
		else
			++it;
	}

	// 如果当前目标就是这个类型，清除记录
	if (pExt->LastFrameTargetType == buildingTypeIndex)
	{
		pExt->LastFrameTargetType = -1;
		pExt->LastFrameTargetX = -1;
		pExt->LastFrameTargetY = -1;
	}
}

void HouseExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	const char* pSection = this->OwnerObject()->PlainName;
	INI_EX exINI(pINI);

	this->BaseNodeCrossOwners.Read(exINI, pSection, "BaseNodeCrossOwners");

	// 资金连锁：同名的所属方共用一个资金池。名字为空则不共享
	char poolName[64] = "";
	pINI->ReadString(pSection, "SharedMoneyPool", "", poolName);

	Debug::Log("[MoneyPool] INI read: section=\"%s\", key=\"%s\"\n", pSection, poolName);

	if (poolName[0])
		MoneyPoolManager::RegisterHouse(this->OwnerObject(), poolName);
}

// =============================
// load / save

template <typename T>
void HouseExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->BaseNodeCrossOwners)
		.Process(this->AuthorizedNodesCaptured)
		.Process(this->LastFrameTargetType)
		.Process(this->LastFrameTargetX)
		.Process(this->LastFrameTargetY)
		;

	// === 序列化 AuthorizedNodeKeys ===
	{
		int count = (int)this->AuthorizedNodeKeys.size();
		Stm.Process(count);

		if constexpr (std::is_same_v<T, ScaffoldStreamReader>)
			this->AuthorizedNodeKeys.resize(count);

		for (int i = 0; i < count; ++i)
		{
			Stm.Process(this->AuthorizedNodeKeys[i].BuildingTypeIndex);
			Stm.Process(this->AuthorizedNodeKeys[i].X);
			Stm.Process(this->AuthorizedNodeKeys[i].Y);
		}
	}

	// === 序列化 DeferredNodeList ===
	{
		int count = (int)this->DeferredNodeList.size();
		Stm.Process(count);

		if constexpr (std::is_same_v<T, ScaffoldStreamReader>)
			this->DeferredNodeList.resize(count);

		for (int i = 0; i < count; ++i)
		{
			auto& info = this->DeferredNodeList[i];
			Stm.Process(info.BuildingTypeIndex);
			Stm.Process(info.MapCoords.X);
			Stm.Process(info.MapCoords.Y);
			Stm.Process(info.Owner);
		}
	}
}

void HouseExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<HouseClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void HouseExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<HouseClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

void HouseExt::Clear()
{
	MoneyPoolManager::Clear();
	ExtMap.Clear();
}

// 池结构整体随存档保存：成员记所属方索引，读档后由 SyncAllPools 解析，避免指针 swizzle 时序问题
bool HouseExt::LoadGlobals(ScaffoldStreamReader& Stm)
{
	MoneyPoolManager::Clear();

	int poolCount = 0;
	if (!Stm.Load(poolCount) || poolCount < 0 || poolCount > 1024)
		return false;

	for (int i = 0; i < poolCount; ++i)
	{
		int nameLength = 0;
		if (!Stm.Load(nameLength) || nameLength < 0 || nameLength > 256)
			return false;

		std::string name(static_cast<size_t>(nameLength), '\0');
		if (nameLength > 0 && !Stm.Read(reinterpret_cast<ScaffoldByteStream::data_t*>(name.data()), nameLength))
			return false;

		auto& pool = MoneyPoolManager::Pools[name];

		int base = 0;
		int initialized = 0;
		int memberCount = 0;

		if (!Stm.Load(base) || !Stm.Load(initialized)
			|| !Stm.Load(memberCount) || memberCount < 0 || memberCount > 1024)
		{
			return false;
		}

		pool.Base = base;
		pool.Initialized = (initialized != 0);
		pool.MemberIndices.clear();
		pool.MemberIndices.reserve(memberCount);

		for (int j = 0; j < memberCount; ++j)
		{
			int index = 0;
			if (!Stm.Load(index))
				return false;

			pool.MemberIndices.push_back(index);
		}
	}

	return Stm.Success();
}

bool HouseExt::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	const auto& pools = MoneyPoolManager::Pools;

	Stm.Save(static_cast<int>(pools.size()));

	for (const auto& [name, pool] : pools)
	{
		const int nameLength = static_cast<int>(name.size());
		Stm.Save(nameLength);
		Stm.Write(reinterpret_cast<const ScaffoldByteStream::data_t*>(name.data()), nameLength);

		Stm.Save(pool.Base);
		Stm.Save(static_cast<int>(pool.Initialized ? 1 : 0));
		Stm.Save(static_cast<int>(pool.MemberIndices.size()));

		for (const int index : pool.MemberIndices)
			Stm.Save(index);
	}

	return Stm.Success();
}


// =============================
// container

HouseExt::ExtContainer::ExtContainer() : Container("HouseClass")
{ }

HouseExt::ExtContainer::~ExtContainer() = default;

// =============================
// container hooks

DEFINE_HOOK(0x4F6532, HouseClass_CTOR, 0x5)
{
	GET(HouseClass*, pItem, EAX);

	HouseExt::ExtMap.TryAllocate(pItem);


	return 0;
}

DEFINE_HOOK(0x4F7371, HouseClass_DTOR, 0x6)
{
	GET(HouseClass*, pItem, ESI);

	HouseExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x504080, HouseClass_SaveLoad_Prefix, 0x5)
DEFINE_HOOK(0x503040, HouseClass_SaveLoad_Prefix, 0x5)
{
	GET_STACK(HouseClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	HouseExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x504069, HouseClass_Load_Suffix, 0x7)
{
	HouseExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x5046DE, HouseClass_Save_Suffix, 0x7)
{
	HouseExt::ExtMap.SaveStatic();

	return 0;
}

DEFINE_HOOK(0x50114D, HouseClass_InitFromINI, 0x5)
{
	GET(HouseClass* const, pThis, EBX);
	GET(CCINIClass* const, pINI, ESI);

	HouseExt::ExtMap.LoadFromINI(pThis, pINI);

	return 0;
}
