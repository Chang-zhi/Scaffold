#include "Body.h"
#include <New/FootPath/FootPathVisualizer.h>
#include <New/ChronoTeleport/ChronoTeleport.h>
#include <Ext/Script/Features/PatrolService.h>
#include <Ext/ScriptType/Body.h>
#include <Ext/Team/Body.h>
#include <Ext/Rules/Body.h>

#include <Interop/AttachEffectService.h>
#include <Interop/ScenarioVariables.h>

#include <CellSpread.h>
#include <Helpers/Cast.h>
#include <Utilities/Debug.h>
#include <algorithm>
#include <cmath>
#include <HouseClass.h>
#include <UnitClass.h>
#include <InfantryClass.h>
#include <InfantryTypeClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <SuperClass.h>
#include <SuperWeaponTypeClass.h>
#include <ScenarioClass.h>
#include <MapClass.h>
#include <RulesClass.h>
#include <TechnoTypeClass.h>
#include <CellClass.h>
#include <Unsorted.h>

// =============================
// ProcessAction - 主分发函数

void ScriptExt::ProcessAction(TeamClass* pTeam)
{
	if (!pTeam || !pTeam->CurrentScript)
		return;

	ChronoTeleportService::Tick(pTeam);

	// CurrentMission 可能被改写成 -1（如脚本重置），需防数组越界
	const auto pNodeIndex = pTeam->CurrentScript->CurrentMission;
	if (pNodeIndex < 0 || pNodeIndex >= ScriptTypeExt::ScriptActionCount)
		return;

	const auto& node = pTeam->CurrentScript->Type->ScriptActions[pNodeIndex];
	const int action = node.Action;

	auto const pExt = TeamExt::ExtMap.FindOrAllocate(pTeam);
	const bool fresh = (pNodeIndex != pExt->LastProcessedMission);
	pExt->LastProcessedMission = pNodeIndex;

	switch (static_cast<ScaffoldScripts>(action))
	{
	case ScaffoldScripts::DistributedLoadIntoTransports:
		ScriptExt::LoadIntoTransportsDistributed(pTeam);
		break;

	case ScaffoldScripts::RegisterFootPathVisualizer:
		ScriptExt::RegisterFootPathVisualizer(pTeam);
		break;

	case ScaffoldScripts::UnregisterFootPathVisualizer:
		ScriptExt::UnregisterFootPathVisualizer(pTeam);
		break;

	case ScaffoldScripts::ScatterAttack:
		ScriptExt::Mission_ScatterAttack(pTeam, -1);
		break;

	case ScaffoldScripts::ScatterAttackByAITargetTypes:
		ScriptExt::Mission_ScatterAttack(pTeam, static_cast<unsigned>(node.Argument) >> 16);
		break;

	case ScaffoldScripts::PatrolToEnemyBuildingNearby:
		ScriptExt::PatrolToBuildingNearby(
			pTeam, node.Argument & 0xFFFF, static_cast<unsigned>(node.Argument) >> 16, fresh, true);
		break;

	case ScaffoldScripts::PatrolToEnemyRally:
		ScriptExt::PatrolToRally(pTeam, fresh, true);
		break;

	case ScaffoldScripts::PatrolToFriendlyBuildingNearby:
		ScriptExt::PatrolToBuildingNearby(
			pTeam, node.Argument & 0xFFFF, static_cast<unsigned>(node.Argument) >> 16, fresh, false);
		break;

	case ScaffoldScripts::PatrolToFriendlyRally:
		ScriptExt::PatrolToRally(pTeam, fresh, false);
		break;

	case ScaffoldScripts::ApplyAttachEffect:
		// 参数打包/解码出错是"动作跑了却没效果"的常见来源, 所以原始 Argument 与解码结果都打出来。
		Debug::Log("[AE] Script %d ApplyAttachEffect: team=[%s] Argument=0x%08X -> effectKey=%d duration=%d\n",
			static_cast<int>(node.Action), pTeam->Type ? pTeam->Type->get_ID() : "<none>",
			static_cast<unsigned>(node.Argument), node.Argument & 0xFFFF, static_cast<unsigned>(node.Argument) >> 16);
		ScriptExt::ApplyAttachEffect(pTeam, node.Argument & 0xFFFF, static_cast<unsigned>(node.Argument) >> 16);
		break;

	case ScaffoldScripts::RemoveAttachEffect:
		Debug::Log("[AE] Script %d RemoveAttachEffect: team=[%s] Argument=0x%08X -> effectKey=%d\n",
			static_cast<int>(node.Action), pTeam->Type ? pTeam->Type->get_ID() : "<none>",
			static_cast<unsigned>(node.Argument), node.Argument & 0xFFFF);
		ScriptExt::RemoveAttachEffect(pTeam, node.Argument & 0xFFFF);
		break;

	case ScaffoldScripts::RemoveAllAttachEffects:
		Debug::Log("[AE] Script %d RemoveAllAttachEffects: team=[%s]\n",
			static_cast<int>(node.Action), pTeam->Type ? pTeam->Type->get_ID() : "<none>");
		ScriptExt::RemoveAllAttachEffects(pTeam);
		break;

	case ScaffoldScripts::ChronoTeleportToWaypoint:
		ChronoTeleportService::FireToWaypoint(pTeam, node.Argument & 0xFFFF);
		break;

	case ScaffoldScripts::EnterMissionForDuration:
		ScriptExt::EnterMissionForDuration(pTeam, node.Argument, fresh, pNodeIndex);
		break;

	case ScaffoldScripts::JumpToLineByLocalVariable:
		ScriptExt::JumpToLineByVariable(pTeam, node.Argument, ScenarioVariables::Scope::Local);
		break;

	case ScaffoldScripts::JumpToLineByGlobalVariable:
		ScriptExt::JumpToLineByVariable(pTeam, node.Argument, ScenarioVariables::Scope::Global);
		break;

	case ScaffoldScripts::WaitForLocalVariable:
		ScriptExt::WaitForVariable(pTeam, node.Argument, ScenarioVariables::Scope::Local, fresh);
		break;

	case ScaffoldScripts::WaitForGlobalVariable:
		ScriptExt::WaitForVariable(pTeam, node.Argument, ScenarioVariables::Scope::Global, fresh);
		break;

	case ScaffoldScripts::JumpToLineIfLocalVariableSet:
		ScriptExt::JumpToLineIfVariableSet(pTeam, node.Argument, ScenarioVariables::Scope::Local);
		break;

	case ScaffoldScripts::JumpToLineIfGlobalVariableSet:
		ScriptExt::JumpToLineIfVariableSet(pTeam, node.Argument, ScenarioVariables::Scope::Global);
		break;

	case ScaffoldScripts::JumpToLocalLineIfLocalSet:
		ScriptExt::JumpToVariableLineIfSet(pTeam, node.Argument,
			ScenarioVariables::Scope::Local, ScenarioVariables::Scope::Local);
		break;

	case ScaffoldScripts::JumpToGlobalLineIfGlobalSet:
		ScriptExt::JumpToVariableLineIfSet(pTeam, node.Argument,
			ScenarioVariables::Scope::Global, ScenarioVariables::Scope::Global);
		break;

	case ScaffoldScripts::JumpToLocalLineIfGlobalSet:
		ScriptExt::JumpToVariableLineIfSet(pTeam, node.Argument,
			ScenarioVariables::Scope::Global, ScenarioVariables::Scope::Local);
		break;

	case ScaffoldScripts::JumpToGlobalLineIfLocalSet:
		ScriptExt::JumpToVariableLineIfSet(pTeam, node.Argument,
			ScenarioVariables::Scope::Local, ScenarioVariables::Scope::Global);
		break;

	default:
		break;
	}
}

