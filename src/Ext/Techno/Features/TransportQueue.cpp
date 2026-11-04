#include <CellClass.h>
#include <FootClass.h>
#include <MapClass.h>
#include <TeamClass.h>
#include <UnitClass.h>
#include <Unsorted.h>
#include <VoxClass.h>

#include <Ext/Rules/Body.h>
#include <Ext/Techno/Body.h>
#include <Ext/TechnoType/Body.h>
#include <New/Transport/QueuePolicy.h>
#include <Utilities/Macro.h>

namespace TransportQueue
{
	namespace
	{
		// Only synchronous, nested engine calls use this guard. Queue state lives
		// in the engine's QueueUpToEnter: already saved, swizzled and invalidated.
		FootClass* InternalDestination = nullptr;

		class DestinationGuard
		{
			FootClass* Previous;
		public:
			explicit DestinationGuard(FootClass* passenger)
				: Previous(InternalDestination) { InternalDestination = passenger; }
			~DestinationGuard() { InternalDestination = Previous; }
		};

		UnitClass* AsTransport(AbstractClass* target)
		{
			auto transport = abstract_cast<UnitClass*>(target);
			return transport && transport->Type->Passengers > 0 ? transport : nullptr;
		}

		Policy::Options GetOptions(UnitClass* transport)
		{
			// A missing transport is never enabled, even by global defaults.
			if (!transport)
				return {};

			const auto rules = RulesExt::Global();
			const Policy::Options global {
				rules && rules->Passengers_ImprovedQueue.Get(),
				rules && rules->Passengers_NoQueueEnter.Get(),
				rules && rules->Passengers_NoQueueUnload.Get(),
				rules ? rules->Passengers_NoQueueEnter_MaxCount.Get() : 0,
				rules ? rules->Passengers_NoQueueUnload_Delay.Get() : 0
			};
			const auto ext = TechnoTypeExt::ExtMap.Find(transport->Type);
			return Policy::ResolveOptions(ext, global);
		}

		Policy::EnterMode GetEnterMode(UnitClass* transport)
		{
			const auto options = GetOptions(transport);
			return Policy::GetEnterMode(options.ImprovedQueue, options.NoQueueEnter);
		}

		bool UsesEnterQueue(UnitClass* transport)
		{
			return GetEnterMode(transport) != Policy::EnterMode::Vanilla;
		}

		bool HasEnterMission(const FootClass* passenger);

		void CompactLoadingLinks(UnitClass* transport)
		{
			if (UsesEnterQueue(transport))
				Policy::CompactLinks(transport->RadioLinks.Items, transport->RadioLinks.Capacity);
		}

		bool IsReservedFor(TechnoClass* techno, UnitClass* transport)
		{
			auto pending = abstract_cast<FootClass*>(techno);
			if (!pending || !pending->IsAlive || pending->InLimbo || pending->Transporter
				|| !HasEnterMission(pending) || !pending->ContainsLink(transport))
				return false;
			const auto ext = TechnoExt::ExtMap.Find(pending);
			return ext && ext->TransportQueueReservation == transport
				&& (pending->Destination == transport || pending->QueueUpToEnter == transport);
		}

		bool HasEntrySlot(UnitClass* transport, FootClass* passenger)
		{
			return Policy::HasEntrySlot<TechnoClass>(passenger,
				transport->RadioLinks.Items, transport->RadioLinks.Capacity,
				GetOptions(transport).EnterMaxCount,
				[transport](TechnoClass* pending) { return IsReservedFor(pending, transport); });
		}

		bool ReserveCapacity(UnitClass* transport, FootClass* passenger)
		{
			if (!transport || !passenger)
				return false;

			const auto passengerExt = TechnoExt::ExtMap.Find(passenger);
			if (!passengerExt)
			{
				return false;
			}

			// Native/AI RequestLoading can arrive without our queued Update.
			// Enforce the same bound before recording admission on that path.
			if (!HasEntrySlot(transport, passenger))
			{
				return false;
			}

			CompactLoadingLinks(transport);
			const double incomingSize = passenger->GetTechnoType()->Size;
			if (incomingSize > transport->Type->SizeLimit)
			{
				return false;
			}

			const bool accepted = Policy::CanReserve<TechnoClass>(passenger,
				transport->RadioLinks.Items, transport->RadioLinks.Capacity,
				transport->Passengers.GetTotalSize(), transport->Type->Passengers,
				[transport](TechnoClass* techno)
				{
					return IsReservedFor(techno, transport);
				},
				[](TechnoClass* techno) { return techno->GetTechnoType()->Size; });

			passengerExt->TransportQueueReservation = accepted ? transport : nullptr;
			return accepted;
		}

