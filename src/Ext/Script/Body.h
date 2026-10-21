#pragma once

#include <ScriptClass.h>
#include <TeamClass.h>
#include <FootClass.h>

#include <Utilities/Container.h>
#include <Utilities/Template.h>
#include <Helpers/Template.h>

#include <Interop/Services/ScenarioVariables.h>

#include <algorithm>
#include <vector>

// 自定义 AI 脚本动作编号
enum class ScaffoldScripts : unsigned int
{
	DistributedLoadIntoTransports = 5500,

	// 路径可视化
	RegisterFootPathVisualizer = 5501,
	UnregisterFootPathVisualizer = 5502,

	// 分散攻击
	ScatterAttack = 5503,

	// 分散攻击（目标改为 [AITargetTypes] 自定义列表）
	ScatterAttackByAITargetTypes = 5508,

	// 巡逻系
	PatrolToEnemyBuildingNearby = 5504,    // 巡逻到敌方指定建筑物附近
	PatrolToEnemyRally = 5505,             // 巡逻到敌方基地集结点
	PatrolToFriendlyBuildingNearby = 5506, // 巡逻到己方指定建筑物附近
	PatrolToFriendlyRally = 5507,          // 巡逻到己方基地集结点

	// AE相关
	ApplyAttachEffect = 5509,         // 给全队施加 AE
	RemoveAttachEffect = 5510,        // 按 AE 类型名移除
	RemoveAllAttachEffects = 5511,    // 清空全队所有 AE

	// 超时空传送
	ChronoTeleportToWaypoint = 5512,

	EnterMissionForDuration = 5513,

	JumpToLineByLocalVariable = 5514,
	JumpToLineByGlobalVariable = 5515,

	WaitForLocalVariable = 5516,
	WaitForGlobalVariable = 5517,

	JumpToLineIfLocalVariableSet = 5518,
	JumpToLineIfGlobalVariableSet = 5519,

	JumpToLocalLineIfLocalSet = 5520,
	JumpToGlobalLineIfGlobalSet = 5521,
	JumpToLocalLineIfGlobalSet = 5522,
	JumpToGlobalLineIfLocalSet = 5523,

};

class ScriptExt
{
public:
	static void ProcessAction(TeamClass* pTeam);
	static void LoadIntoTransportsDistributed(TeamClass* pTeam);

	// 路径可视化
	static void RegisterFootPathVisualizer(TeamClass* pTeam);
	static void UnregisterFootPathVisualizer(TeamClass* pTeam);

	// 分散攻击
	// attackAITargetType: >= 0 时表示 [AITargetTypes] 列表索引，只打列表内的类型；-1 表示用内置目标掩码
	static void Mission_ScatterAttack(TeamClass* pTeam, int attackAITargetType = -1);

	// 索敌（移植上游 Mission.Attack.cpp）
	static bool IsUnitAvailable(TechnoClass* pTechno, bool checkIfInTransportOrAbsorbed);
	static bool IsMindControlledByEnemy(HouseClass* pHouse, TechnoClass* pTechno);
	static bool EvaluateObjectWithMask(TechnoClass* pTechno, int mask, TechnoClass* pTeamLeader = nullptr, int attackAITargetType = -1);
	static TechnoClass* GreatestThreat(TechnoClass* pTechno, int method, int calcThreatMode = 0, HouseClass* onlyTargetThisHouseEnemy = nullptr, bool agentMode = false, const std::vector<TechnoClass*>* pExcludeTargets = nullptr, const std::vector<FootClass*>* pGroup = nullptr, const CoordStruct* pScoringOrigin = nullptr, int attackAITargetType = -1);

	// 巡逻系
	static void PatrolToBuildingNearby(TeamClass* pTeam, int typeIndex, int selectionMode, bool fresh, bool wantEnemy);
	static void PatrolToRally(TeamClass* pTeam, bool fresh, bool wantEnemy);

	// 进入状态并计时
	static void EnterMissionForDuration(TeamClass* pTeam, int argument, bool fresh, int nodeIndex);

	// 转到变量内容指定的行号
	static void JumpToLineByVariable(TeamClass* pTeam, int variableIndex, ScenarioVariables::Scope scope);

	// 等待变量变为非 0
	static void WaitForVariable(TeamClass* pTeam, int variableIndex, ScenarioVariables::Scope scope, bool fresh);

	// 变量已设置时转到指定行号
	static void JumpToLineIfVariableSet(TeamClass* pTeam, int argument, ScenarioVariables::Scope scope);

	// A 已设置时转到变量 B 内容所指的行号
	static void JumpToVariableLineIfSet(TeamClass* pTeam, int argument,
		ScenarioVariables::Scope conditionScope, ScenarioVariables::Scope lineScope);

	// AttachEffect: 
	static void ApplyAttachEffect(TeamClass* pTeam, int nameIndex, int durationOverride);
	static void RemoveAttachEffect(TeamClass* pTeam, int nameIndex);
	static void RemoveAllAttachEffects(TeamClass* pTeam);

};
