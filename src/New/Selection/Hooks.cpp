#include "Selection.h"

#include <UnitClass.h>
#include <TacticalClass.h>
#include <Helpers/Macro.h>

// The second, inlined registration path in TacticalClass's visible-Foot loop.
// EBP is already relative X; EDI is still absolute Y before 6D9E5A.
DEFINE_HOOK(0x6D9E54, TacticalClass_RegisterFoot_DynamicSelection, 0x6)
{
	if (!SelectionService::Enabled())
		return 0;

	GET(TacticalClass*, tactical, ESI);
	GET(int, x, EBP);
	GET(int, y, EDI);
	GET_STACK(ObjectClass*, object, 0x14);
	SelectionService::AddSelectable(tactical, object, x, y - tactical->TacticalPos.Y);
	return 0x6D9ECA;
}

// Ownership changes have already cleared the previous selection here. Both
// native insertion branches are downstream, including selection-group recall.
DEFINE_HOOK(0x5F45FD, ObjectClass_Select_SelectionLimit, 0x8)
{
	if (!SelectionService::Enabled())
		return 0;

	if (!SelectionService::CanSelect())
		return 0x5F454E;
	return R->ESI<ObjectClass*>() ? 0x5F4605 : 0x5F46BB;
}

DEFINE_HOOK(0x69265A, DisplayClass_DecideAction_MixedDeployedMove, 0x6)
{
	GET(ObjectClass*, original, EAX);
	GET(const CellStruct*, cell, ESI);
	GET_STACK(bool, checkFog, 0x2C);
	R->EAX(SelectionService::GetMoveRepresentative(original, cell, checkFog));
	return 0;
}

DEFINE_HOOK(0x653C93, RadarClass_Click_MixedDeployedMove, 0x7)
{
	GET(ObjectClass*, original, EAX);
	LEA_STACK(const CellStruct*, cell, 0x18);
	R->EAX(SelectionService::GetMoveRepresentative(original, cell, false));
	return 0;
}

// Every native event constructor obtains room before its caller performs an
// inlined OutList.Add. Keep the native 128-slot queue and packet layout intact.
DEFINE_HOOK_AGAIN(0x4C65A0, EventClass_Construct_BufferSelectionCommands, 0x6)
DEFINE_HOOK_AGAIN(0x4C65E0, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6650, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C66C0, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6720, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6780, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6800, EventClass_Construct_BufferSelectionCommands, 0x8)
DEFINE_HOOK_AGAIN(0x4C68E0, EventClass_Construct_BufferSelectionCommands, 0x8)
DEFINE_HOOK_AGAIN(0x4C6970, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C69E0, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6A60, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6AE0, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6B60, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6BE0, EventClass_Construct_BufferSelectionCommands, 0x5)
DEFINE_HOOK_AGAIN(0x4C6C50, EventClass_Construct_BufferSelectionCommands, 0x7)
DEFINE_HOOK(0x4C6860, EventClass_Construct_BufferSelectionCommands, 0x6)
{
	if (!SelectionService::Enabled())
		return 0;

	SelectionService::BeforeEvent();
	return 0;
}

// Shared Queue_AI entry BEFORE the mode switch. Campaign (0) and skirmish (5)
// drain OutList directly in this function; they NEVER call the network-only
// 6475F0 entry. Pump here so buffered commands reach both native branches.
DEFINE_HOOK(0x647260, Game_QueueAI_PumpSelectionCommands, 0x5)
{
	if (!SelectionService::Enabled())
		return 0;

	SelectionService::PumpCommands();
	return 0;
}

// FootClass::ClickedMission case 2/62 calls FindReachableCell using the
// current Walk locomotor. A grounded Jumpjet infantry is airborne-capable,
// so keep the explicit click destination and let its Jumpjet locomotor ascend.
DEFINE_HOOK(0x4D8065, FootClass_CellClick_GroundedJumpjetDestination, 0x6)
{
	if (!SelectionService::Enabled())
		return 0;
	GET(FootClass*, foot, ESI);
	GET(const CellStruct*, requested, EDI);
	LEA_STACK(CellStruct*, destination, 0x10);
	if (!SelectionService::UseGroundedFlightDestination(foot, requested, destination))
		return 0;
	R->EAX(destination);
	return 0x4D8074;
}