		bool CanRequestLoading(UnitClass* transport, FootClass* passenger)
		{
			const auto ext = TechnoExt::ExtMap.Find(passenger);
			// A previously admitted vehicle must not lose its seat on a
			// repeated RequestLoading while its native maneuver is in progress.
			if (ext && ext->TransportQueueReservation == transport)
			{
				return true;
			}
			return ReserveCapacity(transport, passenger);
		}

		bool HasEnterMission(const FootClass* passenger)
		{
			// A newly queued player order wins over the old current mission.
			return Policy::HasEnterMission(passenger->CurrentMission, passenger->QueuedMission);
		}

		bool IsPassenger(const FootClass* passenger)
		{
			const auto type = passenger->WhatAmI();
			return type == AbstractType::Infantry || type == AbstractType::Unit;
		}

		bool IsNear(FootClass* passenger, UnitClass* transport, int radius)
		{
			const auto from = passenger->GetMapCoords();
			const auto to = transport->GetMapCoords();
			return Policy::IsNear(from.X - to.X, from.Y - to.Y,
				passenger->Location.Z - transport->Location.Z, radius, Unsorted::CellHeight);
		}

		std::int64_t Distance(FootClass* passenger, UnitClass* transport)
		{
			const auto from = passenger->GetMapCoords();
			const auto to = transport->GetMapCoords();
			return Policy::DistanceSquared(from.X - to.X, from.Y - to.Y);
		}

		bool IsWaiting(FootClass* passenger, UnitClass* transport)
		{
			return passenger && passenger->IsAlive && !passenger->InLimbo
				&& passenger->QueueUpToEnter == transport && HasEnterMission(passenger)
				&& !passenger->Transporter && IsPassenger(passenger)
				&& transport->Owner->IsAlliedWith(passenger);
		}

		void Approach(FootClass* passenger, UnitClass* transport)
		{
			// NearbyLocation is the same search used by vanilla QueueEnter
			// (70D8F0). Never use the transport's occupied cell as a waiting cell.
			if (passenger->IsTether || transport->OnBridge)
			{
				return;
			}

			if (passenger->Destination && passenger->Destination != transport)
			{
				const auto destination = passenger->Destination->GetCoords();
				const auto cell = CellClass::Coord2Cell(destination);
				const auto target = transport->GetMapCoords();
				if (Policy::IsNear(cell.X - target.X, cell.Y - target.Y, 0, 2, Unsorted::CellHeight))
				{
					return;
				}

				// Re-path a moving destination at a bounded, deterministic cadence.
				if (Unsorted::CurrentFrame % 15 != 0)
					return;
			}

			if (!passenger->Destination && IsNear(passenger, transport, 1))
			{
				return;
			}

			CellStruct cell = CellStruct::Empty;
			passenger->NearbyLocation(&cell, transport);
			if (cell == CellStruct::Empty || cell == transport->GetMapCoords())
			{
				// Some movement types consider the occupied transport cell legal.
				// Use the native occupancy/zone checks to find a WAITING cell,
				// rather than stacking queued vehicles on the transporter.
				const auto center = transport->GetMapCoords();
				const auto origin = passenger->GetMapCoords();
				const auto movementZone = passenger->GetTechnoType()->MovementZone;
				const int zone = MapClass::Instance.GetMovementZoneType(origin, movementZone, passenger->OnBridge);
				std::int64_t best = INT64_MAX;
				cell = CellStruct::Empty;
				for (int radius = 1; radius <= 2 && cell == CellStruct::Empty; ++radius)
				{
					for (int dx = -radius; dx <= radius; ++dx)
					{
						for (int dy = -radius; dy <= radius; ++dy)
						{
							if (dx != -radius && dx != radius && dy != -radius && dy != radius)
								continue;
							CellStruct candidate { short(center.X + dx), short(center.Y + dy) };
							auto target = MapClass::Instance.TryGetCellAt(candidate);
							if (!target || !MapClass::Instance.IsWithinUsableArea(target, true)
								|| MapClass::Instance.GetMovementZoneType(candidate, movementZone, passenger->OnBridge) != zone
								|| passenger->IsCellOccupied(target, FacingType::None, -1, passenger->GetCell(), true) != Move::OK)
								continue;
							const auto distance = Policy::DistanceSquared(candidate.X - origin.X, candidate.Y - origin.Y);
							if (distance < best)
							{
								cell = candidate;
								best = distance;
							}
						}
					}
				}
				if (cell == CellStruct::Empty)
				{
					return;
				}
			}

			auto destination = MapClass::Instance.GetCellAt(cell);
			if (passenger->Destination != destination && passenger->GetMapCoords() != cell)
			{
				DestinationGuard guard(passenger);
				passenger->SetDestination(destination, true);
			}
		}