// 5514/5515: 行号取自剧本变量, 语义同原版动作 6(行号为 1 基)
// 原版 0x6E98B5 先 SetMission(Arg - 2) 再置 StepCompleted, 下一帧 NextMission 再 +1, 正好落在第 Arg 行
void ScriptExt::JumpToLineByVariable(TeamClass* pTeam, int variableIndex, ScenarioVariables::Scope scope)
{
	int line = 0;

	// 变量索引越界或变量存储不可用时直接放行, 避免整条脚本卡在这一行
	if (!ScenarioVariables::TryRead(scope, variableIndex, line))
	{
		Debug::Log("[Script] JumpToLine: variable read failed, scope=%d index=%d (valid [0, %d))\n",
			static_cast<int>(scope), variableIndex, ScenarioVariables::Count(scope));
		pTeam->StepCompleted = true;
		return;
	}

	// 0 与原版一致(脚本停止推进); 负数或超出脚本长度时跳过本行, 否则引擎会按越界行号取动作
	int const maxLine = std::min(pTeam->CurrentScript->Type->ActionsCount, ScriptTypeExt::ScriptActionCount);

	if (line < 0 || line > maxLine)
	{
		Debug::Log("[Script] JumpToLine: line %d out of range [1, %d], skip\n", line, maxLine);
		pTeam->StepCompleted = true;
		return;
	}

	Debug::Log("[Script] JumpToLine: team=[%s] %s var[%d]=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>",
		scope == ScenarioVariables::Scope::Global ? "global" : "local",
		variableIndex, line);

	pTeam->CurrentScript->SetMission(line - 2);
	pTeam->StepCompleted = true;
}

// 5516/5517: 变量为 0 时小队停在本行等待, 变为非 0 后执行下一条
void ScriptExt::WaitForVariable(TeamClass* pTeam, int variableIndex, ScenarioVariables::Scope scope, bool fresh)
{
	int value = 0;

	// 索引非法或变量存储不可用时不等待, 否则小队永久卡死
	if (!ScenarioVariables::TryRead(scope, variableIndex, value))
	{
		Debug::Log("[Script] WaitForVariable: variable read failed, scope=%d index=%d (valid [0, %d))\n",
			static_cast<int>(scope), variableIndex, ScenarioVariables::Count(scope));
		pTeam->StepCompleted = true;
		return;
	}

	if (value == 0)
	{
		pTeam->StepCompleted = false;

		if (fresh)
		{
			Debug::Log("[Script] WaitForVariable: team=[%s] %s var[%d]=0 -> waiting\n",
				pTeam->Type ? pTeam->Type->get_ID() : "<none>",
				scope == ScenarioVariables::Scope::Global ? "global" : "local",
				variableIndex);
		}

		return;
	}

	Debug::Log("[Script] WaitForVariable: team=[%s] %s var[%d]=%d -> continue\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>",
		scope == ScenarioVariables::Scope::Global ? "global" : "local",
		variableIndex, value);

	pTeam->StepCompleted = true;
}

void ScriptExt::JumpToLineIfVariableSet(TeamClass* pTeam, int argument, ScenarioVariables::Scope scope)
{
	int const variableIndex = argument & 0xFFFF;
	int const line = static_cast<unsigned>(argument) >> 16;
	int value = 0;

	if (!ScenarioVariables::TryRead(scope, variableIndex, value))
	{
		Debug::Log("[Script] JumpIfVariableSet: variable read failed, scope=%d index=%d (valid [0, %d))\n",
			static_cast<int>(scope), variableIndex, ScenarioVariables::Count(scope));
		pTeam->StepCompleted = true;
		return;
	}

	if (value == 0)
	{
		pTeam->StepCompleted = true;
		return;
	}

	int const maxLine = std::min(pTeam->CurrentScript->Type->ActionsCount, ScriptTypeExt::ScriptActionCount);

	if (line > maxLine)
	{
		Debug::Log("[Script] JumpIfVariableSet: line %d out of range [1, %d], skip\n", line, maxLine);
		pTeam->StepCompleted = true;
		return;
	}

	Debug::Log("[Script] JumpIfVariableSet: team=[%s] %s var[%d]=%d -> line %d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>",
		scope == ScenarioVariables::Scope::Global ? "global" : "local",
		variableIndex, value, line);

	pTeam->CurrentScript->SetMission(line - 2);
	pTeam->StepCompleted = true;
}

