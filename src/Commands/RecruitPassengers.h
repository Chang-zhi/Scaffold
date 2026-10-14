#pragma once

#include <YRPP.h>
#include <FootClass.h>
#include <UnitClass.h>
#include <BuildingClass.h>
#include <InfantryClass.h>
#include <HouseClass.h>
#include <CellSpread.h>
#include <Helpers/Cast.h>
#include <Ext/Rules/Body.h>
#include <Utilities/Debug.h>
#include <Utilities/SyncState.h>

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

#include <EventClass.h>
#include <TargetClass.h>

#include "Command.h"
#include <Ext/Event/Body.h>

using EventClassTargetCtor = void* (__thiscall*)(void*, int, EventType, int, int);

static const EventClassTargetCtor EventClass_TargetCtor =
	reinterpret_cast<EventClassTargetCtor>(0x4C65E0);

static bool IsOrderQueueBlocked()
{
	return *reinterpret_cast<const unsigned char*>(0xAC4CF4) != 0;
}

static bool QueueSelfOrder(TechnoClass* pTechno, EventType eType)
{
	HouseClass* const pLocal = HouseClass::CurrentPlayer;
	if (!pTechno || !pLocal || !pTechno->Owner || !pTechno->IsAlive || pTechno->InLimbo)
		return false;

	if (IsOrderQueueBlocked())
		return false;

	const TargetClass whom(pTechno);

	alignas(EventClass) unsigned char buffer[sizeof(EventClass)] { };

	EventClass_TargetCtor(buffer, pLocal->ArrayIndex, eType, whom.m_ID, whom.m_RTTI);

	if (!EventClass::OutList.Add(*reinterpret_cast<EventClass*>(buffer)))
	{
		Debug::Log("Recruit: event out queue full, dropped event type %d\n", static_cast<int>(eType));
		return false;
	}

	return true;
}

static bool QueueBoardOrder(TechnoClass* pPassenger, TechnoClass* pTransport)
{
	auto const pSender = HouseClass::CurrentPlayer;
	if (!pPassenger || !pTransport || pPassenger == pTransport || !abstract_cast<FootClass*>(pPassenger)
		|| !EventExt::CanControl(pSender, pPassenger) || IsOrderQueueBlocked()
		|| !pPassenger->IsAlive || pPassenger->InLimbo
		|| !pTransport->IsAlive || pTransport->InLimbo)
		return false;

	bool undeploy = false;
	if (auto pUnit = abstract_cast<UnitClass*>(pPassenger))
		undeploy = pUnit->Deployed;
	else if (auto pInf = abstract_cast<InfantryClass*>(pPassenger))
		undeploy = pInf->IsDeployed();

	const int count = undeploy ? 2 : 1;
	return SyncState::QueueTransaction(EventClass::OutList, count, EventClass::MAX_EVENTS, [&]()
		{
			if (undeploy && !QueueSelfOrder(pPassenger, EventType::Deploy))
				return false;
			return pPassenger->ObjectClickedAction(Action::Enter, pTransport, false);
		});
}

static void SortByCanonicalOrder(std::vector<TechnoClass*>& list)
{
	if (list.size() < 2)
		return;

	std::unordered_map<TechnoClass*, int> rank;
	rank.reserve(TechnoClass::Array.Count);

	for (int i = 0; i < TechnoClass::Array.Count; ++i)
	{
		if (TechnoClass* pTechno = TechnoClass::Array.GetItem(i))
			rank.emplace(pTechno, i);
	}

	std::stable_sort(list.begin(), list.end(), [&rank](TechnoClass* a, TechnoClass* b)
		{
			if (a == b)
				return false;

			const int ia = rank.contains(a) ? rank[a] : INT_MAX;
			const int ib = rank.contains(b) ? rank[b] : INT_MAX;

			if (ia != ib)
				return ia < ib;

			return a < b;	
		});
}

struct TransportInfo
{
	CellStruct Cell;	
	int UsedCapacity;	
	int MaxCapacity;	
};

struct RecruitCandidate
{
	TechnoClass* Unit;
	int Size;			
	CellStruct Cell;	
	bool RangeLimited;	
	bool Assigned;		
};

