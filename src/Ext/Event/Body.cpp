#include "Body.h"
#include <New/Selection/Selection.h>

#include <New/ChoiceBox/MapChoiceBoxClass.h>

#include <HouseClass.h>
#include <TechnoClass.h>
#include <Unsorted.h>
#include <SessionClass.h>

#include <Ext/Techno/Body.h>

#include <Helpers/Macro.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <algorithm>
#include <unordered_set>
#include <Utilities/SyncState.h>

namespace
{
	SyncState::FrameGate ChoiceInputFrame;
	SyncState::FrameGate WarpInputFrame;
	std::unordered_set<int> QueuedBoxes;
	std::unordered_set<TechnoClass*> QueuedProjectors;
}

void EventExt::Clear()
{
	ChoiceInputFrame.Reset();
	WarpInputFrame.Reset();
	QueuedBoxes.clear();
	QueuedProjectors.clear();
}

bool EventExt::AddEvent()
{
	SelectionService::BeforeEvent();
	return EventClass::OutList.Add(*reinterpret_cast<EventClass*>(this));
}

void EventExt::RespondEvent()
{
	if (this->HouseIndex < 0 || this->HouseIndex >= HouseClass::Array.Count)
		return;

	switch (this->Type)
	{
	case EventTypeExt::SelectionBatch:
		SelectionService::ReceiveBatch(*reinterpret_cast<EventClass*>(this));
		break;
	case EventTypeExt::ChoiceBoxClick:
		this->RespondChoiceBoxClick();
		break;
	case EventTypeExt::RecruitPassengers:
		this->RespondRecruitPassengers();
		break;
	case EventTypeExt::DeployWarpToggle:
		this->RespondDeployWarpToggle();
		break;
	case EventTypeExt::FullMapMark:
		this->RespondFullMapMark();
		break;
	default:
		break;
	}
}