void ScriptExt::JumpToVariableLineIfSet(TeamClass* pTeam, int argument,
	ScenarioVariables::Scope conditionScope, ScenarioVariables::Scope lineScope)
{
	int const conditionIndex = argument & 0xFFFF;
	int const lineIndex = static_cast<unsigned>(argument) >> 16;
	int value = 0;

	if (!ScenarioVariables::TryRead(conditionScope, conditionIndex, value))
	{
		Debug::Log("[Script] JumpToVariableLine: A read failed, scope=%d index=%d (valid [0, %d))\n",
			static_cast<int>(conditionScope), conditionIndex, ScenarioVariables::Count(conditionScope));
		pTeam->StepCompleted = true;
		return;
	}

	if (value == 0)
	{
		pTeam->StepCompleted = true;
		return;
	}

	int line = 0;

	if (!ScenarioVariables::TryRead(lineScope, lineIndex, line))
	{
		Debug::Log("[Script] JumpToVariableLine: B read failed, scope=%d index=%d (valid [0, %d))\n",
			static_cast<int>(lineScope), lineIndex, ScenarioVariables::Count(lineScope));
		pTeam->StepCompleted = true;
		return;
	}

	int const maxLine = std::min(pTeam->CurrentScript->Type->ActionsCount, ScriptTypeExt::ScriptActionCount);

	if (line < 0 || line > maxLine)
	{
		Debug::Log("[Script] JumpToVariableLine: line %d out of range [1, %d], skip\n", line, maxLine);
		pTeam->StepCompleted = true;
		return;
	}

	Debug::Log("[Script] JumpToVariableLine: team=[%s] %s[%d]=%d -> %s[%d]=%d -> line %d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>",
		conditionScope == ScenarioVariables::Scope::Global ? "global" : "local", conditionIndex, value,
		lineScope == ScenarioVariables::Scope::Global ? "global" : "local", lineIndex, line);

	pTeam->CurrentScript->SetMission(line - 2);
	pTeam->StepCompleted = true;
}

// 5513: 全队进入指定 Mission 并保持 N 秒，低 16 位 Mission、高 16 位秒数
void ScriptExt::EnterMissionForDuration(TeamClass* pTeam, int argument, bool fresh, int nodeIndex)
{
	auto const pExt = TeamExt::ExtMap.FindOrAllocate(pTeam);
	const int missionIndex = argument & 0xFFFF;
	const int duration = static_cast<int>(static_cast<unsigned>(argument) >> 16) * 15; // 原版 15 帧/秒

	// 状态码非法时直接放行，否则整条脚本卡死在本行
	if (missionIndex > static_cast<int>(Mission::SpyplaneOverfly))
	{
		Debug::Log("[Script] EnterMissionForDuration: invalid mission %d (Argument=0x%08X)\n",
			missionIndex, argument);
		pExt->TimedMissionNode = -1;
		pExt->TimedMissionLeft = 0;
		pTeam->StepCompleted = true;
		return;
	}

	const auto mission = static_cast<Mission>(missionIndex);

	if (fresh || pExt->TimedMissionNode != nodeIndex)
	{
		pExt->TimedMissionNode = nodeIndex;
		pExt->TimedMissionLeft = duration;
		pExt->TimedMissionLastFrame = Unsorted::CurrentFrame;
	}
	else if (pExt->TimedMissionLeft > 0)
	{
		int const now = Unsorted::CurrentFrame;
		if (pExt->TimedMissionLastFrame < 0 || now < pExt->TimedMissionLastFrame)
		{
			pExt->TimedMissionLastFrame = now;
		}
		else
		{
			pExt->TimedMissionLeft -= now - pExt->TimedMissionLastFrame;
			pExt->TimedMissionLastFrame = now;
		}
	}

	// 不调用 DoMissionFromScript：它要求队员 Target / Destination 均为空，
	// 刚走完移动动作的队伍 Destination 还在，整队会被跳过。
	int total = 0;
	int assigned = 0;

	for (auto pFoot = pTeam->FirstUnit; pFoot; pFoot = pFoot->NextTeamMember)
	{
		if (!pFoot->IsAlive || pFoot->Health <= 0 || pFoot->InLimbo || !pFoot->IsOnMap || pFoot->Transporter)
			continue;

		++total;

		if (pFoot->GetCurrentMission() == mission || pFoot->QueuedMission == mission)
			continue;

		// 首次下达无条件生效；之后补漏只在未交战时补，避免把攻击拉回
		if (!fresh && pFoot->Target)
			continue;

		// 先切状态再清旧目的地，否则队员会继续走向上一条动作的路径点
		pFoot->QueueMission(mission, true);
		pFoot->SetDestination(nullptr, true);
		++assigned;
	}

	if (fresh)
	{
		Debug::Log("[Script] EnterMissionForDuration: team=[%s] mission=%d frames=%d members=%d assigned=%d\n",
			pTeam->Type ? pTeam->Type->get_ID() : "<none>", missionIndex, duration, total, assigned);
	}

	if (pExt->TimedMissionLeft > 0)
	{
		pTeam->StepCompleted = false;
	}
	else
	{
		pExt->TimedMissionLeft = 0;
		pTeam->StepCompleted = true;
	}
}

