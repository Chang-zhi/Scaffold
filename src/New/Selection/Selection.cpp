#include "Selection.h"
#include "NativeSelection.h"
#include "SelectionPolicy.h"
#include "CommandBuffer.h"

#include <EventClass.h>
#include <HouseClass.h>
#include <ObjectClass.h>
#include <Surface.h>
#include <SessionClass.h>
#include <TacticalClass.h>
#include <TechnoClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <JumpjetLocomotionClass.h>
#include <Unsorted.h>

#include <Ext/Rules/Body.h>
#include <Ext/Event/Body.h>
#include <Ext/TechnoType/Body.h>
#include <Utilities/AresHelper.h>
#include <Utilities/Debug.h>
#include <Utilities/Patch.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <deque>
#include <vector>

namespace
{
	constexpr int NativeCapacity = 500;
	constexpr int CommandBatchSize = 96; // Leave room for native frame/timing events.
	using Candidate = TacticalSelectableStruct;
	using SelectCallback = bool(__fastcall*)(ObjectClass*);
	using MakeSelectionFn = void(__fastcall*)(TacticalClass*, void*, SelectCallback);

	std::vector<Candidate> Candidates;
	MakeSelectionFn PreviousMakeSelection = nullptr;
	bool Installed = false;
	bool Paging = false;
	std::vector<Candidate>* ActiveSnapshot = nullptr;
	Candidate* CurrentView = nullptr;
	struct EventIdentity
	{
		bool operator()(const EventClass& a, const EventClass& b) const
		{
			// Native packet construction can refresh Frame/IsExecuted/HouseIndex
			// before a full destination queue postpones consumption of the head.
			return a.Type == b.Type && !std::memcmp(a.DataBuffer, b.DataBuffer, sizeof(a.DataBuffer));
		}
	};
	Selection::CommandBuffer<EventClass, EventClass::MAX_EVENTS, CommandBatchSize, EventIdentity> Commands;
	Selection::Atomic::Receiver Receiver;
	std::uint32_t NextBatchID = 0;
	struct SavedPatch
	{
		DWORD Address;
		std::vector<BYTE> Bytes;
	};
	std::vector<SavedPatch> SavedPatches;

	void SavePatch(DWORD address, size_t size)
	{
		const auto bytes = reinterpret_cast<const BYTE*>(address);
		SavedPatches.push_back({ address, { bytes, bytes + size } });
	}

	void RestorePatches()
	{
		if (!Installed)
			return;
		for (auto& saved : SavedPatches)
		{
			Patch patch { saved.Address, static_cast<DWORD>(saved.Bytes.size()), saved.Bytes.data() };
			patch.Apply();
		}
		SavedPatches.clear();
		Installed = false;
		CurrentView = nullptr;
		PreviousMakeSelection = nullptr;
	}

	// Only fixed, pointer-free native unit orders are packed. Global options,
	// saving, player joins and other extensions retain their own native events.
	bool IsAtomicUnitOrder(unsigned int type)
	{
		switch (static_cast<EventType>(type))
		{
		case EventType::PowerOn: case EventType::PowerOff:
		case EventType::MegaMission: case EventType::MegaMissionF:
		case EventType::Idle: case EventType::Scatter:
		case EventType::Deploy: case EventType::Detonate:
		case EventType::Primary: case EventType::Repair: case EventType::Sell:
		case EventType::Archive: case EventType::PlanConnect: case EventType::PlanNodeDelete:
			return true;
		default:
			return false;
		}
	}

	unsigned int NativePayloadSize(unsigned int type)
	{
		return type <= 0x2E ? reinterpret_cast<const BYTE*>(0x8208EC)[type] : 0;
	}