		void WaitForEntrySlot(FootClass* passenger, UnitClass* transport)
		{
			passenger->SendCommand(RadioCommand::NotifyUnlink, transport);
			passenger->QueueUpToEnter = transport;
			passenger->SetArchiveTarget(nullptr);
			{
				DestinationGuard guard(passenger);
				passenger->SetDestination(nullptr, true);
			}
			Approach(passenger, transport);
		}

		FootClass* NearestWaiting(UnitClass* transport)
		{
			return Policy::Nearest(FootClass::Array.Items, FootClass::Array.Count,
				[transport](FootClass* passenger)
				{
					return IsWaiting(passenger, transport) && !passenger->Deactivated
						&& !passenger->IsUnderEMP() && !passenger->IsBeingWarpedOut();
				},
				[transport](FootClass* passenger) { return Distance(passenger, transport); });
		}

	}

	bool AssignDestination(FootClass* passenger, AbstractClass* target)
	{
		if (InternalDestination == passenger || !IsPassenger(passenger))
			return false;

		auto transport = AsTransport(target);
		if (const auto ext = TechnoExt::ExtMap.Find(passenger))
		{
			if (ext->TransportQueueReservation != target || !HasEnterMission(passenger))
				ext->TransportQueueReservation = nullptr;
		}
		if (!HasEnterMission(passenger))
		{
			// New move/attack/stop orders supersede our pending enter order.
			if (UsesEnterQueue(AsTransport(passenger->QueueUpToEnter)))
			{
				passenger->QueueUpToEnter = nullptr;
			}
			return false;
		}

		if (!transport || transport == passenger || !UsesEnterQueue(transport))
		{
			// Retargeting an opt-in queued order to a vanilla transport must
			// hand control back to the engine, without leaving the old queue.
			if (target && target != passenger->QueueUpToEnter
				&& UsesEnterQueue(AsTransport(passenger->QueueUpToEnter)))
				passenger->QueueUpToEnter = nullptr;
			return false;
		}

		if (passenger->ContainsLink(transport))
		{
			// An unadmitted native/AI link or a repeated order must not bypass
			// the entrance bound. Admitted maneuvers always retain their place.
			if (GetOptions(transport).NoQueueEnter && !HasEntrySlot(transport, passenger))
			{
				WaitForEntrySlot(passenger, transport);
				return true;
			}
			// Repeated clicks must not restart an in-progress boarding motion.
			return passenger->Destination == transport || passenger->IsTether;
		}

		if (passenger->HasAnyLink())
		{
			return false; // Preserve unrelated refinery/repair/tow radio protocols.
		}

		if (passenger->QueueUpToEnter != transport)
		{
			passenger->QueueUpToEnter = transport;
			passenger->SetArchiveTarget(nullptr);
			DestinationGuard guard(passenger);
			passenger->SetDestination(nullptr, true);
		}
		Approach(passenger, transport);
		return true;
	}

	void FocusLoadingLink(UnitClass* transport, TechnoClass* sender, RadioCommand command)
	{
		// Only simulation loading messages may change the primary link.
		// QueryCanEnter is also used by local mouse-over code and MUST stay
		// read-only here, or different cursors could desynchronise a game.
		if (!Policy::IsLoadingCommand(command))
			return;
		if (transport->Type->Passengers <= 0)
			return;
		if (!GetOptions(transport).NoQueueEnter)
			return;

		CompactLoadingLinks(transport);
		const int index = transport->FindLinkIndex(sender);
		Policy::FocusLink(transport->RadioLinks.Items, transport->RadioLinks.Capacity, index);
	}

