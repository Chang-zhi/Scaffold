#pragma once

#include <EventClass.h>
#include <TargetClass.h>
#include <New/FullMap/FullMap.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class HouseClass;
class ObjectClass;
class TechnoClass;

// 自定义事件类型：原版分派只认 1..0x2E（其余落 default），故从 0x96 起取值。
// 长度表 byte_8208EC 与 EventNames 表的越界点都已在 Body.cpp 里 hook。
enum class EventTypeExt : uint8_t
{
	ChoiceBoxClick = 0x96,
	RecruitPassengers = 0x97,
	DeployWarpToggle = 0x98,
	FullMapMark = 0x99,

	FIRST = ChoiceBoxClick,
	LAST = FullMapMark
};

#pragma pack(push, 1)
// 复用的是 EventClass 那块 111 字节定长内存，布局必须逐字节一致（payload 在偏移 7）。
// 字段全部手填，不走 EventClass 的构造：那些构造按 Type 索引 EventNames（仅 47 项），自定义 Type 会越界。
class EventExt
{
public:
	// 1 + 20 * sizeof(TargetClass)(5) = 101，已贴近 payload 上限 104，不能再加
	static constexpr size_t MAX_SELECTION = 20;

	EventTypeExt Type;
	bool IsExecuted;
	char HouseIndex;
	uint32_t Frame;

	union
	{
		char DataBuffer[104];

		struct CHOICEBOXCLICK
		{
			int BoxID;
			int ButtonIndex;
		} ChoiceBoxClick;

		struct RECRUITPASSENGERS
		{
			uint8_t Count;
			TargetClass Selection[MAX_SELECTION];
		} RecruitPassengers;

		struct DEPLOYWARP
		{
			TargetClass Projector;
		} DeployWarp;

		// 战术地图标注同步。104 字节恰好占满 payload：
		// 手绘折线（Points）与文字（Text）不同时使用，共用一块内联区。
		struct FULLMAPMARK
		{
			uint8_t Op;          // FullMapMarkOp
			uint8_t Kind;        // FullMapMarkKind
			uint8_t ColorIdx;
			uint8_t PointCount;  // Points 里有效点数
			uint16_t Id;
			uint8_t TextLen;     // Text 里有效 wchar 数
			int8_t Author;       // 冗余存 HouseIndex，双保险
			CellStruct Cell;
			CellStruct Cell2;
			union
			{
				int16_t Points[FullMap::MaxPointsPerEvent][2];
				wchar_t Text[FullMap::MaxPointsPerEvent];
			} Data;
		} FullMapMark;
	};

	bool AddEvent();

	void RespondEvent();

	static void RaiseChoiceBoxClick(int boxID, int buttonIndex);
	void RespondChoiceBoxClick();

	static void RaiseRecruitPassengers(HouseClass* pPlayer, const std::vector<ObjectClass*>& selected);
	void RespondRecruitPassengers();

	static void RaiseDeployWarpToggle(TechnoClass* pProjector);
	void RespondDeployWarpToggle();

	static void RaiseFullMapMarkAdd(const FullMap::Mark& mark);
	static void RaiseFullMapMarkAppend(uint16_t id, const int16_t* points, uint8_t count);
	static void RaiseFullMapMarkRemove(uint16_t id);
	static void RaiseFullMapMarkClearOwn();
	void RespondFullMapMark();

	static size_t GetDataSize(EventTypeExt type);
	static bool IsValidType(EventTypeExt type);
	static bool CanControl(HouseClass* pSender, TechnoClass* pTechno);
	static void Clear();
};
#pragma pack(pop)

static_assert(sizeof(EventExt) == 111);
static_assert(offsetof(EventExt, DataBuffer) == 7);
static_assert(sizeof(EventExt::RecruitPassengers) <= sizeof(EventExt::DataBuffer));
static_assert(sizeof(EventExt::DeployWarp) <= sizeof(EventExt::DataBuffer));
static_assert(sizeof(EventExt::FullMapMark) == sizeof(EventExt::DataBuffer));
