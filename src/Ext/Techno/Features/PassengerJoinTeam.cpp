#include <FootClass.h>
#include <TeamClass.h>
#include <TechnoTypeClass.h>
#include <Unsorted.h>

#include <Ext/Techno/Body.h>
#include <Ext/TechnoType/Body.h>

void TechnoExt::ProcessPassengerJoinTeam(FootClass* pFoot)
{
	if (!pFoot || pFoot->Passengers.NumPassengers <= 0)
		return;

	TeamClass* pTeam = pFoot->Team;
	if (!pTeam || !pTeam->Type)
		return;

	auto pTypeExt = TechnoTypeExt::ExtMap.Find(pFoot->GetTechnoType());
	if (!pTypeExt || !pTypeExt->PassengersJoinTeam)
		return;

	for (FootClass* pPassenger = pFoot->Passengers.GetFirstPassenger();
		pPassenger;
		pPassenger = abstract_cast<FootClass*>(pPassenger->NextObject))
	{
		if (pPassenger->Team)
			continue;

		pPassenger->RecruitableA = true;
		pPassenger->RecruitableB = true;

		const int oldInit = Unsorted::ScenarioInit;
		Unsorted::ScenarioInit = 1;
		
		bool forced = false;
		bool added = pTeam->AddMember(pPassenger, false);
		if (!added)
		{
			forced = true;
			added = pTeam->AddMember(pPassenger, true);
		}

		Unsorted::ScenarioInit = oldInit;

		if (added && forced)
			--pTeam->TotalObjects;

		pPassenger->RecruitableA = pFoot->RecruitableA;
		pPassenger->RecruitableB = pFoot->RecruitableB;
	}
}
