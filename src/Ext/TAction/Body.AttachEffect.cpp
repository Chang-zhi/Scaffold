#include "Body.h"

#include <Interop/AttachEffectService.h>

#include <TeamTypeClass.h>
#include <TeamClass.h>
#include <HouseClass.h>
#include <TechnoClass.h>
#include <TagClass.h>
#include <TriggerClass.h>
#include <TriggerTypeClass.h>

#include <Utilities/Debug.h>

#include <string>

namespace
{
	TeamTypeClass* FindTeamTypeByIndex(int index)
	{
		const std::string id = "0" + std::to_string(index);

		for (auto pTeamType : TeamTypeClass::Array)
		{
			if (pTeamType && pTeamType->get_ID() == id)
				return pTeamType;
		}

		return nullptr;
	}

	struct TeamSweepResult
	{
		int Teams;
		int Affected;
	};

	template <typename Fn>
	TeamSweepResult ForEachTeamOfType(TeamTypeClass* pTeamType, Fn&& fn)
	{
		TeamSweepResult result { 0, 0 };

		for (int i = 0; i < TeamClass::Array.Count; ++i)
		{
			auto const pTeam = TeamClass::Array.GetItem(i);

			if (!pTeam || pTeam->Type != pTeamType)
				continue;

			result.Teams++;
			result.Affected += fn(pTeam);
		}

		return result;
	}

	struct TechnoSweepResult
	{
		int Units;
		int Affected;
	};

	template <typename Pred, typename Fn>
	TechnoSweepResult ForEachTechno(Pred&& pred, Fn&& fn)
	{
		TechnoSweepResult result { 0, 0 };

		for (TechnoClass* pTechno : TechnoClass::Array)
		{
			if (!pTechno || !pred(pTechno))
				continue;

			result.Units++;
			result.Affected += fn(pTechno);
		}

		return result;
	}

	// 「关联科技类型」= 身上挂着的标签里含有正在执行的那个触发。
	// 判定不中时原因看不见, 所以把三种情况分开统计进日志:
	// 触发指针为空 / 地图上没有任何科技类型 / 有标签但都不含本触发。
	template <typename Fn>
	TechnoSweepResult ForEachAssociatedTechno(const char* actionName, TriggerClass* pTrigger, Fn&& fn)
	{
		TechnoSweepResult result { 0, 0 };
		int scanned = 0;
		int tagged = 0;

		const bool hasTrigger = pTrigger != nullptr;
		const char* const triggerId = hasTrigger && pTrigger->Type ? pTrigger->Type->get_ID() : "<none>";

		for (TechnoClass* pTechno : TechnoClass::Array)
		{
			if (!pTechno)
				continue;

			++scanned;

			if (!pTechno->AttachedTag)
				continue;

			++tagged;

			if (!hasTrigger || !pTechno->AttachedTag->ContainsTrigger(pTrigger))
				continue;

			Debug::Log("[AE] %s: matched unit [%s]\n", actionName, pTechno->get_ID());

			result.Units++;
			result.Affected += fn(pTechno);
		}

		if (!hasTrigger)
			Debug::Log("[AE] %s: pTrigger is null, no unit can be associated\n", actionName);
		else if (result.Units == 0)
			Debug::Log("[AE] %s: trigger [%s] is attached to no unit (scanned=%d, with a tag=%d)\n",
				actionName, triggerId, scanned, tagged);
		else
			Debug::Log("[AE] %s: trigger [%s] matched=%d affected=%d\n",
				actionName, triggerId, result.Units, result.Affected);

		return result;
	}
}

// ============================================================================
// 696~699: 目标为「某个小队类型的全部小队」
// ============================================================================

