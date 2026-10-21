#pragma once
#include <InfantryClass.h>
#include <AnimClass.h>
#include <TechnoClass.h>

#include <Helpers/Macro.h>
#include <Utilities/Anchor.h>
#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>
#include <Utilities/Macro.h>

#include <Ext/TechnoType/Body.h>
#include <New/Effects/IEffect.h>

#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <memory>

class FootClass;
class TeamClass;
class TemporalClass;

class TechnoExt
{
public:
	using base_type = TechnoClass;

	static constexpr DWORD Canary = 0x1D2C3F4E;
	// static constexpr size_t ExtPointerOffset = 0x34C;
	// 启用指针失效通知；实际处理哪些类型由下方 ExtContainer::InvalidateExtDataIgnorable 过滤
	static constexpr bool ShouldConsiderInvalidatePointer = true;

	// Temporal AOE state（定义在 ExtData 外，方便其他文件直接引用）
	struct TemporalAOEState
	{
		// 从 ini 里面读取的自定义配置项
		bool Active = false;                    // AOE 功能是否激活
		double CellSpread = 3.0;                // AOE 半径（格）
		double SecondaryWeight = 1.0;           // 副目标冻结时间权重

		int WeaponDamage = 100;                 // 武器伤害值
		int ExtraWarpAdded = 0;                 // 已加到主 Temporal 上的额外时间
		int WarpTimer = 0;                      // 内部计时器（替代引擎的 WarpRemaining）
		TechnoClass* CachedMain = nullptr;      // 失效时置空，由 CachedMainDead 保留销毁状态
		bool CachedMainDead = false;            // 缓存的主目标已被游戏抹除
		bool WarpingOut = false;                // 正在抹除副目标中，防止递归
		int ScanInterval = 5;                   // 扫描间隔（帧）
		int ScanCounter = 0;                    // 扫描计数器

		std::vector<TechnoClass*> TargetsInRange;      // 范围内的副目标列表
		std::unordered_set<TechnoClass*> BuildingsDisabled;      // 已被 DisableTemporal 的建筑
		std::unordered_set<TechnoClass*> ContributedTargets;    // 已贡献过时间的副目标（仅累加，离开不扣减）
	};

	struct DeployWarpState
	{
		bool Active = false;                          
		int  ScanCounter = 0;                         
		std::vector<TechnoClass*> TargetsInField;     
	};

	
	struct LocomotorRestoreState
	{
		bool Active = false;
		AbstractClass* Target = nullptr;
		AbstractClass* ArchiveTarget = nullptr;
		AbstractClass* Destination = nullptr;
		AbstractClass* LastDestination = nullptr;
		AbstractClass* MegaDestination = nullptr;
		AbstractClass* MegaTarget = nullptr;
		Mission MegaMission = Mission::None;
		Mission OriginalMission = Mission::None;
		bool HaveAttackMoveTarget = false;

		void Capture(FootClass* pTarget);
		void Restore(FootClass* pTarget);
		void Clear();
	};

	class ExtData final : public Extension<TechnoClass>
	{
	public:
		TechnoTypeExt::ExtData* TypeExtData;

		// Temporal AOE state
		TemporalAOEState AOEState;

		// 部署超时空力场状态
		DeployWarpState WarpField;

		// 磁电吸取前的任务，释放时恢复
		LocomotorRestoreState LocomotorRestore;

		// 效果系统 - 附加到此单位上的所有效果
		std::vector<std::unique_ptr<IEffect>> Effects;

		ExtData(TechnoClass* OwnerObject) : Extension<TechnoClass>(OwnerObject)
			, TypeExtData { nullptr }
		{ }

		virtual ~ExtData() override;
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override;
		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

		void UpdateTemporalAOE();
		void UpdateDeployWarpField();
		void UpdateEffects();       // 每帧更新所有附加效果并清理已死亡效果
		void CompleteLoad();

	private:
		// Swizzled pointers must stay in vectors until all addresses are fixed.
		std::vector<TechnoClass*> LoadedBuildingsDisabled;
		std::vector<TechnoClass*> LoadedContributedTargets;
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<TechnoExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
		void InvalidateTechno(void* ptr)
		{
			this->InvalidateExtDataPointer(ptr, true);
		}