struct RecruitState
{
	std::unordered_set<TechnoClass*> Scattered;	
	std::unordered_set<TechnoClass*> Departing;	
	std::unordered_map<TechnoClass*, int> Granted;
};

namespace RecruitDetail
{
	inline bool IsCommandableByLocalPlayer(TechnoClass* pTechno, HouseClass* pLocal)
	{
		return pTechno && pLocal
			&& (pTechno->IsControllable() || EventExt::CanControl(pLocal, pTechno));
	}

	inline void ScatterFriendlyCell(
		CellStruct cell,
		HouseClass* pLocal,
		TechnoClass* transportRoot,
		const std::unordered_set<TechnoClass*>& whitelist,
		std::unordered_set<TechnoClass*>& scattered)
	{
		CellClass* pCell = MapClass::Instance.TryGetCellAt(cell);
		if (!pCell)
			return;

		for (ObjectClass* pObj = pCell->GetContent(); pObj; pObj = pObj->NextObject)
		{
			TechnoClass* pTech = abstract_cast<TechnoClass*>(pObj);
			if (!pTech || !pTech->IsAlive || pTech->InLimbo || pTech->Transporter)
				continue;
			if (!IsCommandableByLocalPlayer(pTech, pLocal))
				continue;
			if (whitelist.contains(pTech))
				continue;
			if (scattered.contains(pTech))
				continue;

			if (FootClass* pFoot = abstract_cast<FootClass*>(pTech))
			{
				if (pFoot->Destination == transportRoot)
					continue;
			}

			if (!QueueSelfOrder(pTech, EventType::Scatter))
			{
				Debug::Log("Recruit: cannot queue Scatter (queue full / planning mode), stop scattering cell\n");
				return;
			}

			scattered.insert(pTech);
		}
	}

	inline void CollectCanonicalOrder(
		const std::unordered_map<TechnoClass*, TransportInfo>& transports,
		std::vector<TechnoClass*>& out)
	{
		out.clear();
		out.reserve(transports.size());

		for (int i = 0; i < TechnoClass::Array.Count; ++i)
		{
			TechnoClass* pTechno = TechnoClass::Array.GetItem(i);
			if (pTechno && transports.contains(pTechno))
				out.push_back(pTechno);
		}
	}

	inline void CollectWhitelist(
		std::unordered_set<TechnoClass*>& whitelist,
		TechnoClass* transport)
	{
		if (!transport || whitelist.contains(transport))
			return;

		whitelist.insert(transport);

		for (FootClass* pPass = transport->Passengers.GetFirstPassenger();
			pPass; pPass = pPass->NextTeamMember)
		{
			CollectWhitelist(whitelist, pPass);
		}
	}

	inline bool IsHeadingToTransport(FootClass* pFoot)
	{
		if (!pFoot->Destination)
			return false;

		TechnoClass* pDest = abstract_cast<TechnoClass*>(pFoot->Destination);
		return pDest && pDest != pFoot && pDest->WhatAmI() == AbstractType::Unit;
	}

	inline bool IsUsableTransport(TechnoClass* pTechno, TransportInfo& outInfo)
	{
		if (pTechno->WhatAmI() != AbstractType::Unit)
			return false;

		TechnoTypeClass* pType = pTechno->GetTechnoType();
		if (!pType || pType->Passengers <= 0)
			return false;
		if (!pTechno->IsControllable() || pTechno->Deactivated || pTechno->IsUnderEMP())
			return false;

		const int used = pTechno->Passengers.GetTotalSize();
		if (used >= static_cast<int>(pType->Passengers))
			return false;

		outInfo = TransportInfo{
			CellClass::Coord2Cell(pTechno->GetCoords()), used, static_cast<int>(pType->Passengers) };
		return true;
	}