bool TActionExt::ApplyAttachEffectToTeamType(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	auto const pTeamType = FindTeamTypeByIndex(pThis->Param3);

	if (!pTeamType)
	{
		Debug::Log("[Scaffold] ApplyAttachEffectToTeamType: TeamType Param3=%d not found\n", pThis->Param3);
		return false;
	}

	const int nameIndex = pThis->Param4;
	const int durationOverride = pThis->Param5;

	auto const result = ForEachTeamOfType(pTeamType, [nameIndex, durationOverride](TeamClass* pTeam) {
		return AttachEffectService::ApplyToTeam(pTeam, nameIndex, durationOverride);
	});

	Debug::Log("[AE] TAction %d ApplyAttachEffectToTeamType: teamType=[%s] effectKey=%d duration=%d -> teams=%d affected=%d\n",
		static_cast<int>(pThis->ActionKind), pTeamType->get_ID(), nameIndex, durationOverride,
		result.Teams, result.Affected);

	return true;
}

bool TActionExt::RemoveAttachEffectFromTeamType(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	auto const pTeamType = FindTeamTypeByIndex(pThis->Param3);

	if (!pTeamType)
	{
		Debug::Log("[Scaffold] RemoveAttachEffectFromTeamType: TeamType Param3=%d not found\n", pThis->Param3);
		return false;
	}

	const int nameIndex = pThis->Param4;

	auto const result = ForEachTeamOfType(pTeamType, [nameIndex](TeamClass* pTeam) {
		return AttachEffectService::RemoveFromTeam(pTeam, nameIndex);
	});

	Debug::Log("[AE] TAction %d RemoveAttachEffectFromTeamType: teamType=[%s] effectKey=%d -> teams=%d affected=%d\n",
		static_cast<int>(pThis->ActionKind), pTeamType->get_ID(), nameIndex, result.Teams, result.Affected);

	return true;
}

bool TActionExt::RemoveAttachEffectByGroupFromTeamType(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	auto const pTeamType = FindTeamTypeByIndex(pThis->Param3);

	if (!pTeamType)
	{
		Debug::Log("[Scaffold] RemoveAttachEffectByGroupFromTeamType: TeamType Param3=%d not found\n", pThis->Param3);
		return false;
	}

	// 组名直接取自动作文本(与 694/695 读 AI 触发 ID 同一通道), 不经过任何注册表。
	const char* const groupName = pThis->Text;

	if (!groupName[0])
	{
		Debug::Log("[Scaffold] RemoveAttachEffectByGroupFromTeamType: no group name given\n");
		return false;
	}

	auto const result = ForEachTeamOfType(pTeamType, [groupName](TeamClass* pTeam) {
		return AttachEffectService::RemoveGroupsFromTeam(pTeam, groupName);
	});

	Debug::Log("[AE] TAction %d RemoveAttachEffectByGroupFromTeamType: teamType=[%s] group=[%s] -> teams=%d affected=%d\n",
		static_cast<int>(pThis->ActionKind), pTeamType->get_ID(), groupName, result.Teams, result.Affected);

	return true;
}

bool TActionExt::RemoveAllAttachEffectsFromTeamType(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	auto const pTeamType = FindTeamTypeByIndex(pThis->Param3);

	if (!pTeamType)
	{
		Debug::Log("[Scaffold] RemoveAllAttachEffectsFromTeamType: TeamType Param3=%d not found\n", pThis->Param3);
		return false;
	}

	auto const result = ForEachTeamOfType(pTeamType, [](TeamClass* pTeam) {
		return AttachEffectService::RemoveAllFromTeam(pTeam);
	});

	Debug::Log("[AE] TAction %d RemoveAllAttachEffectsFromTeamType: teamType=[%s] -> teams=%d affected=%d\n",
		static_cast<int>(pThis->ActionKind), pTeamType->get_ID(), result.Teams, result.Affected);

	return true;
}

// ============================================================================
// 1100~1103: 目标为「本触发的关联科技类型」
// ============================================================================

bool TActionExt::ApplyAttachEffectToAssociatedTechnos(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	const int nameIndex = pThis->Param3;
	const int durationOverride = pThis->Param4;

	ForEachAssociatedTechno("ApplyAttachEffectToAssociatedTechnos", pTrigger,
		[nameIndex, durationOverride](TechnoClass* pTechno) {
			return AttachEffectService::ApplyToTechno(pTechno, nameIndex, durationOverride);
		});

	Debug::Log("[AE] TAction %d ApplyAttachEffectToAssociatedTechnos: effectKey=%d duration=%d\n",
		static_cast<int>(pThis->ActionKind), nameIndex, durationOverride);

	return true;
}

