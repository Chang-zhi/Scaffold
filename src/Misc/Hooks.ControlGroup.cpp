#include <HouseClass.h>
#include <TechnoClass.h>

#include <Helpers/Macro.h>

DEFINE_HOOK(0x6F6A58, TechnoClass_DrawHealthBar_ShowControlledHousePips, 0x6)
{
	GET(TechnoClass*, pThis, ESI);

	const auto pOwner = pThis->Owner;

	return pOwner && pOwner->IsControlledByCurrentPlayer() ? 0x6F6A8E : 0;
}

DEFINE_HOOK(0x6F677D, TechnoClass_DrawHealthBar_ShowControlledHouseBuildingPips, 0x5)
{
	GET(TechnoClass*, pThis, ESI);

	const auto pOwner = pThis->Owner;

	if (!pOwner || !pOwner->IsControlledByCurrentPlayer())
		return 0;

	R->BL(1);
	return 0x6F67B4;
}