// 散开指定格子上非载具自身的己方单位
static void ScatterBlockersOnCell(CellStruct cell, HouseClass* owner, TechnoClass* exclude = nullptr)
{
	auto pCell = MapClass::Instance.TryGetCellAt(cell);
	if (!pCell) return;

	for (auto pObj = pCell->GetContent(); pObj; pObj = pObj->NextObject)
	{
		auto pBlocking = generic_cast<TechnoClass*>(pObj);
		if (!pBlocking || !pBlocking->IsAlive || pBlocking->InLimbo)
			continue;
		if (pBlocking == exclude)
			continue;
		if (pBlocking->Owner != owner)
			continue;
		if (pBlocking->Transporter)
			continue;
		// 正在前往该载具的单位不散开（避免打断登车流程）
		if (exclude)
		{
			if (FootClass* pFoot = generic_cast<FootClass*>(pBlocking))
			{
				if (pFoot->Destination == exclude)
					continue;
			}
		}
		pBlocking->Scatter(pBlocking->GetCoords(), true, false);
	}
}

// =============================
// DistributedLoadIntoTransports - 分布式装载
// 各载具轮流挑选最近的队员，确保均匀分布
// 一次性分配所有配对，然后等待全部登车

// =============================
// RegisterFootPathVisualizer - 将小队所有成员注册到路径可视化
// =============================

void ScriptExt::RegisterFootPathVisualizer(TeamClass* pTeam)
{
	FootPathVisualizer::RegisterTeam(pTeam);
}

// =============================
// UnregisterFootPathVisualizer - 将小队所有成员从路径可视化移除
// =============================

void ScriptExt::UnregisterFootPathVisualizer(TeamClass* pTeam)
{
	FootPathVisualizer::UnregisterTeam(pTeam);
}

void ScriptExt::LoadIntoTransportsDistributed(TeamClass* pTeam)
{
	HouseClass* const pOwner = pTeam->Owner;

	// 检查是否还有人在装载中，等待
	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (pUnit->GetCurrentMission() == Mission::Enter)
		{
			pTeam->StepCompleted = false;
			return;
		}
	}

	// 每帧清空载具格上的阻塞者
	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		auto pType = pUnit->GetTechnoType();
		if (!pType || pType->Passengers <= 0)
			continue;
		ScatterBlockersOnCell(CellClass::Coord2Cell(pUnit->GetCoords()), pOwner, pUnit);
	}

	// 收集小队内所有有空位的载具
	struct TransportInfo
	{
		FootClass* Vehicle;
		CellStruct Cell;
		int UsedCapacity;
		int MaxCapacity;
	};
	std::vector<TransportInfo> transports;

	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		auto const pType = pUnit->GetTechnoType();
		if (pType->Passengers > 0)
		{
			int used = pUnit->Passengers.GetTotalSize();
			if (used < pType->Passengers)
			{
				auto cell = CellClass::Coord2Cell(pUnit->GetCoords());
				transports.push_back({ pUnit, cell, used, pType->Passengers });
			}
		}
	}

	if (transports.empty())
	{
		return;
	}

	// SizeLimit 小的载具优先挑选；stable_sort 保证并列时顺序各机一致
	std::stable_sort(transports.begin(), transports.end(), [](const TransportInfo& a, const TransportInfo& b) {
		return a.Vehicle->GetTechnoType()->SizeLimit < b.Vehicle->GetTechnoType()->SizeLimit;
	});

	// 统计还有多少非载具队员可分配
	int nonTransportCount = 0;
	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (pUnit->Transporter || pUnit->InLimbo || pUnit->Health <= 0)
			continue;
		if (pUnit->WhatAmI() == AbstractType::AircraftType)
			continue;
		auto pType = pUnit->GetTechnoType();
		if (!pType || pType->ConsideredAircraft)
			continue;
		if (pType->Passengers > 0)
			continue;
		if (pUnit->IsInAir())
			continue;
		nonTransportCount++;
	}

	// 收集所有需要上车的队员（尚未在 Enter 状态的）
	struct UnitInfo
	{
		FootClass* Unit;
		CellStruct Cell;
		int Size;
		bool Assigned;
	};
	std::vector<UnitInfo> units;

	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (pUnit->Transporter || pUnit->InLimbo || pUnit->Health <= 0)
		{
			continue;
		}
		if (pUnit->WhatAmI() == AbstractType::AircraftType)
		{
			continue;
		}

		auto pUnitType = pUnit->GetTechnoType();
		if (!pUnitType || pUnitType->ConsideredAircraft)
		{
			continue;
		}

		// 还有非载具队员可分配时，有空位的载具仍作为司机，跳过
		// 没有非载具队员了，"匹配不到成员" 则作为乘客
		if (pUnitType->Passengers > 0 && nonTransportCount > 0)
		{
			int used = pUnit->Passengers.GetTotalSize();
			if (used < pUnitType->Passengers)
			{
				continue;
			}
		}
		if (pUnit->IsInAir())
		{
			continue;
		}

		int unitSize = static_cast<int>(pUnitType->Size);
		if (unitSize <= 0) unitSize = 1;

		auto unitCell = pUnit->GetMapCoords();
		units.push_back({ pUnit, unitCell, unitSize, false });
	}

	if (units.empty())
	{
		return;
	}

	// 按 SizeLimit 分组轮询：同组内轮询装满，再下一组
	int totalAssigned = 0;
	size_t groupStart = 0;
	while (groupStart < transports.size())
	{
		double groupLimit = transports[groupStart].Vehicle->GetTechnoType()->SizeLimit;
		size_t groupEnd = groupStart + 1;
		while (groupEnd < transports.size() &&
			transports[groupEnd].Vehicle->GetTechnoType()->SizeLimit == groupLimit)
			groupEnd++;

		bool anyAssigned = true;
		while (anyAssigned)
		{
			anyAssigned = false;
			for (size_t ti = groupStart; ti < groupEnd; ti++)
			{
				auto& t = transports[ti];
				if (t.UsedCapacity >= t.MaxCapacity)
					continue;

				int bestIdx = -1;
				int bestDist = INT_MAX;

				for (size_t i = 0; i < units.size(); ++i)
				{
					auto& u = units[i];
					if (u.Assigned)
						continue;
					if (!u.Unit->IsAlive || u.Unit->InLimbo || u.Unit->Health <= 0 || u.Unit->Transporter)
						continue;
					if (u.Size > t.MaxCapacity - t.UsedCapacity)
						continue;
					if (u.Size > static_cast<int>(t.Vehicle->GetTechnoType()->SizeLimit))
						continue;

					// 不能上自己
					if (u.Unit == t.Vehicle)
						continue;
					// 目标载具坐标上有其他单位，散开阻塞者
					ScatterBlockersOnCell(t.Cell, pOwner, t.Vehicle);
					int dist = CellSpread::GetDistance(CellStruct{
						static_cast<short>(u.Cell.X - t.Cell.X),
						static_cast<short>(u.Cell.Y - t.Cell.Y)
					});
					if (dist < bestDist || (bestIdx >= 0 && dist == bestDist && u.Size < units[bestIdx].Size))
					{
						bestDist = dist;
						bestIdx = static_cast<int>(i);
					}
				}

				if (bestIdx >= 0)
				{
					auto& u = units[bestIdx];

					if (auto pUnit = abstract_cast<UnitClass*>(u.Unit))
					{
						if (pUnit->Deployed)
							pUnit->ForceMission(Mission::Unload);
					}
					else if (auto pInf = abstract_cast<InfantryClass*>(u.Unit))
					{
						if (pInf->IsDeployed())
							pInf->ForceMission(Mission::Unload);
					}

					u.Unit->QueueMission(Mission::Enter, false);
					u.Unit->SetTarget(nullptr);
					u.Unit->SetDestination(t.Vehicle, true);

					t.UsedCapacity += u.Size;
					u.Assigned = true;
					++totalAssigned;
					anyAssigned = true;
				}
			}
		}

		groupStart = groupEnd;
	}

	if (totalAssigned > 0)
		pTeam->StepCompleted = false;
	// 如果 totalAssigned == 0，说明没有可分配的，Scaffold 已设 StepCompleted=true
}

