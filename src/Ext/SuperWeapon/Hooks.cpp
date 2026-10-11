#include <CellClass.h>
#include <DisplayClass.h>
#include <MapClass.h>

#include <Utilities/Macro.h>

DEFINE_HOOK(0x6DC25A, SuperWeapon_TargetRing_BridgeZ, 0x6)
{
	REF_STACK(int, centerZ, STACK_OFFSET(0x48, -0x4));

	if (CellClass* const pCell =
		MapClass::Instance.TryGetCellAt(DisplayClass::Instance.CurrentFoundation_CenterCell))
	{
		CoordStruct crd { 0, 0, centerZ };
		centerZ = pCell->FixHeight(crd).Z;
	}

	return 0;
}