// 点击是本机私有输入，需投递成事件；绘制钩子一个逻辑帧内可能被调多次，故按（帧, 框）去重。
void EventExt::RaiseChoiceBoxClick(int boxID, int buttonIndex)
{
	HouseClass* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	const int frame = Unsorted::CurrentFrame;
	if (ChoiceInputFrame.TryEnter(frame))
		QueuedBoxes.clear();
	if (QueuedBoxes.contains(boxID))
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

	QueuedBoxes.insert(boxID);

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
	if (count > MAX_SELECTION)
	{
		Debug::Log("RecruitPassengers: invalid selection count %d\n", count);
		return;
	}
	int resolved = 0;
	for (int i = 0; i < count; ++i)
	{
		const auto rtti = this->RecruitPassengers.Selection[i].m_RTTI;
		if (rtti != 0 && rtti != static_cast<unsigned char>(AbstractType::Abstract))
			return;
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
	auto const pSender = HouseClass::CurrentPlayer;
	if (!pProjector || !CanControl(pSender, pProjector))
		return;

	const int frame = Unsorted::CurrentFrame;

	if (WarpInputFrame.TryEnter(frame))
		QueuedProjectors.clear();
	if (QueuedProjectors.contains(pProjector))
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::DeployWarpToggle;
	eventExt.HouseIndex = static_cast<char>(pSender->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(frame);
	eventExt.DeployWarp.Projector = TargetClass(pProjector);

	if (!eventExt.AddEvent())
	{
		Debug::Log("DEPLOYWARPTOGGLE dropped, event queue full\n");
		return;
	}

	QueuedProjectors.insert(pProjector);

	Debug::Log("Adding event DEPLOYWARPTOGGLE (%s)\n",
		pProjector->GetTechnoType() ? pProjector->GetTechnoType()->ID : "?");
}

void EventExt::RespondDeployWarpToggle()
{
	if (this->DeployWarp.Projector.m_RTTI != static_cast<unsigned char>(AbstractType::Abstract))
		return;
	auto pProjector = this->DeployWarp.Projector.As_Techno();

	if (!pProjector || pProjector->Health <= 0 || pProjector->InLimbo)
		return;

	if (!TechnoExt::DeployWarp::IsProjectorType(pProjector))
		return;
	if (!CanControl(HouseClass::Array.GetItem(this->HouseIndex), pProjector))
		return;

	TechnoExt::DeployWarp::Toggle(pProjector);
}

bool EventExt::CanControl(HouseClass* pSender, TechnoClass* pTechno)
{
	if (!pSender || !pTechno)
		return false;
	if (pTechno->Owner == pSender || pTechno->MindControlledByHouse == pSender)
		return true;
	if (pTechno->MindControlledBy && pTechno->MindControlledBy->Owner == pSender)
		return true;
	return SessionClass::Instance.GameMode == GameMode::Campaign
		&& pSender->IsHumanPlayer && pTechno->Owner
		&& (pTechno->Owner->IsHumanPlayer || pTechno->Owner->IsInPlayerControl);
}

// ============================================================================
// 战术地图标注
// 标注是纯显示数据：本机操作即时应用并广播，Respond 端跳过作者本人（已应用），
// 其余客户端按（本人或盟友）可见性过滤后重放。事件序各机一致 → 无失步风险。
// ============================================================================

void EventExt::RaiseFullMapMarkAdd(const FullMap::Mark& mark)
{
	HouseClass* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::FullMapMark;
	eventExt.HouseIndex = static_cast<char>(pPlayer->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(Unsorted::CurrentFrame);

	auto& data = eventExt.FullMapMark;
	data.Op = FMMOp_Add;
	data.Id = mark.Id;
	data.Kind = static_cast<uint8_t>(mark.Kind);
	data.ColorIdx = mark.ColorIdx;
	data.Author = static_cast<int8_t>(pPlayer->ArrayIndex);
	data.Cell = mark.Cell;
	data.Cell2 = mark.Cell2;
	data.PointCount = 0; // 手绘点一律走 Append，避免与本地应用重复
	// 内联区 22 wchar（留结尾位），超长文字在广播端截断
	data.TextLen = std::min<uint8_t>(mark.TextLen, FullMap::MaxPointsPerEvent - 1);

	if (data.TextLen > 0)
		wcsncpy_s(data.Data.Text, mark.Text, data.TextLen);

	if (!eventExt.AddEvent())
		Debug::Log("FULLMAPMARK Add dropped, event queue full (id=%u)\n", mark.Id);
}

void EventExt::RaiseFullMapMarkAppend(uint16_t id, const int16_t* points, uint8_t count)
{
	HouseClass* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer || count == 0)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::FullMapMark;
	eventExt.HouseIndex = static_cast<char>(pPlayer->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(Unsorted::CurrentFrame);

	auto& data = eventExt.FullMapMark;
	data.Op = FMMOp_Append;
	data.Id = id;
	data.Kind = static_cast<uint8_t>(FullMapMarkKind::Freehand);
	data.Author = static_cast<int8_t>(pPlayer->ArrayIndex);
	data.PointCount = std::min<uint8_t>(count, FullMap::MaxPointsPerEvent);
	for (int i = 0; i < data.PointCount; ++i)
	{
		data.Data.Points[i][0] = points[i * 2];
		data.Data.Points[i][1] = points[i * 2 + 1];
	}

	if (!eventExt.AddEvent())
		Debug::Log("FULLMAPMARK Append dropped, event queue full (id=%u)\n", id);
}

void EventExt::RaiseFullMapMarkRemove(uint16_t id)
{
	HouseClass* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::FullMapMark;
	eventExt.HouseIndex = static_cast<char>(pPlayer->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(Unsorted::CurrentFrame);

	auto& data = eventExt.FullMapMark;
	data.Op = FMMOp_Remove;
	data.Id = id;
	data.Author = static_cast<int8_t>(pPlayer->ArrayIndex);

	if (!eventExt.AddEvent())
		Debug::Log("FULLMAPMARK Remove dropped, event queue full (id=%u)\n", id);
}

void EventExt::RaiseFullMapMarkClearOwn()
{
	HouseClass* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	EventExt eventExt {};
	eventExt.Type = EventTypeExt::FullMapMark;
	eventExt.HouseIndex = static_cast<char>(pPlayer->ArrayIndex);
	eventExt.Frame = static_cast<uint32_t>(Unsorted::CurrentFrame);

	auto& data = eventExt.FullMapMark;
	data.Op = FMMOp_ClearOwn;
	data.Author = static_cast<int8_t>(pPlayer->ArrayIndex);

	if (!eventExt.AddEvent())
		Debug::Log("FULLMAPMARK ClearOwn dropped, event queue full\n");
}

void EventExt::RespondFullMapMark()
{
	const auto author = static_cast<int8_t>(this->HouseIndex);
	if (this->FullMapMark.Author != author)
		return;

	// 本机作者：本地已即时应用
	if (FullMap::IsLocalAuthor(author))
		return;

	// 可见性不在接收端过滤（各客户端都存档标记，含敌人的）：
	// 渲染时按当前盟友关系实时过滤 → 支持中途结盟/解盟。
	auto* pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	const auto& data = this->FullMapMark;
	switch (data.Op)
	{
	case FMMOp_Add:
	{
		FullMap::Mark m {};
		m.Id = data.Id;
		m.Kind = static_cast<FullMapMarkKind>(data.Kind);
		m.ColorIdx = data.ColorIdx;
		m.Author = author;
		m.Cell = data.Cell;
		m.Cell2 = data.Cell2;
		m.PointCount = std::min<uint8_t>(data.PointCount, FullMap::MaxPoints);
		for (int i = 0; i < m.PointCount; ++i)
		{
			m.Points[i][0] = data.Data.Points[i][0];
			m.Points[i][1] = data.Data.Points[i][1];
		}
		m.TextLen = std::min<uint8_t>(data.TextLen, FullMap::MaxPointsPerEvent - 1);
		if (m.TextLen > 0)
			wcsncpy_s(m.Text, data.Data.Text, m.TextLen);
		FullMap::ApplyAdd(m);
		break;
	}
	case FMMOp_Append:
		FullMap::ApplyAppend(author, data.Id, &data.Data.Points[0][0],
			std::min<uint8_t>(data.PointCount, FullMap::MaxPointsPerEvent));
		break;
	case FMMOp_Remove:
		FullMap::ApplyRemove(author, data.Id);
		break;
	case FMMOp_ClearOwn:
		FullMap::ApplyClearOwn(author);
		break;
	default:
		break;
	}
}

size_t EventExt::GetDataSize(EventTypeExt type)
{
	switch (type)
	{
	case EventTypeExt::SelectionBatch:
		return sizeof(EventExt::SelectionBatch);
	case EventTypeExt::ChoiceBoxClick:
		return sizeof(EventExt::ChoiceBoxClick);
	case EventTypeExt::RecruitPassengers:
		return sizeof(EventExt::RecruitPassengers);
	case EventTypeExt::DeployWarpToggle:
		return sizeof(EventExt::DeployWarp);
	case EventTypeExt::FullMapMark:
		return sizeof(EventExt::FullMapMark);
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
	{
		pEvent->RespondEvent();
		// The native default reads HouseIndex before dispatching. Skip that
		// lookup as well, so rejected extension events cannot index it.
		return 0x4C8109;
	}

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