// =============================
// 目标可攻击性判定（对齐原版索敌）
//
// 原版链路: Mission_Hunt -> GreatestThreat(0x6F8DF0) -> CanAutoTargetObject(0x6F7CA0)
// 在 CanAutoTargetObject 中，引擎对每个候选目标执行:
//     GetFireErrorWithoutRange(target, weaponIndex) == FireError::ILLEGAL(5) -> 剔除
// GetFireErrorWithoutRange 即 GetFireError(..., ignoreRange) 的转发（vtbl 239/240，
// UnitClass=0x740FD0 / InfantryClass=0x51C8B0），其内部（sub_6FC0B0）对目标做:
//     目标 IsInAir() 且武器弹道无 AA 标志 -> ILLEGAL
//     目标非 IsInAir() 且武器弹道无 AG 标志 -> ILLEGAL
// 因此 ILLEGAL 精确表示"该单位无论如何都打不到此目标"（地对空/空对地弹道
// 不匹配、非法目标类型、已部署等），而 AMMO/RANGE/FACING/BUSY 等属于瞬时
// 状态，不应作为剔除依据。ignoreRange = true 表示忽略射程只判定合法性，
// 与引擎一致：打不到才剔除，够不着仍然算有效目标（交由移动/寻路处理）。
// =============================

static bool CanEngageTarget(FootClass* pFoot, TechnoClass* pTarget)
{
	if (!pFoot || !pTarget)
		return false;

	// 与引擎一致：逐个武器槽判定；空武器槽不参与
	for (int i = 0; i < 2; ++i)
	{
		const auto pWeapon = pFoot->GetWeapon(i);
		if (!pWeapon || !pWeapon->WeaponType)
			continue;

		if (pFoot->GetFireError(pTarget, i, true) != FireError::ILLEGAL)
			return true;
	}

	return false;
}



