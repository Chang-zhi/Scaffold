#include <Ext/House/Body.h>
#include <Ext/Rules/Body.h>

#include <BuildingClass.h>
#include <UnitTypeClass.h>

#include <Helpers/Macro.h>

namespace
{
	// sub_42E820 is reached from the native base-node satisfaction paths below.
	// Unknown callers intentionally keep the original owner-only behavior.
	static bool IsBaseNodeSatisfactionCaller(DWORD returnAddress)
	{
		switch (returnAddress)
		{
		case 0x42E78Eu: // sub_42E780 -> sub_42E820
		case 0x4FDEC2u: // sub_4FDD10 -> sub_42E820
		case 0x50C3C2u: // sub_50C340 -> sub_42E820
			return true;
		default:
			return false;
		}
	}

	// This only widens the owner scope. Type, coordinate and the native wall
	// matching rules are still evaluated by the original sub_42E820 body.
	static bool AllowCrossOwnerNodeBuilding(
		BaseClass* pBase,
		BuildingClass* pBuilding,
		DWORD returnAddress)
	{
		if (!pBase || !pBuilding || !pBuilding->Owner)
			return false;

		HouseClass* pHouse = pBase->Owner;
		if (!pHouse || pBuilding->Owner == pHouse)
			return false;

		if (!IsBaseNodeSatisfactionCaller(returnAddress))
			return false;

		auto const pExt = HouseExt::ExtMap.Find(pHouse);
		return pExt && pExt->BaseNodeCrossOwners;
	}
}

extern "C" bool __cdecl HouseClass_BaseNode_AllowCrossOwner(
	BaseClass* pBase,
	BuildingClass* pBuilding,
	DWORD returnAddress)
{
	return AllowCrossOwnerNodeBuilding(pBase, pBuilding, returnAddress);
}

// sub_42E820 has already compared the candidate building owner with the
// BaseClass owner when this hook runs. On a foreign owner, allow the native
// exact-type and wall-specific branches to continue only for the confirmed
// base-node satisfaction callers. This keeps the wall Upgrades[3] and
// previous-node accounting entirely in the original executable code.
DEFINE_HOOK(0x42E8BE, HouseClass_BaseNode_CrossOwnerGate, 0x6)
{
	GET(BaseClass*, pBase, EDI);
	GET(BuildingClass*, pBuilding, EBX);
	GET(HouseClass*, pCandidateOwner, EAX);
	GET(HouseClass*, pBaseOwner, ECX);
	GET_STACK(DWORD, returnAddress, 0x28);

	if (pCandidateOwner == pBaseOwner
		|| HouseClass_BaseNode_AllowCrossOwner(pBase, pBuilding, returnAddress))
	{
		return 0x42E8C4;
	}

	return 0x42E984;
}

// =============================
// 可收起建筑（建筑 ⇄ 载具）的基地节点：按所属方科技类型数量判定
//
// 奴隶矿场这类建筑同时存在 BuildingType 与 UnitType：它自己 UndeploysInto 成
// 载具去找矿，再 DeploysInto 回建筑采矿。原版 sub_42E820 用「节点坐标上是否
// 站着本方的同类型建筑」判定节点完成，这类建筑一移动，节点就永远判定失败，
// AI 于是不停地重建，建筑生产线上永远挂着相同的建筑。
//
// [General] BaseNodeUseDeployableCount=yes 时改为按数量判定：
//   可收起建筑 = UndeploysInto 非空，且存在载具的 DeploysInto 指回它；
//   可考虑数量 = 本方该建筑数量 + 本方部署后成为该建筑的载具数量；
//   剩余同类节点数量 <= 可考虑数量时，节点判定为已完成。
//
// 数量只统计地图上真实存在的对象（Active*Types），不含仍留在工厂里等待
// 放置的 limbo 建筑，否则节点会提前完成而建筑无处安放。
//
// 标记顺序由节点顺序指定：每次只判定当前这一个节点，逐个完成、逐个从基地
// 节点列表里移除，因此被清空的次序始终跟随 BaseNodes 里的节点顺序。数量
// 不足时完全交回原版逻辑，其它节点的既有行为不受影响。


namespace
{
	// 可收起建筑：UndeploysInto 非空，且存在载具的 DeploysInto 指回该建筑。
	static bool IsDeployableBuildingType(BuildingTypeClass* pBuildingType)
	{
		return pBuildingType
			&& pBuildingType->UndeploysInto
			&& pBuildingType->UndeploysInto->DeploysInto == pBuildingType;
	}