		virtual bool InvalidateExtDataIgnorable(void* const ptr) const override
		{
			// AOEState 缓存的是 TechnoClass*（副目标 / 被禁用建筑），只对这些类型清理。
			// 注意 AirstrikeClass 派生自 AbstractClass 而非 TechnoClass，不能作为过滤条件。
			switch (static_cast<AbstractClass*>(ptr)->WhatAmI())
			{
			case AbstractType::Building:
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

	static void Clear();
	static void PointerGotInvalid(void* ptr, bool removed);
	static void UpdateGlobalState();
	static void ResetFrameState();
	static void SaveFrameState(ScaffoldStreamWriter& Stm);
	static void LoadFrameState(ScaffoldStreamReader& Stm);
	static bool CompleteLoad();
	static bool LoadGlobals(ScaffoldStreamReader& Stm);
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);

	// Features/WeaponHelpers.cpp
	static WeaponTypeClass* GetCurrentWeapon(TechnoClass* pThis, int& weaponIndex, bool getSecondary = false);
	static WeaponTypeClass* GetCurrentWeapon(TechnoClass* pThis, bool getSecondary = false);

	// 目标是否处于本单位可抵达的移动区域（对齐上游 AllowedTargetByZone）
	static bool AllowedTargetByZone(TechnoClass* pThis, TechnoClass* pTarget, TargetZoneScanType zoneScanType, WeaponTypeClass* pWeapon = nullptr, bool useZone = false, int zone = -1);

	// ── 自动Hunt（Features/AutoHunt.cpp） ────────────────────────
	static void ProcessAutoHunt(FootClass* pFoot);

	// ── 载员自动入队（Features/PassengerJoinTeam.cpp） ───────────
	static void ProcessPassengerJoinTeam(FootClass* pFoot);

	// ── 混乱恢复（Features/BerzerkRestore.cpp） ──────────────────
	static void BerzerkRestoreCheck(TechnoClass* pThis);
	static void BerzerkRestorePointerInvalidate(void* ptr);
	static void BerzerkRestoreClearCache();

	// ── 磁电释放后恢复任务（Features/LocomotorRestore.cpp） ──────
	static void LocomotorRestoreOnImbue(FootClass* pTarget);
	static void LocomotorRestoreCheck(TechnoClass* pThis);

	// ── 合法目标 AI（Features/LegalTargetAI.cpp） ────────────────
	static void HandleLegalTargetAITargeting(TechnoClass* pThis);

	// ── 超时空 AOE（Features/TemporalAOE.cpp） ──────────────────
	struct TemporalAOE
	{
		// 副目标 → 假 Temporal 条目
		struct FakeTemporalEntry
		{
			TemporalClass* FakeTemporal;
			TechnoClass*   Attacker;
		};

		static std::unordered_map<TechnoClass*, FakeTemporalEntry> FakeTemporals;
		static std::unordered_map<TechnoClass*, TechnoClass*> SecondaryClaims;
		static std::unordered_set<TechnoClass*> WarpingOutTargets;
		static std::unordered_map<TechnoClass*, TechnoClass*> CachedMainOwners;
		static std::unordered_map<TechnoClass*, std::unordered_set<TechnoClass*>> SecondariesByAttacker;
		static bool s_PostLoadCleanupNeeded;

		static std::unordered_set<TechnoClass*> DisabledByUs;

		static void CreateFakeTemporal(TechnoClass* pAttacker, TechnoClass* pTarget);
		static void DestroyFakeTemporal(TechnoClass* pTarget);
		static void DestroyFakeTemporalsByAttacker(TechnoClass* pAttacker);
		static void DestroyFakeTemporalsByTargetList(const std::vector<TechnoClass*>& targets);
		static void DestroyAllFakeTemporals();
		static void InitAOEState(TechnoClass* pAttacker);
		static bool HasAOEWeapon(TechnoClass* pAttacker);

		// 铁幕保护中 → 超时空 AOE / 力场都不能选它作冻结目标
		static bool IsIronCurtainProtected(TechnoClass* pTech);
		static void ReleaseAttackerLocks(TechnoClass* pAttacker);
		static void InvalidatePtr(void* ptr);
		static void ValidateGlobals();

		static void ForceTechnoRedraw(TechnoClass* pTechno);
		static void MarkBuildingDisabled(TechnoClass* pTech);   
		static void ReenableBuilding(TechnoClass* pTech);       
		static void ClearBuildingsDisabled(std::unordered_set<TechnoClass*>& set);
		static void PostLoadCleanup();
		static void Clear();
		static void ReleaseAOESecondaries(TechnoClass* pAttacker, TemporalAOEState& state);
		static void DeactivateAOE(TechnoClass* pAttacker, TemporalAOEState& state);
		static void PlayWarpAwayAnim(TechnoClass* pTarget);
		static void WarpOutTarget(TechnoClass* pTarget, TechnoClass* pKiller, TemporalAOEState& state);
	};

	// ── 超时空武器互斥（Features/TemporalExclusive.cpp） ─────────
	struct TemporalExclusive
	{
		static std::unordered_map<TechnoClass*, TechnoClass*> TargetsMap;

		static bool IsCurrentUseExclusiveTemporalWeapon(TechnoClass* pTechno);
		static void InvalidatePtr(void* ptr);
		static void CleanupInvalidTemporalLocks();
		static void HandleTemporalExclusiveTargeting(TechnoClass* pThis);
		static void UpdateTemporalExclusive();
	};

	// ── 部署超时空力场（Features/DeployWarpField.cpp） ───────────
	struct DeployWarp
	{
		static constexpr int ScanInterval = 5;   // 范围扫描间隔（帧），内置不可配

		static std::unordered_map<TechnoClass*, std::unordered_set<TechnoClass*>> ByProjector;

		static bool IsProjectorType(TechnoClass* pTech);
		static void Toggle(TechnoClass* pThis);          // 部署键切换
		static void ReleaseAll(TechnoClass* pProjector); // 立即收起力场
		static void InvalidatePtr(void* ptr);            // 指针失效清理

		// 点击（vtable+0x144）与"动作/光标"（vtable+0x74）的公共入口
		static bool OnActionClicked(TechnoClass* pThis, int action);
		static void DebugGetAction(TechnoClass* pThis, void* pTarget);

		// 启动时替换车辆/步兵/飞行器的 +0x74 与 +0x144 槽（见 DeployWarpField.cpp）
		static void InstallActionWrappers();
	};
};