void ScriptExt::Mission_ScatterAttack(TeamClass* pTeam, int attackAITargetType)
{
	auto const pExt = TeamExt::ExtMap.FindOrAllocate(pTeam);

	// [AITargetTypes] 索引越界：没有可用的自定义目标列表，直接推进脚本
	if (attackAITargetType >= 0
		&& attackAITargetType >= static_cast<int>(RulesExt::Global()->AITargetTypesLists.size()))
	{
		Debug::Log("[Developer warning] ScatterAttackByAITargetTypes: [AITargetTypes] index %d out of range (Count: %d)\n",
			attackAITargetType, static_cast<int>(RulesExt::Global()->AITargetTypesLists.size()));
		pExt->ScatterAttackGroups.clear();
		return;
	}

	// 收集可用成员（存活、在地图上、未搭载）
	std::vector<FootClass*> members;
	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (!pUnit->IsAlive || pUnit->Health <= 0 || pUnit->InLimbo || !pUnit->IsOnMap)
			continue;
		if (pUnit->Transporter)
			continue;
		members.push_back(pUnit);
	}

	// 无可用成员 -> 清空分组，推进脚本
	if (members.empty())
	{
		pExt->ScatterAttackGroups.clear();
		return;
	}

	// 一次性分组：首次进入本动作（或读档后/完成一轮后）按方位角排序均分
	if (pExt->ScatterAttackGroups.empty())
	{
		// 分组数：低 16 位（至少 1 组），超过成员数时按成员数（每人一组）
		int groupCount = pTeam->CurrentScript->Type->ScriptActions[pTeam->CurrentScript->CurrentMission].Argument & 0xFFFF;
		if (groupCount < 1) groupCount = 1;
		if (groupCount > static_cast<int>(members.size()))
			groupCount = static_cast<int>(members.size());

		// 小队中心（用于方位排序）
		double teamCenterX = 0.0;
		double teamCenterY = 0.0;
		for (auto pFoot : members)
		{
			const CoordStruct c = pFoot->GetCoords();
			teamCenterX += c.X;
			teamCenterY += c.Y;
		}
		teamCenterX /= members.size();
		teamCenterY /= members.size();

		// 按成员相对小队中心的方位角排序，再连续等分到各组：
		// 每组占据一个方向扇区，使各组向不同方向分散。
		// stable_sort：方位角并列时保持原顺序，各机一致
		std::stable_sort(members.begin(), members.end(), [&](FootClass* a, FootClass* b) {
			const CoordStruct ca = a->GetCoords();
			const CoordStruct cb = b->GetCoords();
			const double ba = std::atan2(static_cast<double>(ca.Y) - teamCenterY, static_cast<double>(ca.X) - teamCenterX);
			const double bb = std::atan2(static_cast<double>(cb.Y) - teamCenterY, static_cast<double>(cb.X) - teamCenterX);
			return ba < bb;
		});

		// 均分：前 extra 组每组多 1 人
		const int groupBase = static_cast<int>(members.size()) / groupCount;
		const int groupExtra = static_cast<int>(members.size()) % groupCount;

		int begin = 0;
		for (int g = 0; g < groupCount; ++g)
		{
			const int groupSize = groupBase + (g < groupExtra ? 1 : 0);
			pExt->ScatterAttackGroups.emplace_back(members.begin() + begin, members.begin() + begin + groupSize);
			begin += groupSize;
		}
	}

	// 每帧检查组成员是否仍然有效：不在当前可用成员中的移除，空组移除
	for (auto it = pExt->ScatterAttackGroups.begin(); it != pExt->ScatterAttackGroups.end(); )
	{
		auto& group = *it;
		group.erase(std::remove_if(group.begin(), group.end(), [&](FootClass* pFoot) {
			return std::find(members.begin(), members.end(), pFoot) == members.end();
		}), group.end());

		if (group.empty())
			it = pExt->ScatterAttackGroups.erase(it);
		else
			++it;
	}

	if (pExt->ScatterAttackGroups.empty())
	{
		pExt->ScatterAttackGroups.clear(); // 所有组都已清空（成员全部失效）
		return;
	}

	// 新加入的成员（不在任何组中）并入成员最少的组，保持分组不重排
	for (auto pFoot : members)
	{
		bool inGroup = false;
		for (auto& group : pExt->ScatterAttackGroups)
		{
			if (std::find(group.begin(), group.end(), pFoot) != group.end())
			{
				inGroup = true;
				break;
			}
		}
		if (!inGroup)
		{
			size_t bestIdx = 0;
			size_t bestSize = pExt->ScatterAttackGroups[0].size();
			for (size_t gi = 1; gi < pExt->ScatterAttackGroups.size(); ++gi)
			{
				if (pExt->ScatterAttackGroups[gi].size() < bestSize)
				{
					bestSize = pExt->ScatterAttackGroups[gi].size();
					bestIdx = gi;
				}
			}
			pExt->ScatterAttackGroups[bestIdx].push_back(pFoot);
		}
	}

	const int targetMask = static_cast<unsigned>(pTeam->CurrentScript->Type->ScriptActions[pTeam->CurrentScript->CurrentMission].Argument) >> 16;

	constexpr int threatMode = 0;

	// 本组已锁定的目标集合（不同组优先使用不同目标）
	std::vector<TechnoClass*> assignedTargets;

	// 每组一份的索敌上下文：组中心 + 组内是否带渗透特工/工程师（对齐上游）
	auto const computeGroupContext = [](const std::vector<FootClass*>& group, CoordStruct& groupCenter) -> bool
	{
		groupCenter = CoordStruct{ 0, 0, 0 };

		for (auto pFoot : group)
		{
			const CoordStruct c = pFoot->GetCoords();
			groupCenter.X += c.X;
			groupCenter.Y += c.Y;
			groupCenter.Z += c.Z;
		}

		const int groupSize = static_cast<int>(group.size());
		groupCenter.X /= groupSize;
		groupCenter.Y /= groupSize;
		groupCenter.Z /= groupSize;

		// agentMode：组内有渗透特工或工程师时跳过部分武器/免疫判定（对齐上游）
		for (auto pFoot : group)
		{
			if (pFoot->WhatAmI() == AbstractType::Infantry)
			{
				const auto pTypeInf = static_cast<InfantryTypeClass*>(pFoot->GetTechnoType());

				if ((pTypeInf->Agent && pTypeInf->Infiltrate) || pTypeInf->Engineer)
					return true;
			}
		}

		return false;
	};

	// 本组索敌：GreatestThreat + 排除已分配/不可达目标 + 可达性寻路
	auto const selectGroupTarget = [&](const std::vector<FootClass*>& group, FootClass* const pLeader,
		const CoordStruct& groupCenter, bool agentMode) -> TechnoClass*
	{
		std::vector<TechnoClass*> unreachableTargets;

		while (true)
		{
			// 首选：排除"已分配给其他组"和"不可用"的目标
			std::vector<TechnoClass*> excludedTargets = assignedTargets;
			excludedTargets.insert(excludedTargets.end(), unreachableTargets.begin(), unreachableTargets.end());

			TechnoClass* candidate = ScriptExt::GreatestThreat(pLeader, targetMask, threatMode,
				nullptr, agentMode, &excludedTargets, &group, &groupCenter, attackAITargetType);

			// 目标数少于分组数时退化为允许共用目标（只排除不可用的），与原行为一致
			if (!candidate && !assignedTargets.empty())
			{
				candidate = ScriptExt::GreatestThreat(pLeader, targetMask, threatMode,
					nullptr, agentMode, &unreachableTargets, &group, &groupCenter, attackAITargetType);
			}

			if (!candidate)
				break; // 候选耗尽：没有可打的目标了

			// 组内至少一名成员能实际攻击该目标，否则视为不可用目标剔除
			// （对齐原版索敌 GetFireError != ILLEGAL：排除地对空/空对地弹道
			//  不匹配等打不到的目标，避免把目标锁给打不到它的单位导致挂机）
			bool hasShooter = false;
			for (auto pFoot : group)
			{
				if (CanEngageTarget(pFoot, candidate))
				{
					hasShooter = true;
					break;
				}
			}

			if (!hasShooter)
			{
				unreachableTargets.push_back(candidate); // 全组都打不到，剔除后重选
				continue;
			}

			// 组内已有成员正在攻击该目标 -> 视为可抵达（正在交战中），无需寻路检查
			bool alreadyEngaging = false;
			for (auto pFoot : group)
			{
				if (pFoot->Target == candidate)
				{
					alreadyEngaging = true;
					break;
				}
			}

			if (alreadyEngaging || pLeader->UpdatePathfinding(candidate->GetMapCoords(), false, 0))
				return candidate;

			unreachableTargets.push_back(candidate); // 不可抵达，剔除后重选
		}

		return nullptr;
	};

	// 对齐原版行为：锁定一次后不再做周期性重锁，仅当组锁定目标
	// 失效（死亡/离场/不再敌对）时才重新索敌，其余情况保持原有锁定不变。
	bool anyGroupActionable = false;

	for (auto& group : pExt->ScatterAttackGroups)
	{
		FootClass* const pLeader = group.front();

		// 组目标沿用组员当前仍有效的锁定
		TechnoClass* pGroupTarget = nullptr;
		for (auto pFoot : group)
		{
			TechnoClass* const pCurrent = abstract_cast<TechnoClass*>(pFoot->Target);
			if (pCurrent && pCurrent->Health > 0 && !pCurrent->InLimbo && pCurrent->IsOnMap
				&& !pFoot->Owner->IsAlliedWith(pCurrent->Owner))
			{
				pGroupTarget = pCurrent;
				break;
			}
		}

		// 组锁定目标失效 -> 本组重新索敌
		if (!pGroupTarget)
		{
			CoordStruct groupCenter;
			const bool agentMode = computeGroupContext(group, groupCenter);
			pGroupTarget = selectGroupTarget(group, pLeader, groupCenter, agentMode);
		}

		if (!pGroupTarget)
			continue; // 本组找不到可抵达的目标，保持现状

		anyGroupActionable = true;

		// 组内所有成员锁定同一个目标（新入队/被打散成员在此补锁到组目标）
		for (auto pFoot : group)
		{
			// 无法攻击该目标的成员原地警戒，不锁定目标
			// （CanEngageTarget 已覆盖主/副两个武器槽，无需再判 IsArmed）
			if (!CanEngageTarget(pFoot, pGroupTarget))
			{
				if (pFoot->GetCurrentMission() != Mission::Guard)
					pFoot->QueueMission(Mission::Guard, false);
				continue;
			}

			// 已在攻击组目标 -> 保持，不打断
			if (pFoot->Target == pGroupTarget)
				continue;

			pFoot->SetTarget(pGroupTarget);
			pFoot->QueueMission(Mission::Attack, true);
		}

		// 本组已锁定该目标，后续分组优先不选它
		assignedTargets.push_back(pGroupTarget);
	}

	// 所有组都找不到可抵达的目标 -> 敌人均无法抵达，标记完成（清空分组，推进脚本）
	if (!anyGroupActionable)
	{
		pExt->ScatterAttackGroups.clear();
		return;
	}

	// 仍有可作战的目标 -> 保持在本动作，直到最后一个敌人被摧毁
	pTeam->StepCompleted = false;
}

