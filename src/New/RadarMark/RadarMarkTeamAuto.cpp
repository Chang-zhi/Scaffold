#include "RadarMarkTeamAuto.h"
#include "RadarMark.h"
#include "RadarMarkStream.h"

#include <TeamClass.h>
#include <TeamTypeClass.h>
#include <AnimTypeClass.h>
#include <Unsorted.h>

#include <Ext/TeamType/Body.h>

#include <unordered_map>
#include <vector>

namespace
{
	// 每条记录对应一个仍在世界中的小队。Settled = 标记已到期/被清掉，别再补。
	struct Record
	{
		TeamTypeClass* pType = nullptr;   // 指针会被新对象复用，类型变了就当成新记录
		AnimTypeClass* pAnim = nullptr;
		RadarMarkId    Id { };
		bool           Settled = false;
		bool           Seen = false;
	};

	std::unordered_map<TeamClass*, Record> g_Records;
	int g_LastFrame = -1;

	struct SavedRecord
	{
		TeamClass* Object = nullptr;
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

	// 动画按 Animations 名指定，与 [TechnoType] RadarAnim 同一套写法
	// 没有图的动画（名字打错，或该动画只有逻辑没有 SHP）不画，否则会退化成方块
	AnimTypeClass* GetDrawableAnim(TeamTypeExt::ExtData* pTypeExt)
	{
		AnimTypeClass* pAnim = pTypeExt->RadarAnim.Get();
		return (pAnim && RadarMark::ResolveAnimImage(pAnim)) ? pAnim : nullptr;
	}

	RadarArt BuildArt(TeamTypeExt::ExtData* pTypeExt, AnimTypeClass* pAnim)
	{
		RadarArt art { pAnim };
		art.HideInShroud = !pTypeExt->RadarAnim_ShowInShroud;
		return art;
	}

	RadarTeamMode ResolveMode(TeamTypeExt::ExtData* pTypeExt)
	{
		return (pTypeExt->RadarAnim_Mode == RadarTeamMarkMode::EachMember)
			? RadarTeamMode::EachMember
			: RadarTeamMode::Center;
	}

	// 标签随标记保存，恢复时使用 Swizzle 后的对象引用，不依赖小队数组顺序。
	void MakeTag(char(&buffer)[64], TeamClass* pTeam)
	{
		sprintf_s(buffer, "__RadarTeamMark_%p", static_cast<void*>(pTeam));
	}

}

void RadarMarkTeamAuto::Sync()
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

	for (TeamClass* pTeam : TeamClass::Array)
	{
		if (!pTeam)
			continue;

		TeamTypeClass* pType = pTeam->Type;
		auto pTypeExt = pType ? TeamTypeExt::ExtMap.Find(pType) : nullptr;
		AnimTypeClass* pAnim = pTypeExt ? GetDrawableAnim(pTypeExt) : nullptr;

		if (!pAnim)
			continue;

		auto [it, inserted] = g_Records.try_emplace(pTeam);
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
			// 换地图会重读规则/art，AnimTypeClass 被重建，条目里留着的是旧对象
			if (record.pAnim != pAnim)
			{
				RadarMark::UpdateArt(record.Id, BuildArt(pTypeExt, pAnim), RadarMark::AnimDuration(pAnim));
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
		MakeTag(tag, pTeam);
		record.Id = RadarMark::Add(pTeam, BuildArt(pTypeExt, pAnim), RadarMark::AnimDuration(pAnim), tag, ResolveMode(pTypeExt));
		record.pAnim = pAnim;
	}

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

void RadarMarkTeamAuto::Reset()
{
	g_LoadPending = false;
	Clear();
}

bool RadarMarkTeamAuto::SaveGlobals(ScaffoldStreamWriter& Stm)
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

bool RadarMarkTeamAuto::LoadGlobals(ScaffoldStreamReader& Stm)
{
	Reset();
	if (!RadarMarkStream::LoadRecords(Stm, g_LoadedRecords))
		return false;
	g_LoadPending = true;
	return true;
}

bool RadarMarkTeamAuto::CompleteLoad()
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

void RadarMarkTeamAuto::Clear()
{
	g_Records.clear();
	g_LastFrame = -1;
	if (!g_LoadPending)
		g_LoadedRecords.clear();
}

void RadarMarkTeamAuto::PointerGotInvalid(void* ptr)
{
	for (auto it = g_Records.begin(); it != g_Records.end(); )
	{
		if (it->first == ptr || it->second.pType == ptr || it->second.pAnim == ptr)
			it = g_Records.erase(it);
		else
			++it;
	}
}
