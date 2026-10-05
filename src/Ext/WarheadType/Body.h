#pragma once
#include <WarheadTypeClass.h>
#include <SuperWeaponTypeClass.h>
#include <Helpers/Macro.h>
#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>
#include <Ext/Techno/Body.h>

class WarheadTypeExt
{
public:
	using base_type = WarheadTypeClass;

	static constexpr DWORD Canary = 0xAAAADDDD;
	// static constexpr size_t ExtPointerOffset = 0x20;

	class ExtData final : public Extension<WarheadTypeClass>
	{

	public:
		Valueable<bool> Temporal_Exclusive;

		// Temporal AOE
		Valueable<bool> TemporalAOE_Enable;
		Valueable<double> TemporalAOE_CellSpread;
		Valueable<double> TemporalAOE_SecondaryWeight;
		Valueable<bool> TemporalAOE_AffectsAllies;
		Valueable<bool> TemporalAOE_AffectsAirborne;
		Valueable<bool> TemporalAOE_CenterOnAttacker;

		// BerserkReduce - 减少目标的混乱时间
		// 正值减少帧数，负值增加帧数
		// 如果减到0以下，立即清除混乱
		Valueable<int> BerserkReduce;

		ExtData(WarheadTypeClass* OwnerObject) : Extension<WarheadTypeClass>(OwnerObject)
			, Temporal_Exclusive { false }
			, TemporalAOE_Enable { false }
			, TemporalAOE_CellSpread { 2.0 }
			, TemporalAOE_SecondaryWeight { 1.0 }
			, TemporalAOE_AffectsAllies { false }
			, TemporalAOE_AffectsAirborne { false }
			, TemporalAOE_CenterOnAttacker { false }
			, BerserkReduce { 0 }
		{ }

		virtual ~ExtData() = default;
		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }
		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<WarheadTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
	static bool LoadGlobals(ScaffoldStreamReader& Stm);
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
};
