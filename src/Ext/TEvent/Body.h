#pragma once

#include <Utilities/Container.h>
#include <Utilities/Template.h>
#include <Helpers/Template.h>

#include <optional>

#include <TEventClass.h>

class HouseClass;
class TriggerClass;

enum ScaffoldTriggerEvent
{
	// 路径点附近存在所属方的任意科技类型...
	TechnoTypeOfHouseNearWaypoint = 550,

	// 路径点附近不存在所属方的任意科技类型...
	TechnoTypeOfHouseAllLeavesWaypoint = 551,

	// 路径点上存在所属方的指定科技类型...
	TechnoTypeOfHouseExistsAtWaypoint = 552,

	// 路径点上不存在所属方的指定科技类型...
	TechnoTypeOfHouseNotExistsAtWaypoint = 553,

	// 流逝时间(帧)...
	ElapsedTimeFrames = 554,

	// 任务计时器大于 N 秒
	MissionTimerGreater = 555,

	// 任务计时器小于 N 秒
	MissionTimerLess = 556,

	// 指定编号的选择框中指定按钮被点击...
	ChoiceBoxButtonClicked = 557,

	// 指定编号的选择框中任意按钮被点击...
	ChoiceBoxAnyButtonClicked = 558,

	// 指定编号的选择框超时未选...
	ChoiceBoxTimedOut = 559,

	// 电力输出大于...
	HousePowerOutputMuch = 560,

	// 电力输出小于...
	HousePowerOutputLess = 561,

	// 电力负载大于...
	HousePowerDrainMuch = 562,

	// 电力负载小于...
	HousePowerDrainLess = 563,

	// 电力盈余大于...
	HousePowerSurplusMuch = 564,

	// 电力盈余小于...
	HousePowerSurplusLess = 565,

	_DummyMaximum,
};

enum PowerEventMode
{
	Output = 0,
	Drain = 1,
	Surplus = 2
};

class TEventExt
{
public:
	using base_type = TEventClass;

	static constexpr DWORD Canary = 0xAAAAFFFF;

	class ExtData final : public Extension<TEventClass>
	{
	public:
		ExtData(TEventClass* const OwnerObject) : Extension<TEventClass>(OwnerObject)
		{ }

		virtual ~ExtData() = default;

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;


	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	static int GetFlags(int iEvent);

	static std::optional<bool> Execute(TEventClass* pThis, int iEvent, HouseClass* pHouse,
		ObjectClass* pObject, CDTimerClass* pTimer, bool* isPersitant, TechnoClass* pSource,
		TriggerClass* pTrigger = nullptr);

	static bool TechnoTypeOfHouseNearWaypoint(TEventClass* pThis, HouseClass* pHouse);
	static bool TechnoTypeOfHouseExistsAtWaypoint(TEventClass* pThis, HouseClass* pHouse);
	// static bool TechnoDestroyedByHouse(TEventClass* pThis, ObjectClass* pAttached);
	static bool ElapsedTimeFramesFunc(TEventClass* pThis);
	static bool MissionTimerGreaterFunc(TEventClass* pThis);
	static bool MissionTimerLessFunc(TEventClass* pThis);

	static bool ChoiceBoxButtonClickedFunc(TEventClass* pThis, HouseClass* pHouse);
	static bool ChoiceBoxAnyButtonClickedFunc(TEventClass* pThis, HouseClass* pHouse);
	static bool ChoiceBoxTimedOutFunc(TEventClass* pThis, HouseClass* pHouse);

	static bool PowerHander(TEventClass* pThis, HouseClass* pHouse, PowerEventMode mode, bool isMuch);

	class ExtContainer final : public Container<TEventExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
	static void Clear();
	static void PointerGotInvalid(void* ptr, bool removed);
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
	static bool LoadGlobals(ScaffoldStreamReader& Stm);
	static bool CompleteLoad();
};