	std::deque<EventClass> PackAtomicOrders(std::deque<EventClass> input)
	{
		std::deque<EventClass> output;
		while (!input.empty())
		{
			const auto house = input.front().HouseIndex;
			if (!IsAtomicUnitOrder(static_cast<unsigned int>(input.front().Type)) || house < 0)
			{
				output.push_back(input.front());
				input.pop_front();
				continue;
			}
			std::vector<Selection::Atomic::Record> records;
			while (!input.empty() && input.front().HouseIndex == house
				&& IsAtomicUnitOrder(static_cast<unsigned int>(input.front().Type))
				&& records.size() < Selection::Atomic::MaxRecords)
			{
				const auto& event = input.front();
				const auto type = static_cast<BYTE>(event.Type);
				const auto size = NativePayloadSize(type);
				const auto payload = reinterpret_cast<const BYTE*>(event.DataBuffer);
				records.push_back({ type, static_cast<BYTE>(house), { payload, payload + size } });
				input.pop_front();
			}
			std::vector<BYTE> bytes;
			if (!Selection::Atomic::Encode(records, bytes))
				Debug::FatalErrorAndExit("Selection atomic command encoding failed.");
			if (!++NextBatchID)
				++NextBatchID;
			const auto chunks = Selection::Atomic::Split(NextBatchID, bytes, records.size());
			for (const auto& chunk : chunks)
			{
				EventExt wire {};
				wire.Type = EventTypeExt::SelectionBatch;
				wire.HouseIndex = house;
				wire.Frame = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
				wire.SelectionBatch = chunk;
				output.push_back(*reinterpret_cast<EventClass*>(&wire));
			}
		}
		return output;
	}

	bool IsGroundedFlightInfantry(TechnoClass* techno)
	{
		const auto infantry = abstract_cast<InfantryClass*>(techno);
		return infantry && infantry->Type && infantry->IsAlive && !infantry->InLimbo
			&& Selection::PreserveGroundedFlightDestination(true, infantry->Type->JumpJet, infantry->IsInAir());
	}

	bool KeepFlightDestination(const EventClass& event)
	{
		if (event.Type != EventType::MegaMission || event.MegaMission.Mission != static_cast<BYTE>(Mission::Move))
			return false;
		auto actor = event.MegaMission.Whom;
		return IsGroundedFlightInfantry(actor.As_Techno());
	}

	bool SameMission(const EventClass& a, const EventClass& b)
	{
		return a.Type == EventType::MegaMission && b.Type == EventType::MegaMission
			&& a.MegaMission.Mission == b.MegaMission.Mission
			&& !std::memcmp(&a.MegaMission.Target, &b.MegaMission.Target, sizeof(TargetClass))
			&& !std::memcmp(&a.MegaMission.Destination, &b.MegaMission.Destination, sizeof(TargetClass));
	}

	void PreprocessAtomicMissions(std::vector<EventExt>& events)
	{
		using MissionQueue = QueueClass<EventClass, EventClass::MAX_EVENTS * 2>;
		auto& queue = EventClass::MegaMissionList;
		// The native dispatcher may already contain unrelated missions. Save it
		// on the heap (29 KB), use bounded windows for native formation/path work,
		// then restore it BEFORE executing any of this batch's unit orders.
		const auto saved = std::make_unique<MissionQueue>(queue);
		for (size_t offset = 0; offset < events.size(); )
		{
			const auto first = reinterpret_cast<EventClass*>(&events[offset]);
			if (first->Type != EventType::MegaMission)
			{
				++offset;
				continue;
			}
			size_t end = offset + 1;
			while (end < events.size() && end - offset < EventClass::MAX_EVENTS * 2
				&& SameMission(*first, *reinterpret_cast<EventClass*>(&events[end])))
				++end;
			if (end - offset > 1)
			{
				queue.Init();
				for (size_t i = offset; i < end; ++i)
					queue.Add(*reinterpret_cast<EventClass*>(&events[i]));
				using PreprocessFn = void(__fastcall*)(int, int);
				reinterpret_cast<PreprocessFn>(0x64CDA0)(0, static_cast<int>(end - offset));
				for (size_t i = offset; i < end; ++i)
					std::memcpy(&events[i], &queue[static_cast<int>(i - offset)], sizeof(EventClass));
			}
			offset = end;
		}
		queue = *saved;
	}

	int LastCommandFrame = -1;

	struct FlightWatchEntry
	{
		TargetClass Actor;
		CellStruct Goal;
		CoordStruct Origin;
		bool Repaired = false;
	};
	struct FlightWatch
	{
		int Frame = 0;
		std::vector<FlightWatchEntry> Entries;
	};
	std::vector<FlightWatch> FlightWatches;
	int LastFlightWatchFrame = -1;

