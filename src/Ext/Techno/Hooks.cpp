#include "Body.h"

#include <Unsorted.h>

#include <Utilities/Debug.h>
#include <Utilities/SyncState.h>

namespace
{
	SyncState::FrameGate GlobalFrame;
	SyncState::FrameGate ExclusiveFrame;
}

void TechnoExt::ResetFrameState()
{
	GlobalFrame.Reset();
	ExclusiveFrame.Reset();
}

void TechnoExt::SaveFrameState(ScaffoldStreamWriter& Stm)
{
	Stm.Process(GlobalFrame.LastFrame);
	Stm.Process(ExclusiveFrame.LastFrame);
}

void TechnoExt::LoadFrameState(ScaffoldStreamReader& Stm)
{
	Stm.Process(GlobalFrame.LastFrame);
	Stm.Process(ExclusiveFrame.LastFrame);
}

void TechnoExt::UpdateGlobalState()
{
	if (!GlobalFrame.TryEnter(Unsorted::CurrentFrame))
		return;

	TemporalAOE::ValidateGlobals();
}


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

	if (ExclusiveFrame.TryEnter(Unsorted::CurrentFrame))
	{
		TechnoExt::TemporalExclusive::CleanupInvalidTemporalLocks();
		TechnoExt::TemporalExclusive::UpdateTemporalExclusive();
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
	TechnoExt::UpdateGlobalState();

	return 0;
}

// After TechnoClass_AI
DEFINE_HOOK(0x4DA54E, FootClass_AI, 0x6)
{
	GET(FootClass*, pThis, ESI);

	TechnoExt::ProcessAutoHunt(pThis);

	return 0;
}