// ============================================================================
// 巡逻系(动作 5504~5507)辅助与实现
// ============================================================================
namespace
{
	// 动作 Argument 编码(对齐原版动作 47):
	//   高 16 位 = 建筑类型索引(0 = 第一个类型,如 GAPOWR;0xFFFF = 任意类型)
	//   低 16 位 = 索敌方式:0 最小威胁 / 1 最大威胁 / 2 最近 / 3 最远
	enum class TargetSelection : int
	{
		MinimumThreat = 0,
		MaximumThreat = 1,
		Closest = 2,
		Farthest = 3,
	};

	// 找指定阵营的目标建筑,按索敌方式从候选集中选出。
	// typeIndex:BuildingTypeClass::Array 索引;0xFFFF = 任意类型
	// wantEnemy:true = 属于 HouseClass::EnemyHouseIndex 指向的敌方阵营;
	//            false = 团队所属方自己的建筑(TeamClass::Owner)
	static BuildingClass* FindTeamBuilding(TeamClass* pTeam, int typeIndex, TargetSelection selection, bool wantEnemy)
	{
		if (!pTeam || !pTeam->Owner)
			return nullptr;

		BuildingTypeClass* pType = nullptr;
		if (typeIndex != 0xFFFF)
		{
			if (typeIndex < 0 || typeIndex >= BuildingTypeClass::Array.Count)
				return nullptr; // 类型索引无效
			pType = BuildingTypeClass::Array.GetItem(typeIndex);
		}

		HouseClass* const pOwner = pTeam->Owner;
		CoordStruct teamPos{ 0, 0, 0 };
		if (pTeam->FirstUnit)
			teamPos = pTeam->FirstUnit->GetCoords();

		BuildingClass* best = nullptr;
		long long bestScore = 0;
		bool hasBest = false;

		for (BuildingClass* pBuilding : BuildingClass::Array)
		{
			if (!pBuilding || !pBuilding->IsAlive || pBuilding->InLimbo)
				continue;
			if (!pBuilding->Owner)
				continue;
			if (pType && pBuilding->GetType() != static_cast<ObjectTypeClass*>(pType))
				continue;

			const bool isOwn = (pBuilding->Owner == pOwner);
			const bool isEnemy = (pBuilding->Owner->ArrayIndex == pOwner->EnemyHouseIndex);
			if (wantEnemy ? !isEnemy : !isOwn)
				continue;

			long long score = 0;
			switch (selection)
			{
			case TargetSelection::MinimumThreat:
				score = -pBuilding->GetThreatValue();
				break;
			case TargetSelection::MaximumThreat:
				score = pBuilding->GetThreatValue();
				break;
			case TargetSelection::Closest:
			default:
			{
				const CoordStruct pos = pBuilding->GetCoords();
				const long long dx = pos.X - teamPos.X;
				const long long dy = pos.Y - teamPos.Y;
				score = -(dx * dx + dy * dy);
				break;
			}
			case TargetSelection::Farthest:
			{
				const CoordStruct pos = pBuilding->GetCoords();
				const long long dx = pos.X - teamPos.X;
				const long long dy = pos.Y - teamPos.Y;
				score = dx * dx + dy * dy;
				break;
			}
			}

			if (!hasBest || score > bestScore)
			{
				bestScore = score;
				best = pBuilding;
				hasBest = true;
			}
		}

		return best;
	}