	void Update(FootClass* passenger, UnitClass* transport)
	{
		if (!IsWaiting(passenger, transport))
		{
			passenger->QueueUpToEnter = nullptr;
			return;
		}

		if (passenger->Deactivated || passenger->IsUnderEMP() || passenger->IsBeingWarpedOut())
		{
			return;
		}

		Approach(passenger, transport);

		const auto mode = GetEnterMode(transport);
		const bool onLand = !transport->OnBridge && transport->GetCell()->LandType != LandType::Water;
		if (mode == Policy::EnterMode::Parallel && IsNear(passenger, transport, 2))
		{
			// A full concurrent entrance is a wait, not a capacity rejection.
			// Keep QueueUpToEnter/the nearby waiting destination and retry after
			// an admitted passenger boards or cancels. Do this before RequestLink.
			if (!HasEntrySlot(transport, passenger))
			{
				return;
			}

			// Each passenger keeps a real, reciprocal engine radio link and
			// follows the complete original approach/UpdatePosition path. More
			// links remove the serial reservation; they do NOT add seats.
			if (!transport->HasFreeLink())
				transport->SetLinkCount(transport->RadioLinks.Capacity + 1);

			DestinationGuard guard(passenger);
			reinterpret_cast<bool(__thiscall*)(FootClass*)>(0x70D7E0)(passenger);
			return;
		}

		if (auto linked = abstract_cast<FootClass*>(transport->GetNthLink()))
		{
			// Recover an old distant reservation (e.g. an AI/native order or an
			// existing save), but never interrupt a tethered/final approach.
			if (linked != passenger && !transport->IsTether && !linked->IsTether
				&& linked->Destination == transport && HasEnterMission(linked)
				&& !IsNear(linked, transport, 2) && IsNear(passenger, transport, 2)
				&& !transport->Destination)
			{
				linked->SendCommand(RadioCommand::NotifyUnlink, transport);
				linked->QueueUpToEnter = transport;
				linked->SetArchiveTarget(nullptr);
				DestinationGuard guard(linked);
				linked->SetDestination(nullptr, true);
				Approach(linked, transport);
			}
			else
			{
				return;
			}
		}

		// On water/bridges the original RequestLoading must be allowed to
		// move the transport to a boarding location. On land, reserve only
		// when a passenger is near the entrance, never while it is far away.
		// Check this before scanning Foot::Array: distant waiting groups
		// must not each run a global nearest-passenger search every frame.
		if (onLand && !IsNear(passenger, transport, 2))
		{
			return;
		}

		if (transport->HasAnyLink())
		{
			return;
		}
		if (const auto nearest = NearestWaiting(transport); nearest != passenger)
		{
			return;
		}

		// Execute vanilla UpdateEnter with its real QueryCanEnter, link and
		// RequestMoveTo exchanges. The synchronous guard avoids re-queuing
		// the SetDestination(transport) issued by that exchange.
		DestinationGuard guard(passenger);
		reinterpret_cast<bool(__thiscall*)(FootClass*)>(0x70D7E0)(passenger);
	}
}

// Hooks are past the prologues (away from Phobos's optional sync-log hooks).
// Both sites have EBP=this and EBX=destination; their native epilogues differ.
DEFINE_HOOK(0x741C4F, UnitClass_SetDestination_TransportQueue, 0x5)
{
	GET(FootClass*, passenger, EBP);
	GET(AbstractClass*, target, EBX);
	return TransportQueue::AssignDestination(passenger, target) ? 0x74317A : 0;
}

DEFINE_HOOK(0x51AC25, InfantryClass_SetDestination_TransportQueue, 0x5)
{
	GET(FootClass*, passenger, EBP);
	GET(AbstractClass*, target, EBX);
	return TransportQueue::AssignDestination(passenger, target) ? 0x51B1D7 : 0;
}

DEFINE_HOOK(0x4D929F, FootClass_MissionEnter_WaitNearTransport, 0x6)
{
	GET(FootClass*, passenger, ESI);
	return TransportQueue::UsesEnterQueue(TransportQueue::AsTransport(passenger->QueueUpToEnter)) ? 0x4D946C : 0;
}