	// 该建筑类型在本方还有多少个基地节点。
	static int GetDeployableNodeCount(BaseClass* pBase, int buildingTypeIndex)
	{
		int count = 0;

		for (int i = 0; i < pBase->BaseNodes.Count; ++i)
		{
			if (pBase->BaseNodes[i].BuildingTypeIndex == buildingTypeIndex)
				++count;
		}

		return count;
	}

	// 本方可以变成该建筑的科技数量：建筑本体 + 部署后成为该建筑的载具。
	//
	// 用 CountOwnedAndPresent（Active*Types）而不是 CountOwnedNow：
	// 建筑刚造好时仍在 factory 里等着放置，此刻它已经计入 Owned*Types，
	// 若按 Owned 计数，节点会在建筑真正落地之前就判定完成并被移除，
	// 建筑随后无处安放，AI 于是反复重建。
	static int GetDeployableTechCount(HouseClass* pHouse, BuildingTypeClass* pBuildingType)
	{
		if (!pHouse || !pBuildingType)
			return 0;

		int count = pHouse->CountOwnedAndPresent(pBuildingType);

		for (auto const pUnitType : UnitTypeClass::Array)
		{
			if (pUnitType && pUnitType->DeploysInto == pBuildingType)
				count += pHouse->CountOwnedAndPresent(pUnitType);
		}

		return count;
	}

	static bool ShouldUseDeployableCount()
	{
		auto const pExt = RulesExt::Global();
		return pExt && pExt->BaseNodeUseDeployableCount;
	}

	// 原版按坐标判定失败后，再看本方科技类型数量是否够这个节点用。
	static bool IsBaseNodeSatisfiedByDeployableCount(BaseClass* pBase, int nodeIndex)
	{
		if (!ShouldUseDeployableCount())
			return false;

		if (!pBase || !pBase->Owner
			|| nodeIndex < 0 || nodeIndex >= pBase->BaseNodes.Count)
		{
			return false;
		}

		int const buildingTypeIndex = pBase->BaseNodes[nodeIndex].BuildingTypeIndex;

		if (buildingTypeIndex < 0 || buildingTypeIndex >= BuildingTypeClass::Array.Count)
			return false;

		BuildingTypeClass* pBuildingType = BuildingTypeClass::Array[buildingTypeIndex];

		if (!IsDeployableBuildingType(pBuildingType))
			return false;

		int const available = GetDeployableTechCount(pBase->Owner, pBuildingType);

		if (available <= 0)
			return false;

		return available >= GetDeployableNodeCount(pBase, buildingTypeIndex);
	}
}

// 0x42E78E 起的 6 字节覆写：
//   test eax,eax (2) / jz short loc_42E799 (2) / pop edi (1) / mov al,1 (1)
// Syringe 固定注入 5 字节 E9 rel32，size 必须 >= 5 且不能把 rel32 的尾巴
// 留在补丁中间（之前 size=4 导致 rel32 覆盖 0x42E792 的 pop edi，返回该地址
// 即执行残缺指令而崩溃）。
//
// 钩子处于 call sub_42E820 之后，节点序号已被被调用方的 retn 4 弹掉，
// 此时 ESP+0 是保存的 EDI、ESP+4 是 ESI。成功判定时跳进下面的裸函数复刻
// 原版尾声（pop edi 落在覆写区内）。
static __declspec(naked) void BaseNode_SatisfiedReturn()
{
	__asm
	{
		pop edi
		mov al, 1
		pop esi
		retn 4
	}
}

DEFINE_HOOK(0x42E78E, HouseClass_BaseNode_DeployableCount, 0x6)
{
	GET(BaseClass*, pBase, EDI);
	GET(int, nodeIndex, ESI);

	// 原版已判定完成（节点坐标上有本方同类型建筑）。
	if (R->EAX())
		return reinterpret_cast<DWORD>(BaseNode_SatisfiedReturn);

	if (IsBaseNodeSatisfiedByDeployableCount(pBase, nodeIndex))
		return reinterpret_cast<DWORD>(BaseNode_SatisfiedReturn);

	// 原版失败路径：继续走 Upgrades[3] 墙体兜底。
	return 0x42E799;
}