bool TActionExt::RemoveAttachEffectFromAssociatedTechnos(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	const int nameIndex = pThis->Param3;

	ForEachAssociatedTechno("RemoveAttachEffectFromAssociatedTechnos", pTrigger,
		[nameIndex](TechnoClass* pTechno) {
			return AttachEffectService::RemoveFromTechno(pTechno, nameIndex);
		});

	Debug::Log("[AE] TAction %d RemoveAttachEffectFromAssociatedTechnos: effectKey=%d\n",
		static_cast<int>(pThis->ActionKind), nameIndex);

	return true;
}

bool TActionExt::RemoveAttachEffectByGroupFromAssociatedTechnos(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	// 组名直接取自动作文本(与 698 同一通道), 不经过任何注册表。
	const char* const groupName = pThis->Text;

	if (!groupName[0])
	{
		Debug::Log("[Scaffold] RemoveAttachEffectByGroupFromAssociatedTechnos: no group name given\n");
		return false;
	}

	ForEachAssociatedTechno("RemoveAttachEffectByGroupFromAssociatedTechnos", pTrigger,
		[groupName](TechnoClass* pTechno) {
			return AttachEffectService::RemoveGroupsFromTechno(pTechno, groupName);
		});

	Debug::Log("[AE] TAction %d RemoveAttachEffectByGroupFromAssociatedTechnos: group=[%s]\n",
		static_cast<int>(pThis->ActionKind), groupName);

	return true;
}

bool TActionExt::RemoveAllAttachEffectsFromAssociatedTechnos(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	ForEachAssociatedTechno("RemoveAllAttachEffectsFromAssociatedTechnos", pTrigger,
		[](TechnoClass* pTechno) {
			return AttachEffectService::RemoveAllFromTechno(pTechno);
		});

	Debug::Log("[AE] TAction %d RemoveAllAttachEffectsFromAssociatedTechnos\n",
		static_cast<int>(pThis->ActionKind));

	return true;
}

// ============================================================================
// 1104~1105: 目标为「某个所属方的全部科技类型」
// ============================================================================

bool TActionExt::ApplyAttachEffectToHouseTechnos(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	auto const pOwner = HouseClass::FindByCountryIndex(pThis->Param3);

	if (!pOwner)
	{
		Debug::Log("[Scaffold] ApplyAttachEffectToHouseTechnos: house Param3=%d not found\n", pThis->Param3);
		return false;
	}

	const int nameIndex = pThis->Param4;
	const int durationOverride = pThis->Param5;

	auto const result = ForEachTechno(
		[&](TechnoClass* pTechno) { return pTechno->Owner == pOwner; },
		[&](TechnoClass* pTechno) { return AttachEffectService::ApplyToTechno(pTechno, nameIndex, durationOverride); });

	Debug::Log("[AE] TAction %d ApplyAttachEffectToHouseTechnos: house=[%s] effectKey=%d duration=%d -> units=%d affected=%d\n",
		static_cast<int>(pThis->ActionKind), pOwner->get_ID(), nameIndex, durationOverride,
		result.Units, result.Affected);

	return true;
}

bool TActionExt::RemoveAttachEffectFromHouseTechnos(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location)
{
	auto const pOwner = HouseClass::FindByCountryIndex(pThis->Param3);

	if (!pOwner)
	{
		Debug::Log("[Scaffold] RemoveAttachEffectFromHouseTechnos: house Param3=%d not found\n", pThis->Param3);
		return false;
	}

	const int nameIndex = pThis->Param4;

	auto const result = ForEachTechno(
		[&](TechnoClass* pTechno) { return pTechno->Owner == pOwner; },
		[&](TechnoClass* pTechno) { return AttachEffectService::RemoveFromTechno(pTechno, nameIndex); });

	Debug::Log("[AE] TAction %d RemoveAttachEffectFromHouseTechnos: house=[%s] effectKey=%d -> units=%d affected=%d\n",
		static_cast<int>(pThis->ActionKind), pOwner->get_ID(), nameIndex, result.Units, result.Affected);

	return true;
}
