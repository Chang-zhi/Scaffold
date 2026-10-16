#include <HouseClass.h>
#include <TechnoClass.h>

#include <Ext/Rules/Body.h>

#include <Helpers/Macro.h>

DEFINE_HOOK(0x63821F, PlanningManager_AddNode_AllowAfterTerminalCommand, 0x9)
{
	const auto pRulesExt = RulesExt::Global();

	if (!pRulesExt || !pRulesExt->AllowPlanningAfterTerminalCommand)
		return 0;

	GET(int, validationResult, EAX);

	return validationResult == 1 || validationResult == 2 ? 0x638325 : 0;
}

DEFINE_HOOK(0x638380, PlanningManager_AddNode_KeepSelectionAfterTerminalCommand, 0x7)
{
	const auto pRulesExt = RulesExt::Global();

	if (!pRulesExt || !pRulesExt->AllowPlanningAfterTerminalCommand)
		return 0;

	return 0x6383B4;
}

DEFINE_HOOK(0x638F45, PlanningManager_UpdateWaypointHover_AllowControlledHouse, 0xA)
{
	GET(TechnoClass*, pNodeUnit, EDX);

	const auto pOwner = pNodeUnit->Owner;
	R->EAX(pOwner);

	return pOwner && pOwner->IsControlledByCurrentPlayer() ? 0x638F4F : 0x638F68;
}
