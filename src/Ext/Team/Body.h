#pragma once

#include <TeamClass.h>
#include <FootClass.h>

#include <Utilities/Container.h>
#include <Utilities/Template.h>

#include <algorithm>
#include <vector>

// 与 Phobos 一致: 脚本动作的扩展状态挂在 TeamClass 上, 而不是 ScriptClass
class TeamExt
{
public:
	using base_type = TeamClass;

	static constexpr DWORD Canary = 0x4D414554;
	// 启用指针失效通知; ScatterAttackGroups 缓存的是 FootClass*, 见下方过滤
	static constexpr bool ShouldConsiderInvalidatePointer = true;

	class ExtData final : public Extension<TeamClass>
	{
	public:
		ExtData(TeamClass* OwnerObject) : Extension<TeamClass>(OwnerObject)
			, LastProcessedMission(-1)
			, TimedMissionNode(-1)
			, TimedMissionLeft(0)
			, TimedMissionLastFrame(0)
		{ }

		virtual ~ExtData() = default;

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override;

		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

		std::vector<std::vector<FootClass*>> ScatterAttackGroups;
		int LastProcessedMission;

		// 计时动作所属脚本行, -1 表示无
		int TimedMissionNode;
		// 剩余帧数, 参与序列化; 读档后从这里继续
		int TimedMissionLeft;
		// 上次结算帧, 不参与序列化; 读档后置 -1, 首帧只对齐帧号
		int TimedMissionLastFrame;

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<TeamExt>
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
};