	inline void CollectCandidates(
		const std::vector<TechnoClass*>& selectedNonTransports,
		const std::unordered_set<TechnoClass*>& selectedSet,
		HouseClass* pPlayer,
		std::vector<RecruitCandidate>& out)
	{
		out.clear();

		for (TechnoClass* pUnit : selectedNonTransports)
		{
			if (!pUnit->IsAlive || pUnit->InLimbo || pUnit->Transporter)
				continue;
			if (pUnit->WhatAmI() == AbstractType::AircraftType || pUnit->IsInAir())
				continue;

			TechnoTypeClass* pType = pUnit->GetTechnoType();
			if (!pType || pType->ConsideredAircraft)
				continue;

			int size = static_cast<int>(pType->Size);
			if (size <= 0)
				size = 1;

			out.push_back(RecruitCandidate{
				pUnit, size, pUnit->GetMapCoords(), false, false });
		}

		for (FootClass* pFoot : FootClass::Array)
		{
			if (!pFoot || !pFoot->IsAlive || pFoot->InLimbo || pFoot->Transporter)
				continue;
			if (!IsCommandableByLocalPlayer(pFoot, pPlayer))
				continue;

			TechnoTypeClass* pType = pFoot->GetTechnoType();
			if (!pType || pType->ConsideredAircraft)
				continue;
			if (pFoot->WhatAmI() == AbstractType::AircraftType || pFoot->IsInAir())
				continue;

			if (pType->Passengers > 0)
				continue;

			if (selectedSet.contains(pFoot))
				continue;
			if (IsHeadingToTransport(pFoot))
				continue;

			int size = static_cast<int>(pType->Size);
			if (size <= 0)
				size = 1;

			out.push_back(RecruitCandidate{
				pFoot, size, pFoot->GetMapCoords(), true, false });
		}
	}

	int TryAssign(
		const std::vector<TechnoClass*>& sortedVehicles,
		const std::unordered_map<TechnoClass*, int>& initialCapacities,
		std::vector<RecruitCandidate>& candidates,
		RecruitState& state)
	{
		const double recruitRange = RulesExt::Global()->Command_RecruitRange;
		int totalRecruited = 0;

		std::vector<int> pool;
		pool.reserve(candidates.size());

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			const RecruitCandidate& c = candidates[i];

			if (c.Assigned || !c.Unit->IsAlive || c.Unit->InLimbo || c.Unit->Transporter)
				continue;

			pool.push_back(static_cast<int>(i));
		}

		if (pool.empty())
			return 0;

		std::vector<int> distance(pool.size(), -1);

		size_t groupStart = 0;
		while (groupStart < sortedVehicles.size())
		{
			TechnoClass* pGroupHead = sortedVehicles[groupStart];
			TechnoTypeClass* pHeadType = pGroupHead->GetTechnoType();
			const double groupLimit = pHeadType ? pHeadType->SizeLimit : 0.0;

			size_t groupEnd = groupStart + 1;
			while (groupEnd < sortedVehicles.size())
			{
				TechnoTypeClass* pNextType = sortedVehicles[groupEnd]->GetTechnoType();
				if (!pNextType || pNextType->SizeLimit != groupLimit)
					break;
				groupEnd++;
			}

			bool anyAssigned = true;
			while (anyAssigned)
			{
				anyAssigned = false;

				for (size_t vi = groupStart; vi < groupEnd; ++vi)
				{
					TechnoClass* pCurVeh = sortedVehicles[vi];
					TechnoTypeClass* pVehType = pCurVeh->GetTechnoType();
					auto itInitial = initialCapacities.find(pCurVeh);

					if (!pVehType || itInitial == initialCapacities.end())
						continue;

					const int room = static_cast<int>(pVehType->Passengers)
						- itInitial->second - state.Granted[pCurVeh];
					if (room <= 0)
						continue;

					const CellStruct vehCell = pCurVeh->GetMapCoords();

					for (size_t s = 0; s < pool.size(); ++s)
					{
						const RecruitCandidate& c = candidates[pool[s]];
						const int dist = CellSpread::GetDistance(CellStruct{
							static_cast<short>(c.Cell.X - vehCell.X),
							static_cast<short>(c.Cell.Y - vehCell.Y)
						});

						distance[s] = (!c.RangeLimited || dist <= recruitRange) ? dist : -1;	// -1 = 够不着
					}

					int bestSlot = -1;
					int bestDist = INT_MAX;
					int bestSize = INT_MAX;

					for (size_t s = 0; s < pool.size(); ++s)
					{
						const int dist = distance[s];
						if (dist < 0)
							continue;

						const RecruitCandidate& c = candidates[pool[s]];
						if (c.Assigned || c.Unit == pCurVeh || c.Size > room)
							continue;
						if (c.Size > static_cast<int>(pVehType->SizeLimit))
							continue;
						if (state.Departing.contains(c.Unit))
							continue;

						if (dist < bestDist || (dist == bestDist && c.Size < bestSize))
						{
							bestDist = dist;
							bestSize = c.Size;
							bestSlot = static_cast<int>(s);
						}
					}

					if (bestSlot < 0)
						continue;

					RecruitCandidate& picked = candidates[pool[bestSlot]];

					if (!QueueBoardOrder(picked.Unit, pCurVeh))
						continue;

					state.Granted[pCurVeh] += picked.Size;
					picked.Assigned = true;
					state.Departing.insert(picked.Unit);
					totalRecruited++;
					anyAssigned = true;
				}
			}

			groupStart = groupEnd;
		}

