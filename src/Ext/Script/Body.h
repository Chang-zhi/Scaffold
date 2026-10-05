#pragma once

#include <ScriptClass.h>
#include <TeamClass.h>
#include <FootClass.h>

#include <Utilities/Container.h>
#include <Utilities/Template.h>
#include <Helpers/Template.h>

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

	// 超时空传送(复刻原版脚本动作 56/57 的传送逻辑)
	ChronoTeleportToWaypoint = 5512,  // 超时空传送至指定路径点
};

class ScriptExt
{
public:
	using base_type = ScriptClass;

	static constexpr DWORD Canary = 0x3B3B3B3B;
	// 启用指针失效通知；ScatterAttackGroups 缓存的是 FootClass*，见下方过滤
	static constexpr bool ShouldConsiderInvalidatePointer = true;

	class ExtData final : public Extension<ScriptClass>
	{
	public:
		ExtData(ScriptClass* OwnerObject) : Extension<ScriptClass>(OwnerObject)
			, LastProcessedMission(-1)
		{ }

		virtual ~ExtData() = default;

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override;

		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

		std::vector<std::vector<FootClass*>> ScatterAttackGroups;
		int LastProcessedMission;
	};

	class ExtContainer final : public Container<ScriptExt>
	{
	public:
		ExtContainer();
		~ExtContainer();

		virtual bool InvalidateExtDataIgnorable(void* const ptr) const override
		{
			switch (static_cast<AbstractClass*>(ptr)->WhatAmI())
			{
			case AbstractType::Unit:
			case AbstractType::Infantry:
			case AbstractType::Aircraft:
				return false;
			default:
				return true;
			}
		}
	};

	static ExtContainer ExtMap;

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

	// AttachEffect: 
	static void ApplyAttachEffect(TeamClass* pTeam, int nameIndex, int durationOverride);
	static void RemoveAttachEffect(TeamClass* pTeam, int nameIndex);
	static void RemoveAllAttachEffects(TeamClass* pTeam);

	// 超时空传送: 目标坐标用参数指定的路径点, 其余流程与原版 56/57 一致
	static void ChronoTeleportToWaypoint(TeamClass* pTeam, int waypointIndex);
};
