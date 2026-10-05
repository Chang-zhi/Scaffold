#include "Body.h"
#include <Ext/TAction/Features/ScriptManipulator.h>
#include <Ext/TAction/Features/TaskForceManipulator.h>

#include <YRpp.h>
#include <TagClass.h>
#include <TagTypeClass.h>
#include <TechnoClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <HouseClass.h>
#include <Ext/House/Body.h>
#include <New/FootPath/FootPathVisualizer.h>
#include <ArrayClasses.h>
#include <MessageListClass.h>
#include <GameOptionsClass.h>
#include <DisplayClass.h>

#include <Utilities/SavegameDef.h>
#include <Utilities/SpawnerHelper.h>
#include <Utilities/GeneralUtils.h>

#include <New/TextBox/MapTextBoxClass.h>
#include <New/TextBox/TextBoxTypeClass.h>
#include <New/TextBox/WaypointTextBoxClass.h>
#include <New/TextBox/TechnoTextBoxClass.h>
#include <New/ChoiceBox/ChoiceBoxTypeClass.h>
#include <New/ChoiceBox/WaypointChoiceBoxClass.h>
#include <New/ChoiceBox/ScreenChoiceBoxClass.h>

#include <set>
#include <vector>
#include <string>
#include <cstring>
#include <Unsorted.h>

//Static init
TActionExt::ExtContainer TActionExt::ExtMap;

// =============================
// load / save

template <typename T>
void TActionExt::ExtData::Serialize(T& Stm)
{
	//Stm;
}

void TActionExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<TActionClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TActionExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<TActionClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