		return totalRecruited;
	}
}

class AutoPassengersLoad : public AresCommandClass
{
public:
	virtual const char* GetName() const override
	{
		return "Auto_passengers_load";
	}

	virtual const wchar_t* GetUIName() const override
	{
		const wchar_t* textPtr
			= StringTable::TryFetchString("CMND:UINAME_AUTOLOAD", L"自动装载 Auto passengers load");

		return textPtr;
	}

	virtual const wchar_t* GetUICategory() const override
	{
		const wchar_t* textPtr = StringTable::TryFetchString("CMND:UICATEGORY_SCAFFOLD");

		if (!textPtr || !*textPtr)
			textPtr = L"Scaffold";

		return textPtr;
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		const wchar_t* textPtr
			= StringTable::TryFetchString("CMND:UIDESCR_AUTOLOAD"
				, L"选中空载具按指定按键，自动招募附近单位上车\nRecruit nearby units to board selected empty transports");

		return textPtr;
	}

	virtual void Execute(WWKey eInput) const override
	{
		HouseClass* const pPlayer = HouseClass::CurrentPlayer;
		if (!pPlayer)
			return;

		std::vector<ObjectClass*> selected;

		for (ObjectClass* pObj : ObjectClass::CurrentObjects)
		{
			if (!pObj || !pObj->IsSelected || !pObj->IsAlive || pObj->InLimbo)
				continue;

			selected.push_back(pObj);
		}

		if (selected.empty())
			return;

		RunRecruit(pPlayer, selected);

		if (selected.size() > EventExt::MAX_SELECTION)
		{
			Debug::Log("Auto passengers load: %zu selected, event keeps first %zu\n",
				selected.size(), EventExt::MAX_SELECTION);
			selected.resize(EventExt::MAX_SELECTION);
		}

		EventExt::RaiseRecruitPassengers(pPlayer, selected);
	}