	void WatchGroundedFlights(const std::vector<EventExt>& orders)
	{
		if (!SelectionService::Enabled())
			return;
		FlightWatch watch;
		watch.Frame = Unsorted::CurrentFrame;
		for (const auto& order : orders)
		{
			const auto& event = *reinterpret_cast<const EventClass*>(&order);
			if (!KeepFlightDestination(event))
				continue;
			auto actor = event.MegaMission.Whom;
			auto destination = event.MegaMission.Destination;
			const auto foot = actor.As_Foot();
			const auto goal = destination.As_Cell();
			if (foot && goal)
				watch.Entries.push_back({ event.MegaMission.Whom, goal->MapCoords, foot->Location });
		}
		if (!watch.Entries.empty())
		{
			if (FlightWatches.size() >= 16)
				FlightWatches.erase(FlightWatches.begin());
			FlightWatches.push_back(std::move(watch));
		}
	}

	void CheckFlightWatches()
	{
		if (!SelectionService::Enabled())
			return;
		const int frame = Unsorted::CurrentFrame;
		if (frame == LastFlightWatchFrame)
			return;
		LastFlightWatchFrame = frame;
		for (auto& watch : FlightWatches)
		{
			for (auto& entry : watch.Entries)
			{
				auto actor = entry.Actor;
				const auto foot = actor.As_Foot();
				if (!foot || !foot->IsAlive || foot->InLimbo || foot->Health <= 0 || entry.Repaired)
					continue;
				CellStruct destination { -1, -1 };
				if (foot->Destination)
					destination = CellClass::Coord2Cell(foot->Destination->GetCoords());
				const auto location = CellClass::Coord2Cell(foot->Location);
				if (destination == location && entry.Goal != location)
				{
					if (auto* const cell = MapClass::Instance.TryGetCellAt(entry.Goal))
					{
						foot->SetDestination(cell, true);
						if (foot->Locomotor)
							foot->Locomotor->Move_To(cell->GetCoords());
						entry.Repaired = true;
					}
				}
			}
		}
		std::erase_if(FlightWatches, [frame](const FlightWatch& watch) { return frame - watch.Frame >= 180; });
	}

	// Native selection/hit-test readers, verified in YR 1.001. Writers are
	// intercepted separately; the fixed native expiry scan keeps its old array.
	// Foreign DLL code keeps its original globals.
	void SetView(Candidate* view)
	{
		if (!Installed || view == CurrentView)
			return;
		CurrentView = view;
		const auto base = reinterpret_cast<DWORD>(view);
		Patch::Apply_TYPED<DWORD>(0x6DA0E0, { base });       // selection rectangle contains objects
		Patch::Apply_TYPED<DWORD>(0x6DA3CB, { base + 4 });   // object under mouse
		Patch::Apply_TYPED<DWORD>(0x6DA5FF, { base });       // native rectangle selection
		Patch::Apply_TYPED<DWORD>(0x6DC43A, { base });       // keyboard/type-selection iterator
	}

	bool IsLowPriority(ObjectClass* object)
	{
		if (object && (object->AbstractFlags & AbstractFlags::Techno) != AbstractFlags::None)
		{
			const auto type = object->GetTechnoType();
			if (const auto ext = TechnoTypeExt::ExtMap.TryFind(type))
				return ext->LowSelectionPriority;
		}
		return false;
	}

	bool IsInRect(TacticalClass* tactical, const LTRBStruct& rect, const Candidate& item)
	{
		if (!item.Object || !item.Object->IsAlive)
			return false;
		const int x = item.X - tactical->TacticalPos.X;
		const int y = item.Y - tactical->TacticalPos.Y;
		return x >= rect.Left && x < rect.Left + rect.Right
			&& y >= rect.Top && y < rect.Top + rect.Bottom;
	}

	LTRBStruct SelectionRect(const LTRBStruct& band)
	{
		const int left = std::min(band.Left, band.Right);
		const int top = std::min(band.Top, band.Bottom);
		return { left, top, std::max(band.Left, band.Right) - left + 1,
			std::max(band.Top, band.Bottom) - top + 1 };
	}

