#include <Ext/House/Body.h>

#include <BuildingClass.h>
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