	static void RunRecruit(HouseClass* pPlayer, const std::vector<ObjectClass*>& selected)
	{
		std::unordered_map<TechnoClass*, TransportInfo> transports;
		std::vector<TechnoClass*> selectedNonTransports;
		std::unordered_set<TechnoClass*> selectedSet;

		for (ObjectClass* pObj : selected)
		{
			if (!pObj || !pObj->IsAlive || pObj->InLimbo)
				continue;

			TechnoClass* pTechno = abstract_cast<TechnoClass*>(pObj);
			if (!pTechno || !RecruitDetail::IsCommandableByLocalPlayer(pTechno, pPlayer))
				continue;

			selectedSet.insert(pTechno);

			TransportInfo info;
			if (RecruitDetail::IsUsableTransport(pTechno, info))
				transports[pTechno] = info;
			else
				selectedNonTransports.push_back(pTechno);
		}

		if (transports.empty())
			return;

		SortByCanonicalOrder(selectedNonTransports);

		std::vector<TechnoClass*> sortedKeys;
		RecruitDetail::CollectCanonicalOrder(transports, sortedKeys);

		std::stable_sort(sortedKeys.begin(), sortedKeys.end(), [](TechnoClass* a, TechnoClass* b) {
			return a->GetTechnoType()->SizeLimit < b->GetTechnoType()->SizeLimit;
		});

		std::unordered_map<TechnoClass*, int> initialCapacities;
		initialCapacities.reserve(transports.size());
		for (TechnoClass* pVeh : sortedKeys)
			initialCapacities[pVeh] = transports[pVeh].UsedCapacity;

		std::vector<RecruitCandidate> candidates;
		RecruitDetail::CollectCandidates(selectedNonTransports, selectedSet, pPlayer, candidates);

		RecruitState state;
		state.Scattered.reserve(sortedKeys.size() * 4);
		state.Departing.reserve(candidates.size());
		state.Granted.reserve(sortedKeys.size());

		std::unordered_set<TechnoClass*> whitelist;
		whitelist.reserve(8);

		for (TechnoClass* pVeh : sortedKeys)
		{
			whitelist.clear();
			RecruitDetail::CollectWhitelist(whitelist, pVeh);

			RecruitDetail::ScatterFriendlyCell(
				transports[pVeh].Cell, pPlayer, pVeh, whitelist, state.Scattered);
		}

		int totalRecruited = RecruitDetail::TryAssign(
			sortedKeys, initialCapacities, candidates, state);

		std::vector<TechnoClass*> failedTransports;
		bool hasSuccessful = false;

		for (TechnoClass* pVeh : sortedKeys)
		{
			const int granted = state.Granted[pVeh];

			if (granted > 0)
			{
				hasSuccessful = true;
				continue;
			}

			if (pVeh->IsAlive && !pVeh->InLimbo && !pVeh->Transporter)
				failedTransports.push_back(pVeh);
		}

		if (failedTransports.empty())
			return;

		if (hasSuccessful)
		{
			std::vector<RecruitCandidate> emptyTransports;
			emptyTransports.reserve(failedTransports.size());

			for (TechnoClass* pVeh : failedTransports)
			{
				TechnoTypeClass* pType = pVeh->GetTechnoType();
				if (!pType)
					continue;

				int size = static_cast<int>(pType->Size);
				if (size <= 0)
					size = 1;

				emptyTransports.push_back(RecruitCandidate{
					pVeh, size, pVeh->GetMapCoords(), false, false });
			}

			if (!emptyTransports.empty())
				totalRecruited += RecruitDetail::TryAssign(
					sortedKeys, initialCapacities, emptyTransports, state);

			return;
		}

		if (failedTransports.size() < 2)
			return;
		
		TechnoClass* pReceiver = nullptr;
		int bestRemaining = 0;

		for (TechnoClass* pVeh : sortedKeys)
		{
			const TransportInfo& info = transports[pVeh];
			const int rem = info.MaxCapacity - info.UsedCapacity - state.Granted[pVeh];

			if (rem > bestRemaining)
			{
				bestRemaining = rem;
				pReceiver = pVeh;
			}
		}

		if (!pReceiver)
			return;

		TechnoTypeClass* pReceiverType = pReceiver->GetTechnoType();
		if (!pReceiverType)
			return;

		for (TechnoClass* pFailed : failedTransports)
		{
			if (pFailed == pReceiver)
				continue;

			TechnoTypeClass* pFailedType = pFailed->GetTechnoType();
			if (!pFailedType)
				continue;

			int failedSize = static_cast<int>(pFailedType->Size);
			if (failedSize <= 0)
				failedSize = 1;

			const int room = transports[pReceiver].MaxCapacity
				- transports[pReceiver].UsedCapacity - state.Granted[pReceiver];

			if (failedSize > room)
				continue;

			if (failedSize > static_cast<int>(pReceiverType->SizeLimit))
				continue;

			if (!QueueBoardOrder(pFailed, pReceiver))
				continue;
			state.Granted[pReceiver] += failedSize;
			totalRecruited++;
		}

		(void)totalRecruited;
	}
};