	void NativeMakeSelection(TacticalClass* tactical, SelectCallback callback)
	{
		if (tactical->Band.Left || tactical->Band.Top)
		{
			auto rect = SelectionRect(tactical->Band);
			using SelectFn = void(__thiscall*)(TacticalClass*, LTRBStruct*, SelectCallback);
			reinterpret_cast<SelectFn>(0x6DA5C0)(tactical, &rect, callback);
			tactical->Band.Left = 0;
			tactical->Band.Top = 0;
		}
	}

	void __fastcall MakeSelection(TacticalClass* tactical, void*, SelectCallback callback)
	{
		if (!PreviousMakeSelection)
		{
			NativeMakeSelection(tactical, callback);
			return;
		}
		if (Paging)
			return;

		const int count = tactical->SelectableCount;
		if (count <= NativeCapacity)
		{
			PreviousMakeSelection(tactical, nullptr, callback);
			return;
		}

		// Preserve the installed foreign selector (Phobos selection groups, IFV
		// groups, mass-selectable buildings and callbacks). Give it safe windows
		// through the ORIGINAL array; never patch a Phobos address or its DLL.
		const auto band = tactical->Band;
		const auto rect = SelectionRect(band);
		std::vector<Candidate> snapshot(Candidates.begin(), Candidates.end());
		bool highPriority = false;
		if (AresHelper::CanUsePhobos && CCINIClass::INI_RA2MD.ReadBool("Phobos", "PrioritySelectionFiltering", true))
		{
			for (const auto& item : snapshot)
			{
				if (!IsInRect(tactical, rect, item) || IsLowPriority(item.Object))
					continue;
				const auto owner = item.Object->GetOwningHouse();
				if (owner && owner->IsControlledByCurrentPlayer()
					&& item.Object->CanBeSelected() && item.Object->CanBeSelectedNow()
					&& !item.Object->InLimbo)
				{
					highPriority = true;
					break;
				}
			}
		}
		// Phobos computes priority per call. Filter across the whole rectangle
		// first so a low-only later page cannot bypass a high-priority first page.
		if (highPriority)
			std::erase_if(snapshot, [](const Candidate& item) { return IsLowPriority(item.Object); });

		ActiveSnapshot = &snapshot;
		Paging = true;
		SetView(&Unsorted::TacticalSelectables[0]);
		for (size_t offset = 0; offset < snapshot.size(); offset += NativeCapacity)
		{
			const auto pageCount = std::min<size_t>(NativeCapacity, snapshot.size() - offset);
			std::copy_n(snapshot.begin() + offset, pageCount, &Unsorted::TacticalSelectables[0]);
			tactical->SelectableCount = static_cast<int>(pageCount);
			tactical->Band = band;
			PreviousMakeSelection(tactical, nullptr, callback);
		}
		Paging = false;
		ActiveSnapshot = nullptr;
		tactical->SelectableCount = count;
		tactical->Band = band;
		tactical->Band.Left = 0;
		tactical->Band.Top = 0;
		std::copy_n(Candidates.begin(), std::min<size_t>(Candidates.size(), NativeCapacity), &Unsorted::TacticalSelectables[0]);
		SetView(Candidates.data());
	}
}

bool SelectionService::Enabled()
{
	const auto rules = RulesExt::Global();
	return rules && rules->SelectionQueue_Extend.Get();
}

void SelectionService::Configure()
{
	if (Enabled())
	{
		Install();
		return;
	}
	const bool hadCache = Installed;
	RestorePatches();
	if (hadCache && TacticalClass::Instance)
		TacticalClass::Instance->SelectableCount = 0;
	Candidates.clear();
	Commands.Clear();
	Receiver.Clear();
	FlightWatches.clear();
	LastFlightWatchFrame = -1;
	NextBatchID = 0;
	LastCommandFrame = -1;
}