DEFINE_HOOK(0x70D812, TechnoClass_UpdateEnter_TransportQueue, 0x5)
{
	GET(FootClass*, passenger, ESI);
	GET(AbstractClass*, target, EDI);
	if (passenger == TransportQueue::InternalDestination)
		return 0;
	if (auto transport = TransportQueue::AsTransport(target); TransportQueue::UsesEnterQueue(transport))
	{
		TransportQueue::Update(passenger, transport);
		return 0x70D8E2; // Native false-return epilogue, no stack args pending.
	}
	return 0;
}

// Native QueryCanEnter checks only passengers already inside. Reserve seats
// for other linked passengers already accepted by UpdateEnter, before this
// passenger is allowed to move into the transport's occupied cell.
DEFINE_HOOK(0x70D82F, TechnoClass_UpdateEnter_ReservePassengerCapacity, 0x5)
{
	GET(RadioCommand, answer, EAX);
	GET(FootClass*, passenger, ESI);
	GET(AbstractClass*, target, EDI);
	auto transport = TransportQueue::AsTransport(target);
	const auto vtable = *reinterpret_cast<DWORD*>(passenger);

	if (answer == RadioCommand::AnswerPositive
		&& TransportQueue::GetEnterMode(transport) == TransportQueue::Policy::EnterMode::Parallel)
	{
		if (!TransportQueue::HasEntrySlot(transport, passenger))
		{
			// A concurrent place is temporary: keep Enter and requeue rather
			// than running the native capacity-rejection/cancel tail.
			TransportQueue::WaitForEntrySlot(passenger, transport);
			R->EAX(vtable);
			return 0x70D8E2;
		}
		if (!TransportQueue::ReserveCapacity(transport, passenger))
		{
			// Use the native rejection tail to clear destination, queued entry
			// and both sides of the link; EAX must contain the passenger vtable.
			R->EAX(vtable);
			return 0x70D85E;
		}
	}

	// The hook replaces both `cmp eax, 1` and `mov eax, [esi]`; restore the
	// passenger vtable and branch directly to the corresponding vanilla path.
	R->EAX(vtable);
	return answer == RadioCommand::AnswerPositive ? 0x70D836 : 0x70D85E;
}

// RadioClass::HasAnyLink scans the entire array, but the following vanilla
// code reads only slot zero. Parallel boarding plus native unlink can leave
// holes, so restore the packed-prefix invariant before GetNthLink(0).
DEFINE_HOOK(0x741AEC, UnitClass_AssignDestination_CompactLoadingLinks, 0x9)
{
	GET(UnitClass*, transport, EBP);
	TransportQueue::CompactLoadingLinks(transport);
	R->EAX(transport->GetNthLink());
	return 0x741AF5;
}

// A repeated order can leave a vehicle already stopped on the transport cell.
// In that state there will be no further locomotor arrival notification. Run
// the complete native arrival handler once; do not Limbo/AddPassenger manually.
DEFINE_HOOK(0x4D92B2, FootClass_MissionEnter_CompleteStationaryArrival, 0x5)
{
	GET(FootClass*, passenger, ESI);
	GET(AbstractClass*, target, EAX);
	auto transport = TransportQueue::AsTransport(target);
	if (TransportQueue::UsesEnterQueue(transport) && TransportQueue::IsPassenger(passenger)
		&& passenger->Destination == transport && passenger->ContainsLink(transport)
		&& passenger->GetMapCoords() == transport->GetMapCoords()
		&& !passenger->Locomotor->Is_Moving())
	{
		passenger->UpdatePosition(PCPType::End);
		return 0x4D946C;
	}
	return 0;
}

DEFINE_HOOK(0x4D92BF, FootClass_MissionEnter_TransportMoving, 0x5)
{
	GET(FootClass*, passenger, ESI);
	GET(RadioCommand, answer, EAX);
	auto transport = TransportQueue::AsTransport(passenger->GetNthLink());
	if (!TransportQueue::UsesEnterQueue(transport))
		return 0;
	if (passenger->IsTether || answer == RadioCommand::AnswerPositive)
		return 0x4D92ED;
	return answer == RadioCommand::RequestLoading ? 0x4D946C : 0x4D92CE;
}