	// 敌方阵营:HouseClass::EnemyHouseIndex 指向的"当前敌人"
	// (原版 HouseClass+0x5600;= -1 表示当前无敌人)
	static HouseClass* FindEnemyHouse(TeamClass* pTeam)
	{
		if (!pTeam || !pTeam->Owner)
			return nullptr;

		const int enemyIdx = pTeam->Owner->EnemyHouseIndex;
		if (enemyIdx < 0 || enemyIdx >= HouseClass::Array.Count)
			return nullptr; // 当前没有敌人

		HouseClass* pHouse = HouseClass::Array.GetItem(enemyIdx);
		if (!pHouse || pHouse->ArrayIndex != enemyIdx)
			return nullptr; // 索引与实例不一致(防御)

		return pHouse;
	}
}

void ScriptExt::PatrolToBuildingNearby(TeamClass* pTeam, int typeIndex, int selectionMode, bool fresh, bool wantEnemy)
{
	if (!pTeam->FirstUnit)
	{
		Debug::Log("[PT] skip(no member) type=%d sel=%d enemy=%d\n", typeIndex, selectionMode, wantEnemy);
		return; // 无成员 → 保持 StepCompleted,跳过本条
	}

	BuildingClass* pTarget = FindTeamBuilding(
		pTeam, typeIndex, static_cast<TargetSelection>(selectionMode & 0x3), wantEnemy);
	if (!pTarget)
	{
		Debug::Log("[PT] skip(no target) type=%d sel=%d enemy=%d EHI=%d\n",
			typeIndex, selectionMode, wantEnemy,
			pTeam->Owner ? pTeam->Owner->EnemyHouseIndex : -999);
		return; // 无目标建筑 → 保持 StepCompleted,跳过本条
	}

	if (!PatrolService::AIBuildingNearby(pTeam, pTarget, 2, fresh))
	{
		Debug::Log("[PT] skip(AI fail) target=%s\n",
			static_cast<TechnoTypeClass*>(pTarget->GetType())->get_ID());
		return; // 建筑失效 / 目标格无效 → 保持 StepCompleted,跳过本条
	}

	if (fresh)
	{
		Debug::Log("[PT] patrolling to building %s (type=%d sel=%d enemy=%d)\n",
			static_cast<TechnoTypeClass*>(pTarget->GetType())->get_ID(),
			typeIndex, selectionMode, wantEnemy);
	}

	// 巡逻进行中:留在本动作,等待就位检查完成。
	// 落点 = 建筑格 + 向团队方向偏移 2 格(避免目标格被建筑本体占据导致寻路/异常分支)
	pTeam->StepCompleted = false;
}

void ScriptExt::PatrolToRally(TeamClass* pTeam, bool fresh, bool wantEnemy)
{
	if (!pTeam->FirstUnit)
	{
		Debug::Log("[PT] skip(no member) rally enemy=%d\n", wantEnemy);
		return; // 无成员 → 保持 StepCompleted,跳过本条
	}

	HouseClass* pHouse = wantEnemy ? FindEnemyHouse(pTeam) : pTeam->Owner;
	if (!pHouse)
	{
		Debug::Log("[PT] skip(no house) rally enemy=%d EHI=%d\n",
			wantEnemy, pTeam->Owner ? pTeam->Owner->EnemyHouseIndex : -999);
		return; // 无敌方阵营(或己方无效)→ 保持 StepCompleted,跳过本条
	}

	if (!PatrolService::AIRally(pTeam, pHouse, fresh))
	{
		Debug::Log("[PT] skip(AI fail) rally house=%s\n", pHouse->get_ID());
		return; // 阵营无基地 → 保持 StepCompleted,跳过本条
	}

	if (fresh)
	{
		Debug::Log("[PT] patrolling to rally house=%s base=(%d,%d)\n",
			pHouse->get_ID(), pHouse->BaseCenter.X, pHouse->BaseCenter.Y);
	}

	// 巡逻进行中:留在本动作
	pTeam->StepCompleted = false;
}

void ScriptExt::ApplyAttachEffect(TeamClass* pTeam, int nameIndex, int durationOverride)
{
	const int attached = AttachEffectService::ApplyToTeam(pTeam, nameIndex, durationOverride);

	Debug::Log("[AE] Script ApplyAttachEffect: team=[%s] effectKey=%d duration=%d -> attached=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", nameIndex, durationOverride, attached);
}

void ScriptExt::RemoveAttachEffect(TeamClass* pTeam, int nameIndex)
{
	const int removed = AttachEffectService::RemoveFromTeam(pTeam, nameIndex);

	Debug::Log("[AE] Script RemoveAttachEffect: team=[%s] effectKey=%d -> removed=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", nameIndex, removed);
}



void ScriptExt::RemoveAllAttachEffects(TeamClass* pTeam)
{
	const int removed = AttachEffectService::RemoveAllFromTeam(pTeam);

	Debug::Log("[AE] Script RemoveAllAttachEffects: team=[%s] -> removed=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", removed);
}

