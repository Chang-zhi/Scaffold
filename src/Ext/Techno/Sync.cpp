#include "Body.h"

#include <TemporalClass.h>
#include <Utilities/NativeSavegame.h>
#include <Utilities/SavegameDef.h>

namespace
{
	constexpr size_t MaxRecords = 1024 * 1024;
	struct Relation
	{
		TechnoClass* Target = nullptr;
		TechnoClass* Attacker = nullptr;
	};
	bool PendingGlobals = false;
	std::vector<TemporalClass*> LoadedFakes;
	std::vector<Relation> LoadedClaims;
	std::vector<Relation> LoadedMainOwners;
	std::vector<Relation> LoadedExclusive;
	std::vector<TechnoClass*> LoadedDisabled;

	template <typename Map>
	void SaveRelations(ScaffoldStreamWriter& stm, const Map& map)
	{
		std::vector<Relation> records;
		for (auto target : TechnoClass::Array)
		{
			const auto it = map.find(target);
			if (it != map.end())
				records.push_back({ target, it->second });
		}
		stm.Save(records.size());
		for (auto& record : records)
			stm.Process(record.Target).Process(record.Attacker);
	}

	bool LoadRelations(ScaffoldStreamReader& stm, std::vector<Relation>& records)
	{
		size_t count = 0;
		if (!stm.Load(count) || count > MaxRecords)
			return false;
		records.clear();
		records.resize(count);
		for (auto& record : records)
			stm.Process(record.Target).Process(record.Attacker);
		return stm.Success();
	}

	template <typename Map>
	bool RestoreRelations(Map& map, const std::vector<Relation>& records,
		const std::unordered_set<TechnoClass*>& live)
	{
		map.clear();
		for (const auto& record : records)
		{
			if (!live.contains(record.Target) || !live.contains(record.Attacker)
				|| !map.emplace(record.Target, record.Attacker).second)
				return false;
		}
		return true;
	}
}

void TechnoExt::Clear()
{
	for (auto fake : LoadedFakes)
	{
		fake->Owner = nullptr;
		fake->Target = nullptr;
		fake->NextTemporal = nullptr;
		fake->PrevTemporal = nullptr;
		TemporalClass::Array.AddItem(fake);
		GameDelete(fake);
	}
	LoadedFakes.clear();
	LoadedClaims.clear();
	LoadedMainOwners.clear();
	LoadedExclusive.clear();
	LoadedDisabled.clear();
	PendingGlobals = false;
	TemporalAOE::Clear();
	TemporalExclusive::TargetsMap.clear();
	DeployWarp::ByProjector.clear();
	BerzerkRestoreClearCache();
	ResetFrameState();
	ExtMap.Clear();
}

bool TechnoExt::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	SaveFrameState(Stm);
	SaveRelations(Stm, TemporalAOE::SecondaryClaims);
	SaveRelations(Stm, TemporalAOE::CachedMainOwners);
	SaveRelations(Stm, TemporalExclusive::TargetsMap);
	std::vector<TechnoClass*> disabled;
	std::vector<TemporalClass*> fakes;
	for (auto target : TechnoClass::Array)
	{
		if (TemporalAOE::DisabledByUs.contains(target))
			disabled.push_back(target);
		const auto it = TemporalAOE::FakeTemporals.find(target);
		if (it != TemporalAOE::FakeTemporals.end())
			fakes.push_back(it->second.FakeTemporal);
	}
	Stm.Process(disabled);
	Stm.Save(fakes.size());
	for (auto fake : fakes)
	{
		std::vector<unsigned char> bytes;
		if (!NativeSavegame::Save(fake, bytes))
			return false;
		Stm.Process(bytes);
	}
	return Stm.Success();
}

bool TechnoExt::LoadGlobals(ScaffoldStreamReader& Stm)
{
	Clear();
	LoadFrameState(Stm);
	if (!LoadRelations(Stm, LoadedClaims) || !LoadRelations(Stm, LoadedMainOwners)
		|| !LoadRelations(Stm, LoadedExclusive))
		return false;
	Stm.Process(LoadedDisabled);
	size_t count = 0;
	if (!Stm.Load(count) || count > MaxRecords)
		return false;
	LoadedFakes.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		std::vector<unsigned char> bytes;
		size_t capacity = 0;
		size_t length = 0;
		if (!Stm.Load(capacity) || !Stm.Load(length)
			|| capacity > NativeSavegame::MaxObjectBytes || length > capacity
			|| length < sizeof(void*) + sizeof(TemporalClass))
			return false;
		bytes.resize(length);
		if (!Stm.Read(bytes.data(), bytes.size()))
			return false;
		auto fake = GameCreate<TemporalClass>(nullptr);
		if (!fake)
			return false;
		TemporalClass::Array.Remove(fake);
		LoadedFakes.push_back(fake);
		// Native Load registers the saved fake address and all of its fields.
		if (!NativeSavegame::Load(fake, bytes))
			return false;
	}
	PendingGlobals = true;
	TemporalAOE::s_PostLoadCleanupNeeded = true;
	return Stm.Success();
}

bool TechnoExt::CompleteLoad()
{
	if (!PendingGlobals)
		return true;
	std::unordered_set<TechnoClass*> live;
	for (auto techno : TechnoClass::Array)
	{
		if (!techno)
			continue;
		live.insert(techno);
		if (auto ext = ExtMap.Find(techno))
			ext->CompleteLoad();
	}
	if (!RestoreRelations(TemporalAOE::SecondaryClaims, LoadedClaims, live)
		|| !RestoreRelations(TemporalAOE::CachedMainOwners, LoadedMainOwners, live)
		|| !RestoreRelations(TemporalExclusive::TargetsMap, LoadedExclusive, live))
		return false;
	for (auto target : LoadedDisabled)
	{
		if (!live.contains(target))
			return false;
		TemporalAOE::DisabledByUs.insert(target);
	}
	for (auto fake : LoadedFakes)
	{
		if (!live.contains(fake->Owner) || !live.contains(fake->Target)
			|| fake->WarpPerStep != 0 || fake->WarpRemaining != 0x7FFFFFFF
			|| !TemporalAOE::FakeTemporals.emplace(fake->Target,
				TemporalAOE::FakeTemporalEntry { fake, fake->Owner }).second)
			return false;
		TemporalAOE::SecondariesByAttacker[fake->Owner].insert(fake->Target);
	}
	for (auto techno : TechnoClass::Array)
	{
		auto ext = techno ? ExtMap.Find(techno) : nullptr;
		if (!ext)
			continue;
		for (auto target : ext->WarpField.TargetsInField)
		{
			if (!live.contains(target))
				return false;
			// The last scan can legitimately retain a target whose fake has
			// since been released. Preserve this phase until the next scan.
			DeployWarp::ByProjector[techno].insert(target);
		}
	}
	LoadedFakes.clear();
	LoadedClaims.clear();
	LoadedMainOwners.clear();
	LoadedExclusive.clear();
	LoadedDisabled.clear();
	PendingGlobals = false;
	TemporalAOE::s_PostLoadCleanupNeeded = false;
	return true;
}