// Success-only site, BEFORE Phobos's 73DC1E hook. Failure still follows the
// untouched 73DC71 reinsert/Undiscover path and returns without looping.
DEFINE_HOOK(0x73DC0C, UnitClass_MissionUnload_TransportQueue, 0x8)
{
	GET(UnitClass*, transport, ESI);
	GET(FootClass*, passenger, EDI);
	const auto options = TransportQueue::GetOptions(transport);
	if (!options.NoQueueUnload)
		return 0;

	// These are the original successful-unload side effects at 73DC0C..
	// 73DC6C. Keep the original per-passenger sound and team admission.
	if (transport->Team)
		transport->Team->AddMember(passenger, false);
	if (transport->Type->LeaveTransportSound != -1)
		VoxClass::PlayAtPos(transport->Type->LeaveTransportSound, &transport->Location);

	R->EBX(0); // Vanilla's zero register, overwritten during the prior pass.
	switch (TransportQueue::Policy::AfterSuccessfulUnload(transport->Passengers.NumPassengers,
		transport->NonPassengerCount, options.UnloadDelay))
	{
	case TransportQueue::Policy::UnloadAction::ContinueNow:
		return 0x73D8B7;
	case TransportQueue::Policy::UnloadAction::Wait:
		// MissionClass::AI stores the returned frame count in its native
		// UpdateTimer. Keep case 3 so the next expiry unloads one passenger.
		// Bypass the normal Rate*900 + random delay, preserving exact frames.
		R->EAX(options.UnloadDelay);
		return 0x73E2B7; // Original pop edi/esi/ebp/ebx; add esp,70h; ret.
	default:
		return 0x73E289;
	}
}

// Native NotifyBeginLoad uses SendToFirstLink(RequestTether). With parallel
// loading links its first link must be the actual caller, not a previous unit.
DEFINE_HOOK(0x73743B, UnitClass_ReceiveCommand_TransportQueue, 0x7)
{
	GET(UnitClass*, transport, ECX);
	GET_STACK(TechnoClass*, sender, 0x38);
	GET(RadioCommand, command, EBP);
	if (command == RadioCommand::NotifyUnlink)
	{
		if (const auto ext = TechnoExt::ExtMap.Find(sender);
			ext && ext->TransportQueueReservation == transport)
			ext->TransportQueueReservation = nullptr;
	}
	else if (command == RadioCommand::RequestLoading && transport->Type->Passengers > 0
		&& TransportQueue::GetEnterMode(transport) == TransportQueue::Policy::EnterMode::Parallel)
	{
		auto passenger = abstract_cast<FootClass*>(sender);
		if (passenger && TransportQueue::IsPassenger(passenger) && TransportQueue::HasEnterMission(passenger))
		{
			const auto answer = passenger->SendCommand(RadioCommand::QueryCanEnter, transport);
			if (answer == RadioCommand::AnswerPositive && !TransportQueue::CanRequestLoading(transport, passenger))
				return 0x73780F; // Original AnswerNegative epilogue, with no pending stack arguments.
		}
	}
	TransportQueue::FocusLoadingLink(transport, sender, command);
	return 0;
}

// Vanilla picks a building/aircraft/unit from the occupied cell, then skips
// only one object if that happens to be the passenger itself (73A63C). With
// concurrent arrivals this may leave ESI pointing at another passenger, so
// the native ContainsLink test skips boarding altogether. Use the ordered,
// linked transport in this cell, then execute every original arrival check.
DEFINE_HOOK(0x73A64A, UnitClass_UpdatePosition_TransportQueueTarget, 0x5)
{
	GET(FootClass*, passenger, EBP);
	if (!passenger || passenger->CurrentMission != Mission::Enter
		|| !TransportQueue::HasEnterMission(passenger) || !passenger->IsAlive
		|| passenger->InLimbo || passenger->Transporter)
		return 0;

	auto transport = TransportQueue::AsTransport(passenger->Destination);
	if (!TransportQueue::UsesEnterQueue(transport) || passenger == transport
		|| !transport->IsAlive || transport->InLimbo
		|| passenger->OnBridge != transport->OnBridge
		|| passenger->GetMapCoords() != transport->GetMapCoords()
		|| !passenger->ContainsLink(transport))
		return 0;

	R->ESI(transport);
	// Replay mov eax,[ebp]; mov ecx,ebp. The native mission, cell, link,
	// QueryCanEnter, success and rejection branches all follow unchanged.
	return 0;
}