void SelectionService::Install()
{
	// Startup occurs before RulesExt exists. Default-off must not even reserve
	// a new cache or write any operand/jump; rules loading opts in explicitly.
	if (!Enabled() || Installed)
		return;

	// Refuse unexpected reader instructions rather than writing into another
	// extension's hook. These operands are untouched by the referenced Phobos.
	const std::array<std::pair<DWORD, DWORD>, 4> operands {{
		{ 0x6DA0E0, 0xB0CEC8 }, { 0x6DA3CB, 0xB0CECC },
		{ 0x6DA5FF, 0xB0CEC8 }, { 0x6DC43A, 0xB0CEC8 }
	}};
	for (const auto& [address, expected] : operands)
	{
		if (*reinterpret_cast<const DWORD*>(address) != expected)
			Debug::FatalErrorAndExit("Selection cache reader at %08X was changed by another module.", address);
	}

	Candidates.reserve(NativeCapacity);
	const auto call = reinterpret_cast<const BYTE*>(0x4ABCEB);
	if (*call != 0xE8)
		Debug::FatalErrorAndExit("Unexpected tactical selection call at 004ABCEB.");
	auto previous = static_cast<DWORD>(0x4ABCF0 + *reinterpret_cast<const LONG*>(call + 1));
	if (previous == 0x6D9FF0 && *reinterpret_cast<const BYTE*>(previous) == 0xE9)
		previous = static_cast<DWORD>(previous + 5 + *reinterpret_cast<const LONG*>(previous + 1));
	if (previous != 0x6D9FF0)
		PreviousMakeSelection = reinterpret_cast<MakeSelectionFn>(previous);

	SavePatch(0x6D9EF0, 5);
	SavePatch(0x4ABCEB, 5);
	SavePatch(0x6D9FF0, 5);
	for (const auto& [address, expected] : operands)
		SavePatch(address, sizeof(expected));
	Installed = true;
	SetView(Candidates.data());
	// Keep a typed, qualified entry: an unqualified AddSelectable resolves to
	// this class's stdcall member, which pops 16 bytes instead of native's 12.
	const Selection::Native::RegisterSelectableFn registerSelectable =
		Selection::Native::RegistrationEntry<SelectionService>;
	Patch::Apply_LJMP(0x6D9EF0, reinterpret_cast<void*>(registerSelectable));
	Patch::Apply_CALL(0x4ABCEB, reinterpret_cast<void*>(&MakeSelection));
	Patch::Apply_LJMP(0x6D9FF0, reinterpret_cast<void*>(&MakeSelection));
}

