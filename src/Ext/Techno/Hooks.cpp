#include "Body.h"

#include <Unsorted.h>

#include <Utilities/Debug.h>


// Avoid secondary jump
DEFINE_JUMP(VTABLE, 0x7E2328, 0x41C200) // AircraftClass_GetTechnoType -> AircraftClass_GetType
DEFINE_JUMP(VTABLE, 0x7E3F40, 0x459EE0) // BuildingClass_GetTechnoType -> BuildingClass_GetType
DEFINE_JUMP(VTABLE, 0x7EB0DC, 0x51FAF0) // InfantryClass_GetTechnoType -> InfantryClass_GetType
DEFINE_JUMP(VTABLE, 0x7F5CF4, 0x741490) // UnitClass_GetTechnoType -> UnitClass_GetType

// Early, before ObjectClass_AI
DEFINE_HOOK(0x6F9E50, TechnoClass_AI, 0x5)
{
	GET(TechnoClass*, pThis, ECX);

	// Berzerk restore check
	TechnoExt::BerzerkRestoreCheck(pThis);

	// Temporal exclusive 的全局维护：每帧只跑一次
	{
		static int lastTemporalFrame = 0;
		if (Unsorted::CurrentFrame != lastTemporalFrame)
		{
			lastTemporalFrame = Unsorted::CurrentFrame;
			TechnoExt::TemporalExclusive::CleanupInvalidTemporalLocks();
			TechnoExt::TemporalExclusive::UpdateTemporalExclusive();
		}
	}

	// Temporal exclusive
	TechnoExt::HandleLegalTargetAITargeting(pThis);
	TechnoExt::TemporalExclusive::HandleTemporalExclusiveTargeting(pThis);

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	if (pExt)
	{
		pExt->UpdateTemporalAOE();
		pExt->UpdateDeployWarpField();
		pExt->UpdateEffects();
	}

	{
		static int lastFrame = 0;
		if (Unsorted::CurrentFrame != lastFrame)
		{
			lastFrame = Unsorted::CurrentFrame;
			TechnoExt::TemporalAOE::ValidateGlobals();
		}
	}

	return 0;
}

// After TechnoClass_AI
DEFINE_HOOK(0x4DA54E, FootClass_AI, 0x6)
{
	GET(FootClass*, pThis, ESI);

	TechnoExt::ProcessAutoHunt(pThis);

	return 0;
}
