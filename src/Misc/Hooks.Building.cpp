#include <BuildingClass.h>
#include <HouseClass.h>
#include <SessionClass.h>
#include <TechnoClass.h>

#include <Helpers/Macro.h>

DEFINE_HOOK(0x4513DA, BuildingClass_GetImage_UseFinalImageForIdleUnplaced, 0x8)
{
	GET(BuildingClass*, pThis, ECX);

	if (pThis && pThis->QueueBState != 0
		&& !pThis->ActuallyPlacedOnMap
		&& pThis->BState == static_cast<int>(BStateType::Idle))
	{
		return 0x4513E4;
	}

	return 0;
}