bool SelectionService::AddSelectable(TacticalClass* tactical, ObjectClass* object, int x, int y)
{
	if (!Enabled() || Paging || !tactical || !object || x < -32 || y < -32
		|| x > DSurface::ViewBounds.Width + 32 || y > DSurface::ViewBounds.Height + 32)
		return false;

	// The renderer resets SelectableCount before registering the next frame.
	if (!tactical->SelectableCount)
		Candidates.clear();
	if (Candidates.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
		return false;
	Candidates.push_back({ object, tactical->TacticalPos.X + x, tactical->TacticalPos.Y + y });
	tactical->SelectableCount = static_cast<int>(Candidates.size());
	if (Candidates.size() <= NativeCapacity)
		Unsorted::TacticalSelectables[Candidates.size() - 1] = Candidates.back();
	SetView(Candidates.data());
	return true;
}

bool SelectionService::CanSelect()
{
	const auto rules = RulesExt::Global();
	return !Enabled() || !rules || Selection::CanAdd(rules->SelectionLimit.Get(), ObjectClass::CurrentObjects.Count);
}

bool SelectionService::UseGroundedFlightDestination(FootClass* foot, const CellStruct* requested, CellStruct* destination)
{
	if (!Enabled() || !requested || !destination || !IsGroundedFlightInfantry(foot)
		|| !MapClass::Instance.TryGetCellAt(*requested))
		return false;

	*destination = *requested;
	return true;
}

ObjectClass* SelectionService::GetMoveRepresentative(ObjectClass* original, const CellStruct* cell, bool checkFog)
{
	return Selection::MoveRepresentative(original, ObjectClass::CurrentObjects,
		[](ObjectClass* object)
		{
			const auto unit = abstract_cast<UnitClass*>(object);
			return unit && unit->Type->IsSimpleDeployer && unit->Type->DeployToLand && unit->Deployed;
		},
		[cell, checkFog](ObjectClass* object) { return static_cast<int>(object->MouseOverCell(cell, checkFog, false)); },
		static_cast<int>(Action::Move), static_cast<int>(Action::NoMove), static_cast<int>(Action::None));
}

void SelectionService::BeforeEvent()
{
	if (!Enabled())
		return;
	Commands.BeforeAdd(EventClass::OutList);
}

void SelectionService::PumpCommands()
{
	if (!Enabled())
		return;
	const int frame = Unsorted::CurrentFrame;
	CheckFlightWatches();
	const bool allowBatch = frame != LastCommandFrame;
	// Existing wire fragments pass through unchanged. Any new contiguous
	// native unit orders become one atomic batch before transport is throttled.
	if (!Commands.Empty())
		Commands.TransformPending(EventClass::OutList, PackAtomicOrders);
	Commands.Pump(EventClass::OutList, allowBatch);
	LastCommandFrame = frame;
}

void SelectionService::PointerGotInvalid(void* pointer, bool /*removed*/)
{
	// Match TacticalClass::PointerExpired, which clears even temporary expiry.
	for (auto& item : Candidates)
		if (item.Object == pointer)
			item.Object = nullptr;
	if (ActiveSnapshot)
	{
		for (auto& item : *ActiveSnapshot)
			if (item.Object == pointer)
				item.Object = nullptr;
	}
}

void SelectionService::Clear()
{
	const bool hadCache = Installed;
	RestorePatches();
	Receiver.Clear();
	FlightWatches.clear();
	LastFlightWatchFrame = -1;
	NextBatchID = 0;
	Candidates.clear();
	Commands.Clear();
	LastCommandFrame = -1;
	if (hadCache && TacticalClass::Instance)
		TacticalClass::Instance->SelectableCount = 0;
	SetView(Candidates.data());
}

bool SelectionService::CompleteLoad()
{
	// TacticalClass is deserialized after ClearClasses; its saved count has no
	// corresponding live candidate array until the next render.
	Clear();
	Configure();
	return true;
}

void SelectionService::ReceiveBatch(const EventClass& event)
{
	if (!Enabled() || event.HouseIndex < 0 || event.HouseIndex >= HouseClass::Array.Count)
		return;
	const auto& wire = reinterpret_cast<const EventExt&>(event);
	std::vector<Selection::Atomic::Record> records;
	const auto result = Receiver.Accept(static_cast<unsigned int>(event.HouseIndex), wire.SelectionBatch, records);
	if (result != Selection::Atomic::Result::Complete)
	{
		return;
	}

	std::vector<EventExt> orders;
	orders.reserve(records.size());
	for (const auto& record : records)
	{
		if (record.House != static_cast<unsigned int>(event.HouseIndex) || !IsAtomicUnitOrder(record.Type)
			|| record.Payload.size() != NativePayloadSize(record.Type))
		{
			return; // Validate the WHOLE batch before any unit receives an order.
		}
		EventExt order {};
		order.Type = static_cast<EventTypeExt>(record.Type);
		order.HouseIndex = event.HouseIndex;
		order.Frame = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
		std::memcpy(order.DataBuffer, record.Payload.data(), record.Payload.size());
		orders.push_back(order);
	}

	const auto sender = HouseClass::Array.GetItem(event.HouseIndex);
	std::erase_if(orders, [sender](const EventExt& order)
		{
			TargetClass actor;
			std::memcpy(&actor, order.DataBuffer, sizeof(actor));
			return !EventExt::CanControl(sender, actor.As_Techno());
		});
	PreprocessAtomicMissions(orders);
	WatchGroundedFlights(orders);
	const int frame = Unsorted::CurrentFrame;
	size_t executed = 0;
	for (auto& order : orders)
	{
		using ExecuteFn = void(__thiscall*)(EventClass*);
		reinterpret_cast<ExecuteFn>(0x4C6CB0)(reinterpret_cast<EventClass*>(&order));
		++executed;
	}
}
