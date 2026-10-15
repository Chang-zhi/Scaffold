#include <ObjectClass.h>
#include <HouseClass.h>

#include <Ext/Rules/Body.h>
#include <Helpers/Macro.h>

DEFINE_HOOK(0x6927EE, DisplayClass_DecideAction_AllowAlliedRepair, 0x9)
{
	enum { ContinueVanillaCheck = 0x6927F7, ReturnNoRepair = 0x69280D, };

	GET(ObjectClass*, pTarget, ESI);

	const auto pOwner = pTarget ? pTarget->GetOwningHouse() : nullptr;

	if (pOwner && pOwner->IsControlledByCurrentPlayer())
		return ContinueVanillaCheck;

	const auto pRulesExt = RulesExt::Global();

	if (pOwner
		&& pRulesExt
		&& pRulesExt->AllowRepairAlliedBuildings
		&& pOwner->IsAlliedWith(HouseClass::CurrentPlayer))
	{
		return ContinueVanillaCheck;
	}

	return ReturnNoRepair;
}
