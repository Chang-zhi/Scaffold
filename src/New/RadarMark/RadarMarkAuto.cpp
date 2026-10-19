#include "RadarMarkAuto.h"
#include "RadarMark.h"
#include "RadarMarkStream.h"

#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <AnimTypeClass.h>
#include <Unsorted.h>

#include <Ext/TechnoType/Body.h>

#include <unordered_map>
#include <vector>

namespace
{
	// 每条记录对应一个仍在世界中的对象。Settled = 标记已到期/被清掉，别再补。
	struct Record
	{
		TechnoTypeClass* pType = nullptr;   // 指针会被新对象复用，类型变了就当成新记录
		AnimTypeClass*   pAnim = nullptr;   // 建标记时用的动画；规则/art 重读后会换新对象，需要比对
		RadarMarkId      Id { };
		bool             Settled = false;
		bool             Seen = false;
	};

	std::unordered_map<TechnoClass*, Record> g_Records;
	int g_LastFrame = -1;

	struct SavedRecord
	{
		TechnoClass* Object = nullptr;
		Record Data;

		template <typename T>
		bool Serialize(T& Stm, bool registerForChange)
		{
			return Stm.Process(this->Object, registerForChange)
				.Process(this->Data.pType, registerForChange)
				.Process(this->Data.pAnim, registerForChange)
				.Process(this->Data.Id.Value).Process(this->Data.Settled).Success();
		}

		bool Load(ScaffoldStreamReader& Stm, bool registerForChange)
		{
			return this->Serialize(Stm, registerForChange);
		}

		bool Save(ScaffoldStreamWriter& Stm) const
		{
			return const_cast<SavedRecord*>(this)->Serialize(Stm, true);
		}
	};

	std::vector<SavedRecord> g_LoadedRecords;
	bool g_LoadPending = false;

	RadarArt BuildArt(TechnoTypeExt::ExtData* pTypeExt)
	{
		RadarArt art { pTypeExt->RadarAnim.Get() };
		art.HideInShroud = !pTypeExt->RadarAnim_ShowInShroud;
		return art;
	}

	// 用 UniqueID 而不是指针：存读档后对象地址会变，tag 必须还能对上同一条标记
	void MakeTag(char(&buffer)[64], TechnoClass* pTechno)
	{
		sprintf_s(buffer, "__RadarTypeMark_%u", static_cast<unsigned int>(pTechno->UniqueID));
	}

	// 没有图的动画（名字打错，或该动画只有逻辑没有 SHP）不画，否则会退化成方块
	AnimTypeClass* GetDrawableAnim(TechnoTypeExt::ExtData* pTypeExt)
	{
		AnimTypeClass* pAnim = pTypeExt->RadarAnim.Get();
		return (pAnim && RadarMark::ResolveAnimImage(pAnim)) ? pAnim : nullptr;
	}
}

void RadarMarkAuto::Sync()
{
	// 指针完成重定位后，由 RadarMark::CompleteLoad 统一恢复。
	if (g_LoadPending)
		return;

	int const frame = Unsorted::CurrentFrame;

	if (frame == g_LastFrame)
		return;

	// 换地图/读档会把帧计数归零，旧场景的指针一律作废
	if (frame < g_LastFrame)
		g_Records.clear();

	g_LastFrame = frame;

	for (auto& pair : g_Records)
		pair.second.Seen = false;

	for (TechnoClass* pTechno : TechnoClass::Array)
	{
		// 进载具/机场只是看不见，标记留着（计时也不停）；死亡才放掉记录
		if (!pTechno || !pTechno->IsAlive)
			continue;

		TechnoTypeClass* pType = pTechno->GetTechnoType();
		auto pTypeExt = TechnoTypeExt::ExtMap.Find(pType);
		AnimTypeClass* pAnim = pTypeExt ? GetDrawableAnim(pTypeExt) : nullptr;

		if (!pAnim)
			continue;

		auto [it, inserted] = g_Records.try_emplace(pTechno);
		auto& record = it->second;

		if (inserted || record.pType != pType)
		{
			record = Record { };
			record.pType = pType;
		}

		record.Seen = true;

		if (record.Settled)
			continue;

		if (record.Id && RadarMark::Exists(record.Id))
		{
			// 换地图会重读规则/art，AnimTypeClass 被重建，条目里留着的是旧对象。
			// 这里比对当前指针，变了就把外观换掉（不动计时）。
			if (record.pAnim != pAnim)
			{
				RadarMark::UpdateArt(record.Id, BuildArt(pTypeExt), RadarMark::AnimDuration(pAnim));
				record.pAnim = pAnim;
			}

			continue;
		}

		// 记录还在、条目没了：要么到期，要么被别处清掉。两种都别自动复活。
		if (record.Id)
		{
			record.Settled = true;
			record.Id = { };
			continue;
		}

		char tag[64];
		MakeTag(tag, pTechno);
		record.Id = RadarMark::Add(pTechno, BuildArt(pTypeExt), RadarMark::AnimDuration(pAnim), tag);
		record.pAnim = pAnim;
	}

	// 记录没了（死亡、类型变掉、RadarAnim 被撤掉）就把标记一并撤掉，避免留下没人管的孤儿条目
	for (auto it = g_Records.begin(); it != g_Records.end(); )
	{
		if (it->second.Seen)
		{
			++it;
			continue;
		}

		RadarMark::Remove(it->second.Id);
		it = g_Records.erase(it);
	}
}

void RadarMarkAuto::Reset()
{
	g_LoadPending = false;
	Clear();
}

bool RadarMarkAuto::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	if (g_LoadPending || g_Records.size() > RadarMarkStream::MaxRecords)
		return false;

	Stm.Save(static_cast<int>(g_Records.size()));
	for (auto const& [object, record] : g_Records)
	{
		SavedRecord saved { object, record };
		Stm.Process(saved);
	}
	return Stm.Success();
}

bool RadarMarkAuto::LoadGlobals(ScaffoldStreamReader& Stm)
{
	Reset();
	if (!RadarMarkStream::LoadRecords(Stm, g_LoadedRecords))
		return false;
	g_LoadPending = true;
	return true;
}

bool RadarMarkAuto::CompleteLoad()
{
	if (!g_LoadPending)
		return true;

	for (auto const& saved : g_LoadedRecords)
	{
		if (saved.Object)
			g_Records[saved.Object] = saved.Data;
	}
	g_LoadedRecords.clear();
	g_LoadPending = false;
	g_LastFrame = -1;
	return true;
}

void RadarMarkAuto::Clear()
{
	g_Records.clear();
	g_LastFrame = -1;
	if (!g_LoadPending)
		g_LoadedRecords.clear();
}

void RadarMarkAuto::PointerGotInvalid(void* ptr)
{
	for (auto it = g_Records.begin(); it != g_Records.end(); )
	{
		if (it->first == ptr || it->second.pType == ptr || it->second.pAnim == ptr)
			it = g_Records.erase(it);
		else
			++it;
	}
}