bool TActionExt::Execute(TActionClass* pThis, HouseClass* pHouse, ObjectClass* pObject,
	TriggerClass* pTrigger, CellStruct const& location, bool& bHandled)
{
	bHandled = true;

	// 行为 48(MoveCameraToWaypoint) / 112(CenterCameraAtWaypoint) 会把视野移动到指定路径点。
	// 但若玩家正“跟随某个单位”, DisplayClass 每帧都会把战术镜头强制拉回被跟随单位,
	// 覆盖该行动设置的镜头位置, 导致行为看起来“不执行”。故执行这两个行为时取消跟随状态。
	// Vanilla
	switch (pThis->ActionKind)
	{
	case TriggerAction::MoveCameraToWaypoint:
	case TriggerAction::CenterCameraAtWaypoint:
		DisplayClass::Instance.FollowObject = false;
		DisplayClass::Instance.ObjectToFollow = nullptr;
		break;
	default:
		break;
	}

	// Scaffold
	switch (static_cast<ScaffoldTriggerAction>(pThis->ActionKind))
	{

	case ScaffoldTriggerAction::SetWaypointTextBoxByType:
		return TActionExt::SetWaypointTextBoxByType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetWaypointTextBoxByData:
		return TActionExt::SetWaypointTextBoxByData(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearWaypointTextBox:
		return TActionExt::ClearWaypointTextBox(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearAllWaypointTextBoxs:
		return TActionExt::ClearAllWaypointTextBoxs(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindAllTeamMemberToTag:
		return TActionExt::BindAllTeamMemberToTag(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindOwnerTeamMemberToTag:
		return TActionExt::BindOwnerTeamMemberToTag(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindAllTechnoTypeToTag:
		return TActionExt::BindAllTechnoTypeToTag(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindOwnerTechnoTypeToTag:
		return TActionExt::BindOwnerTechnoTypeToTag(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::GiveHouseMoney:
		return TActionExt::GiveHouseMoney(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::TakeHouseMoney:
		return TActionExt::TakeHouseMoney(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetHouseMoney:
		return TActionExt::SetHouseMoney(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::AddBaseNodeForHouseAtWaypoint:
		return TActionExt::AddBaseNodeForHouseAtWaypoint(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAllBaseNodeForHouseAtWaypoint:
		return TActionExt::RemoveAllBaseNodeForHouseAtWaypoint(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveBaseNodesOfBuildingTypeForHouse:
		return TActionExt::RemoveBaseNodesOfBuildingTypeForHouse(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::DestroyAllTagByTagTypeSafely:
		return TActionExt::DestroyAllTagByTagTypeSafely(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagToTechnoTypeAtWaypoint:
		return TActionExt::BindTagToTechnoTypeAtWaypoint(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagToTechnoTypeOfHouseAtWaypoint:
		return TActionExt::BindTagToTechnoTypeOfHouseAtWaypoint(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagToSpecificTechnoTypeWithinWaypointRange:
	 	return TActionExt::BindTagToSpecificTechnoTypeWithinWaypointRange(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagToSpecificTechnoTypeOfSpecificOwnerWithinWaypointRange:
	 	return TActionExt::BindTagToSpecificTechnoTypeOfSpecificOwnerWithinWaypointRange(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagToAllTechnoTypesWithinWaypointRange:
	 	return TActionExt::BindTagToAllTechnoTypesWithinWaypointRange(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagToAllTechnoTypesOfSpecificOwnerWithinWaypointRange:
		return TActionExt::BindTagToAllTechnoTypesOfSpecificOwnerWithinWaypointRange(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::UnifyAllInstancesOfSameTagType:
		return TActionExt::UnifyAllInstancesOfSameTagType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetRecruitableForFoot:
		return TActionExt::SetRecruitableForFoot(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagsToAllTechTypesInWaypointRangeExceptSpecified:
		return TActionExt::BindTagsToAllTechTypesInWaypointRangeExceptSpecified(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::BindTagsToAllTechTypesOfTriggerOwnerInWaypointRangeExceptSpecified:
		return TActionExt::BindTagsToAllTechTypesOfTriggerOwnerInWaypointRangeExceptSpecified(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::UpdateAllBuildingAnims:
		return TActionExt::UpdateAllBuildingAnims(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::UpdateAssociatedBuildingsAnims:
		return TActionExt::UpdateAssociatedBuildingsAnims(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::UpdateOwnerBuildingsAnimations:
		return TActionExt::UpdateOwnerBuildingsAnimations(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::CreateTeamConsideringLimits:
		return TActionExt::CreateTeamConsideringLimits(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RecruitNearbyFootToTeam:
		return TActionExt::RecruitNearbyFootToTeam(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetUnitTextBoxByTriggerType:
		return TActionExt::SetUnitTextBoxByTriggerType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetUnitTextBoxByTriggerData:
		return TActionExt::SetUnitTextBoxByTriggerData(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetUnitTextBoxByTeamType:
		return TActionExt::SetUnitTextBoxByTeamType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetUnitTextBoxByTeamData:
		return TActionExt::SetUnitTextBoxByTeamData(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearUnitTextBoxByType:
		return TActionExt::ClearUnitTextBoxByType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearUnitTextBoxByTag:
		return TActionExt::ClearUnitTextBoxByTag(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearUnitTextBoxByTechType:
		return TActionExt::ClearUnitTextBoxByTechType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearUnitTextBoxByHouseAndType:
		return TActionExt::ClearUnitTextBoxByHouseAndType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearUnitTextBoxByTeam:
		return TActionExt::ClearUnitTextBoxByTeam(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearAllUnitTextBoxs:
		return TActionExt::ClearAllUnitTextBoxs(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearAllTextBoxs:
		return TActionExt::ClearAllTextBoxs(pThis, pHouse, pObject, pTrigger, location);

	// ---- ChoiceBox Actions ----
	case ScaffoldTriggerAction::SetWaypointChoiceBox:
		return TActionExt::SetWaypointChoiceBox(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetScreenChoiceBox:
		return TActionExt::SetScreenChoiceBox(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearChoiceBoxByID:
		return TActionExt::ClearChoiceBoxByID(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ClearAllChoiceBoxs:
		return TActionExt::ClearAllChoiceBoxs(pThis, pHouse, pObject, pTrigger, location);

	// ---- Script Manipulation Actions ----
	case ScaffoldTriggerAction::ClearScript:
		return TActionExt::ClearScript(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::CopyScript:
		return TActionExt::CopyScript(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ModifyScriptByParam:
		return TActionExt::ModifyScriptByParam(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ModifyScriptByLocalVar:
		return TActionExt::ModifyScriptByLocalVar(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ModifyScriptByGlobalVar:
		return TActionExt::ModifyScriptByGlobalVar(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RebindTeamTypeScript:
		return TActionExt::RebindTeamTypeScript(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ResetTeamTypeScript:
		return TActionExt::ResetTeamTypeScript(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ResetAllTeamTypeScripts:
		return TActionExt::ResetAllTeamTypeScripts(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RestoreScriptContent:
		return TActionExt::RestoreScriptContent(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RestoreAllScriptContents:
		return TActionExt::RestoreAllScriptContents(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SeekTeamTypeScript:
		return TActionExt::SeekTeamTypeScript(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetTeamTypeMaxValue:
		return TActionExt::SetTeamTypeMaxValue(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RegisterFootPathVisualizer:
		return TActionExt::RegisterFootPathVisualizer(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::UnregisterFootPathVisualizer:
		return TActionExt::UnregisterFootPathVisualizer(pThis, pHouse, pObject, pTrigger, location);

	// ---- 任务简报 / 最佳时间 Actions ----
	case ScaffoldTriggerAction::SetMissionBriefing:
		return TActionExt::SetMissionBriefing(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetOverParTitle:
		return TActionExt::SetOverParTitle(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetOverParMessage:
		return TActionExt::SetOverParMessage(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetUnderParTitle:
		return TActionExt::SetUnderParTitle(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetUnderParMessage:
		return TActionExt::SetUnderParMessage(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetParTimeEasy:
		return TActionExt::SetParTimeEasy(pThis, pHouse, pObject, pTrigger, location);

	// ---- TaskForce Editing Actions ----
	case ScaffoldTriggerAction::ClearTaskForce:
		return TActionExt::ClearTaskForce(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::CopyTaskForce:
		return TActionExt::CopyTaskForce(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ModifyTaskForceEntry:
		return TActionExt::ModifyTaskForceEntry(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RebindTeamTypeTaskForce:
		return TActionExt::RebindTeamTypeTaskForce(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RestoreTaskForce:
		return TActionExt::RestoreTaskForce(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RestoreAllTaskForces:
		return TActionExt::RestoreAllTaskForces(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ResetTeamTypeTaskForce:
		return TActionExt::ResetTeamTypeTaskForce(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ResetAllTeamTypeTaskForces:
		return TActionExt::ResetAllTeamTypeTaskForces(pThis, pHouse, pObject, pTrigger, location);

	case ScaffoldTriggerAction::RecruitGroupToTeam:
		return TActionExt::RecruitGroupToTeam(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::UndeployHouseUnits:
		return TActionExt::UndeployHouseUnits(pThis, pHouse, pObject, pTrigger, location);

	case ScaffoldTriggerAction::SetParTimeMedium:
		return TActionExt::SetParTimeMedium(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetParTimeDifficult:
		return TActionExt::SetParTimeDifficult(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::SetGameSpeed:
		return TActionExt::SetGameSpeed(pThis, pHouse, pObject, pTrigger, location);

	case ScaffoldTriggerAction::DisableLoadGame:
		return TActionExt::DisableLoadGame(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::DisableSaveGame:
		return TActionExt::DisableSaveGame(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::EnableLoadGame:
		return TActionExt::EnableLoadGame(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::EnableSaveGame:
		return TActionExt::EnableSaveGame(pThis, pHouse, pObject, pTrigger, location);

	case ScaffoldTriggerAction::SellAllBuildingsOfHouse:
		return TActionExt::SellAllBuildingsOfHouse(pThis, pHouse, pObject, pTrigger, location);

	// ---- 触发组 / 随机触发 Actions ----
	case ScaffoldTriggerAction::RandomEnableTriggersByGroup:
		return TActionExt::RandomEnableTriggersByGroup(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RandomDisableTriggersByGroup:
		return TActionExt::RandomDisableTriggersByGroup(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RandomEnableTriggersByName:
		return TActionExt::RandomEnableTriggersByName(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RandomDisableTriggersByName:
		return TActionExt::RandomDisableTriggersByName(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::AddTriggerToGroupById:
		return TActionExt::AddTriggerToGroupById(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveTriggerFromGroupById:
		return TActionExt::RemoveTriggerFromGroupById(pThis, pHouse, pObject, pTrigger, location);

	// ---- AI 触发开关 ----
	case ScaffoldTriggerAction::EnableAITriggerById:
		return TActionExt::EnableAITriggerById(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::DisableAITriggerById:
		return TActionExt::DisableAITriggerById(pThis, pHouse, pObject, pTrigger, location);

	// ---- AttachEffect Actions ----
	case ScaffoldTriggerAction::ApplyAttachEffectToTeamType:
		return TActionExt::ApplyAttachEffectToTeamType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAttachEffectFromTeamType:
		return TActionExt::RemoveAttachEffectFromTeamType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAttachEffectByGroupFromTeamType:
		return TActionExt::RemoveAttachEffectByGroupFromTeamType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAllAttachEffectsFromTeamType:
		return TActionExt::RemoveAllAttachEffectsFromTeamType(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ApplyAttachEffectToAssociatedTechnos:
		return TActionExt::ApplyAttachEffectToAssociatedTechnos(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAttachEffectFromAssociatedTechnos:
		return TActionExt::RemoveAttachEffectFromAssociatedTechnos(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAttachEffectByGroupFromAssociatedTechnos:
		return TActionExt::RemoveAttachEffectByGroupFromAssociatedTechnos(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAllAttachEffectsFromAssociatedTechnos:
		return TActionExt::RemoveAllAttachEffectsFromAssociatedTechnos(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::ApplyAttachEffectToHouseTechnos:
		return TActionExt::ApplyAttachEffectToHouseTechnos(pThis, pHouse, pObject, pTrigger, location);
	case ScaffoldTriggerAction::RemoveAttachEffectFromHouseTechnos:
		return TActionExt::RemoveAttachEffectFromHouseTechnos(pThis, pHouse, pObject, pTrigger, location);

	// case ScaffoldTriggerAction::testAction:
	// 	return TActionExt::testAction(pThis, pHouse, pObject, pTrigger, location);

	default:
		bHandled = false;
		return true;
	}
}


// =============================
// container

TActionExt::ExtContainer::ExtContainer() : Container("TActionClass") { }

TActionExt::ExtContainer::~ExtContainer() = default;

