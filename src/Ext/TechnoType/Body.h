#pragma once
#include <TechnoTypeClass.h>

#include <Helpers/Macro.h>
#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>
#include <Utilities/Enum.h>

class Matrix3D;
class ParticleSystemTypeClass;
class TechnoTypeExt
{
public:
	using base_type = TechnoTypeClass;

	static constexpr DWORD Canary = 0xAAAAEEEE;

	class ExtData final : public Extension<TechnoTypeClass>
	{
	public:
		Valueable<bool> AutoHunt;
		Valueable<bool> LegalTargetWhenAIOwner;

		Valueable<TargetZoneScanType> TargetZoneScanType;
		Valueable<int> RadarJamRadius;
		Nullable<int> InhibitorRange;

		Valueable<SmartVHPScanType> SmartVHPScan;
		Valueable<int> SmartVHPScan_Count;
		Valueable<int> SmartVHPScan_Damage;
		Valueable<double> SmartVHPScan_Bias;
		Valueable<double> SmartVHPScan_UnknownFactor;
		Valueable<double> SmartVHPScan_ExcludeFraction;

		// ---- 中央调度 ----
		Valueable<double> SmartVHPScan_Overflow;
		Valueable<double> SmartVHPScan_SwitchThreshold;
		Valueable<bool> SmartVHPScan_IncludeInflight;
		Valueable<int> SmartVHPScan_CountIdleFrames;   // 打满人数上限后先空手等这么多帧，超时才允许超编

		// ---- 部署超时空力场 ----
		// 部署键切换力场：范围内对象进入超时空抹除状态（原版机制自带免伤），再次部署恢复
		Valueable<bool>   DeployWarp_Enable;          // 总开关；带此键的类型自身永不被冻结
		Valueable<double> DeployWarp_Range;           // 力场半径（格）
		Valueable<bool>   DeployWarp_AffectsEnemies;  // 是否也纳入敌军；默认否
		Valueable<bool>   DeployWarp_AffectsAllies;   // 是否保护盟友；默认是（投影者自己恒纳入）
		Valueable<bool>   DeployWarp_AffectsAirborne; // 是否影响空中的单位（飞行器、悬停中的步兵/车辆）；默认否
		ExtData(TechnoTypeClass* OwnerObject) : Extension<TechnoTypeClass>(OwnerObject)
			, AutoHunt { false }
			, LegalTargetWhenAIOwner { true }
			, TargetZoneScanType { TargetZoneScanType::Same }
			, RadarJamRadius { 0 }
			, InhibitorRange { }
			, SmartVHPScan { SmartVHPScanType::None }
			, SmartVHPScan_Count { 1 }
			, SmartVHPScan_Damage { 0 }
			, SmartVHPScan_Bias { 2.0 }
			, SmartVHPScan_UnknownFactor { 1.0 }
			, SmartVHPScan_ExcludeFraction { 0.0 }
			, SmartVHPScan_Overflow { 0.25 }
			, SmartVHPScan_SwitchThreshold { 1.25 }
			, SmartVHPScan_IncludeInflight { true }
			, SmartVHPScan_CountIdleFrames { 0 }
			, DeployWarp_Enable { false }
			, DeployWarp_Range { 3.0 }
			, DeployWarp_AffectsEnemies { false }
			, DeployWarp_AffectsAllies { true }
			, DeployWarp_AffectsAirborne { false }
		{ }

		virtual ~ExtData() = default;
		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

		void LoadFromINIByWhatAmI(INI_EX& exINI, const char* pSection, INI_EX& exArtINI, const char* pArtSection);


	private:
		template <typename T>
		void Serialize(T& Stm);

	};

	class ExtContainer final : public Container<TechnoTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
	static bool SelectWeaponMutex;

	static void ApplyTurretOffset(TechnoTypeClass* pType, Matrix3D* mtx, double factor = 1.0);
	static TechnoTypeClass* GetTechnoType(ObjectTypeClass* pType);

	static WeaponTypeClass* GetWeaponType(TechnoTypeClass* pThis, int weaponIndex, bool isElite);

};
