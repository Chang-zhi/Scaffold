#pragma once

#include <TeamTypeClass.h>
#include <ScriptTypeClass.h>
#include <Utilities/Container.h>
#include <Utilities/Template.h>
#include <Utilities/TemplateDef.h>
#include <Helpers/Template.h>

class AnimTypeClass;

class TeamTypeExt
{
public:
	using base_type = TeamTypeClass;

	static constexpr DWORD Canary = 0xDCBA4321;

	class ExtData final : public Extension<TeamTypeClass>
	{
	public:
		int OriginalScriptTypeIndex;
		int OriginalTaskForceIndex;

		// ---- 小地图标记 ----
		// 该类型的所有小队实例在小地图上显示这个动画。解散后由 RadarMarkTeamAuto 自动清掉
		Nullable<AnimTypeClass*> RadarAnim;          // Animations 名，与 [TechnoType] RadarAnim 同一套写法
		Valueable<bool> RadarAnim_ShowInShroud;      // 迷雾里也画
		Valueable<RadarTeamMarkMode> RadarAnim_Mode; // Center=整队一个画在成员中心 / EachMember=每个成员一个

		ExtData(TeamTypeClass* const OwnerObject)
			: Extension<TeamTypeClass>(OwnerObject)
			, OriginalScriptTypeIndex { -1 }
			, OriginalTaskForceIndex { -1 }
			, RadarAnim { }
			, RadarAnim_ShowInShroud { false }
			, RadarAnim_Mode { RadarTeamMarkMode::Center }
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

	class ExtContainer final : public Container<TeamTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};
