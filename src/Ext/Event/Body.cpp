#include "Body.h"

#include <New/ChoiceBox/MapChoiceBoxClass.h>

#include <HouseClass.h>
#include <TechnoClass.h>
#include <Unsorted.h>

#include <Ext/Techno/Body.h>

#include <Helpers/Macro.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <algorithm>

bool EventExt::AddEvent()
{
	return EventClass::OutList.Add(*reinterpret_cast<EventClass*>(this));
}

void EventExt::RespondEvent()
{
	switch (this->Type)
	{
	case EventTypeExt::ChoiceBoxClick:
		this->RespondChoiceBoxClick();
		break;
	case EventTypeExt::RecruitPassengers:
		this->RespondRecruitPassengers();
		break;
	case EventTypeExt::DeployWarpToggle:
		this->RespondDeployWarpToggle();
		break;
	default:
		break;
	}
}

// 点击是本机私有输入，需投递成事件；绘制钩子一个逻辑帧内可能被调多次，故按（帧, 框）去重。
void EventExt::RaiseChoiceBoxClick(int boxID, int buttonIndex)
{
	static int s_lastQueuedFrame = -1;
	static int s_lastQueuedBoxID = -1;

	HouseClass* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	const int frame = Unsorted::CurrentFrame;
	if (s_lastQueuedFrame == frame && s_lastQueuedBoxID == boxID)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::ChoiceBoxClick;
	eventExt.HouseIndex = static_cast<char>(pPlayer->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(frame);
	eventExt.ChoiceBoxClick.BoxID = boxID;
	eventExt.ChoiceBoxClick.ButtonIndex = buttonIndex;

	if (!eventExt.AddEvent())
	{
		// 队列满：不记去重状态，本逻辑帧内后续的渲染帧还能再试一次
		Debug::Log("CHOICEBOXCLICK dropped, event queue full (box=%d, button=%d)\n", boxID, buttonIndex);
		return;
	}

	s_lastQueuedFrame = frame;
	s_lastQueuedBoxID = boxID;

	Debug::Log("Adding event CHOICEBOXCLICK (box=%d, button=%d)\n", boxID, buttonIndex);
}

void EventExt::RespondChoiceBoxClick()
{
	MapChoiceBoxClass::ApplyClickEvent(this->ChoiceBoxClick.BoxID, this->ChoiceBoxClick.ButtonIndex);
}

void EventExt::RaiseRecruitPassengers(HouseClass* pPlayer, const std::vector<ObjectClass*>& selected)
{
	if (!pPlayer)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::RecruitPassengers;
	eventExt.HouseIndex = static_cast<char>(pPlayer->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(Unsorted::CurrentFrame);

	const size_t count = std::min(selected.size(), MAX_SELECTION);
	eventExt.RecruitPassengers.Count = static_cast<uint8_t>(count);

	for (size_t i = 0; i < count; ++i)
		eventExt.RecruitPassengers.Selection[i] = TargetClass(selected[i]);

	if (!eventExt.AddEvent())
		Debug::Log("RecruitPassengers: event queue full, dropped\n");

	if (selected.size() > MAX_SELECTION)
		Debug::Log("RecruitPassengers: selection truncated to %zu of %zu\n", MAX_SELECTION, selected.size());
}

// 只读契约：招募决策已在按键方做过一次，结果由 Scatter/Deploy/Enter 事件下发到各机；
// 这里禁止改状态（尤其禁止各机重跑一遍招募），否则会重复下单并分叉。
void EventExt::RespondRecruitPassengers()
{
	if (this->HouseIndex < 0 || this->HouseIndex >= HouseClass::Array.Count)
		return;

	const int count = this->RecruitPassengers.Count;
	int resolved = 0;
	for (int i = 0; i < count; ++i)
	{
		if (this->RecruitPassengers.Selection[i].As_Object())
			resolved++;
	}

	// 正常时各机一致（对象在各机都还在）；数量不符只作诊断，不影响状态
	if (resolved != count)
		Debug::Log("RecruitPassengers: %d/%d targets resolved (house=%d)\n",
			resolved, count, static_cast<int>(this->HouseIndex));
}

void EventExt::RaiseDeployWarpToggle(TechnoClass* pProjector)
{
	if (!pProjector)
		return;

	const int frame = Unsorted::CurrentFrame;

	static int s_lastQueuedFrame = -1;
	static TechnoClass* s_lastQueuedProjector = nullptr;

	if (s_lastQueuedFrame == frame && s_lastQueuedProjector == pProjector)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::DeployWarpToggle;
	eventExt.HouseIndex = static_cast<char>(pProjector->Owner ? pProjector->Owner->ArrayIndex : -1);
	eventExt.Frame = static_cast<uint32_t>(frame);
	eventExt.DeployWarp.Projector = TargetClass(pProjector);

	if (!eventExt.AddEvent())
	{
		Debug::Log("DEPLOYWARPTOGGLE dropped, event queue full\n");
		return;
	}

	s_lastQueuedFrame = frame;
	s_lastQueuedProjector = pProjector;

	Debug::Log("Adding event DEPLOYWARPTOGGLE (%s)\n",
		pProjector->GetTechnoType() ? pProjector->GetTechnoType()->ID : "?");
}

void EventExt::RespondDeployWarpToggle()
{
	auto pProjector = this->DeployWarp.Projector.As_Techno();

	if (!pProjector || pProjector->Health <= 0 || pProjector->InLimbo)
		return;

	if (!TechnoExt::DeployWarp::IsProjectorType(pProjector))
		return;

	TechnoExt::DeployWarp::Toggle(pProjector);
}

size_t EventExt::GetDataSize(EventTypeExt type)
{
	switch (type)
	{
	case EventTypeExt::ChoiceBoxClick:
		return sizeof(EventExt::ChoiceBoxClick);
	case EventTypeExt::RecruitPassengers:
		return sizeof(EventExt::RecruitPassengers);
	case EventTypeExt::DeployWarpToggle:
		return sizeof(EventExt::DeployWarp);
	default:
		break;
	}

	return 0;
}

bool EventExt::IsValidType(EventTypeExt type)
{
	return type >= EventTypeExt::FIRST && type <= EventTypeExt::LAST;
}

// hooks
DEFINE_HOOK(0x64B6FE, sub_64B660_GetEventSize, 0x6)
{
	const auto eventType = static_cast<EventTypeExt>(R->EDI() & 0xFF);

	if (EventExt::IsValidType(eventType))
	{
		const size_t eventSize = EventExt::GetDataSize(eventType);

		R->EDX(eventSize);
		R->EBP(eventSize);
		return 0x64B71D;
	}

	return 0;
}

DEFINE_HOOK(0x64BE7D, sub_64BDD0_GetEventSize1, 0x6)
{
	const auto eventType = static_cast<EventTypeExt>(R->EDI() & 0xFF);

	if (EventExt::IsValidType(eventType))
	{
		const size_t eventSize = EventExt::GetDataSize(eventType);

		REF_STACK(size_t, eventSizeInStack, STACK_OFFSET(0xAC, -0x8C));
		eventSizeInStack = eventSize;
		R->ECX(eventSize);
		R->EBP(eventSize);
		return 0x64BE97;
	}

	return 0;
}

DEFINE_HOOK(0x64C30E, sub_64BDD0_GetEventSize2, 0x6)
{
	const auto eventType = static_cast<EventTypeExt>(R->ESI() & 0xFF);

	if (EventExt::IsValidType(eventType))
	{
		const size_t eventSize = EventExt::GetDataSize(eventType);

		R->ECX(eventSize);
		R->EBP(eventSize);
		return 0x64C321;
	}

	return 0;
}

DEFINE_HOOK(0x4C6CC8, EventClass_Execute_Ext, 0x5)
{
	GET(EventExt*, pEvent, ESI);

	if (pEvent && EventExt::IsValidType(pEvent->Type))
		pEvent->RespondEvent();

	return 0;
}

// EventNames 只有 47 项（LAST_EVENT=47），自定义 Type 会越界读表。
// 零售 exe 里这两处日志是 call nullsub_1（空函数），越界读本身不崩；拦掉是为了换带日志的 exe 时不炸。
static const char EventNameExtUnknown[] = "EXT";

DEFINE_HOOK(0x64C5C7, sub_64C380_EventNameSafe, 0x7)
{
	GET(EventExt*, pEvent, ESI);

	if (static_cast<unsigned int>(pEvent->Type) > 0x2E)
	{
		R->ECX(reinterpret_cast<DWORD>(EventNameExtUnknown));
		return 0x64C5CE;
	}

	return 0;
}

DEFINE_HOOK(0x65208B, sub_652070_EventNameSafe, 0x7)
{
	GET(EventExt*, pEvent, ESI);

	if (static_cast<unsigned int>(pEvent->Type) > 0x2E)
	{
		R->ECX(reinterpret_cast<DWORD>(EventNameExtUnknown));
		return 0x652092;
	}

	return 0;
}
