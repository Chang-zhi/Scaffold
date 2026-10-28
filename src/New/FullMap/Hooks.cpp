#include <New/FullMap/FullMap.h>

#include <Surface.h>
#include <MessageListClass.h>
#include <WWMouseClass.h>

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <Helpers/Macro.h>
#include <Utilities/Debug.h>

#pragma comment(lib, "comctl32.lib")

// 战术层渲染尾段（ScreenChoiceBox 同款屏幕锚点）：战术层之上、侧栏之下绘制。
DEFINE_HOOK(0x6D4B25, TacticalClass_Draw_FullMap, 0x5)
{
	FullMap::OnDraw();
	return 0;
}

// 战术层世界层（FootPath/TextBox 同款锚点）：战术视野内叠加标记。
DEFINE_HOOK(0x6D4684, TacticalClass_Draw_FullMapWorldMarks, 0x6)
{
	FullMap::DrawWorldMarks();
	return 0;
}

// GScreenClass::GetInputAndUpdate(0x4F4320) 末尾的
// Update(this, const int& key, const Point2D& mouse) 调用点（call [eax+28h]）。
// 打开战术地图时把喂给 Update 的鼠标坐标替换为视野中心：
// 控件命中、框选起手、边缘滚屏、悬停光标全部失效。
// 注意：键值不能动——主循环读取同一个键值做快捷键派发（含本功能的关闭键）。
// 参数已在栈上：[esp+0]=&key（push esi），[esp+4]=&mouse（push ecx）。
DEFINE_HOOK(0x4F43D9, GScreen_GetInputAndUpdate_FullMapBlock, 0x6)
{
	if (FullMap::ShouldBlockGameInput())
	{
		GET_STACK(Point2D*, pMouse, 4);

		if (pMouse)
		{
			const auto& view = DSurface::ViewBounds;
			*pMouse = Point2D { view.X + view.Width / 2, view.Y + view.Height / 2 };
		}
	}
	return 0;
}

// MouseClass 点击动作分发器（0x4AB9B0，被全部 8 个游戏模式类虚表共享）：
// 框选、下令、右键取消、信标放置等所有点击动作都在此落地。
// 打开期间整体跳过，跳到函数自身的 retn 14h（0x4AC2A7）以正确清理 5 个栈参数。
DEFINE_HOOK(0x4AB9B0, MouseClass_ClickActions_FullMapBlock, 0x6)
{
	if (FullMap::ShouldBlockGameInput())
	{
		R->EAX(0);
		return 0x4AC2A7; // retn 14h
	}
	return 0;
}

// MouseClass 左键按下动作入口（0x4AAE90）：框选起手、点选单位/建筑、下令、信标等
// 全部点击动作的"按下"侧；与下面的 0x4AB9B0（抬起分发）成对。
// 关键：TacticalClass 每帧轮询路径（sub_692F30 → sub_4AAE90）不经过窗口消息，
// 仅靠子类化吞消息挡不住，必须在这里拦。前 3 条指令共 8 字节（83 EC 10 / 53 / 8B 5C 24 18），
// 跳 retn 14h 时栈上仍是调用方参数，可正确清栈。
DEFINE_HOOK(0x4AAE90, MouseClass_DownActions_FullMapBlock, 0x8)
{
	if (FullMap::ShouldBlockGameInput())
	{
		// 限频证据日志：确认"按下动作"路径确实被拦（不是每帧刷屏）
		static DWORD lastLog = 0;
		const DWORD now = timeGetTime();
		if (now - lastLog >= 2000)
		{
			lastLog = now;
			Debug::Log("[FullMap] blocked MouseDownAction (0x4AAE90) n=%u\n",
				static_cast<unsigned>(now));
		}
		R->EAX(0);
		return 0x4AB796; // retn 14h（5 个栈参数）
	}
	return 0;
}

// 框选橡皮筋起点/更新（0x4AC310 / 0x4AC380）：打开期间一并拦掉，
// 防止打开前遗留的捕获状态继续画框选。
DEFINE_HOOK(0x4AC310, MouseClass_BandStart_FullMapBlock, 0x6)
{
	if (FullMap::ShouldBlockGameInput())
	{
		R->EAX(0);
		return 0x4AC36E; // retn 4
	}
	return 0;
}

DEFINE_HOOK(0x4AC380, MouseClass_BandUpdate_FullMapBlock, 0x6)
{
	if (FullMap::ShouldBlockGameInput())
	{
		R->EAX(0);
		return 0x4AC4E4; // retn 4
	}
	return 0;
}

// InputManagerClass::IsKeyPressed（0x54F5C0，stdcall，游戏轮询按键/鼠标键的公共入口，
// 经 sub_53EC90 读 word_AA0168 镜像表或直连 GetAsyncKeyState）。
// 打开期间把鼠标键的询问一律回答"未按下"——框选起手、控件点击从轮询源头被掐断。
// 跳到 0x54F601（neg eax; retn 4）：跳过被替换的 push esi，不破坏寄存器。
DEFINE_HOOK(0x54F5C0, InputManager_IsKeyPressed_FullMapBlock, 0x5)
{
	if (FullMap::ShouldBlockGameInput())
	{
		GET_STACK(uint8_t, key, 4);
		if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON
			|| key == VK_XBUTTON1 || key == VK_XBUTTON2)
		{
			R->EAX(0);
			return 0x54F601; // neg eax; retn 4
		}
	}
	return 0;
}

// 原版雷达入口：单击保持原版移动视野；第二次按下交给 FullMap。
// 打开后原版鼠标输入会被屏蔽，关闭双击由 FullMap::PollInput 处理。
DEFINE_HOOK(0x6539D0, RadarClass_GetMouseAction_FullMapMinimapDoubleClick, 0x7)
{
	const bool blocked = FullMap::ShouldBlockGameInput();
	GET_STACK(unsigned char, eventFlags, 0x4);
	if (blocked || FullMap::HandleNativeMinimapClick(eventFlags))
	{
		// Entry hook precedes all pushes. 0x653EDF is the function's single
		// retn 0Ch instruction, so skipping straight to it preserves the stack.
		R->EAX(1);
		return 0x653EDF;
	}
	return 0;
}

// Normalize cursor requests before the game changes the software cursor image.
// Repeated SetCursor calls from the renderer cannot prevent intermediate swaps.
DEFINE_HOOK(0x5BDA80, MouseClass_SetCursor_FullMap, 0x6)
{
	if (FullMap::ShouldBlockGameInput())
	{
		R->Stack<unsigned int>(0x4, static_cast<unsigned int>(MouseCursorType::Default));
		R->Stack8(0x8, 0);
	}
	return 0;
}

DEFINE_HOOK(0x5BDC80, MouseClass_UpdateCursor_FullMap, 0x7)
{
	if (FullMap::ShouldBlockGameInput())
	{
		R->Stack<unsigned int>(0x4, static_cast<unsigned int>(MouseCursorType::Default));
		R->Stack8(0x8, 0);
	}
	return 0;
}

DEFINE_HOOK(0x5BDAB0, MouseClass_UpdateCursorMinimapState_FullMap, 0x7)
{
	if (FullMap::ShouldBlockGameInput())
		R->Stack8(0x4, 0);
	return 0;
}

// RadarClass::Render 内位图→侧栏 blit 之后（0x657668 = 视野框绘制调用）：
// 在侧栏小地图上叠加战术地图标记色点。画在侧栏表面而非雷达位图上，
// 色点不会随放大 blit 进入战术地图。
DEFINE_HOOK(0x657668, RadarClass_Render_FullMapMinimapMarks, 0x5)
{
	// 保持现有滚轮与鼠标拦截；小地图打开由原版雷达输入入口处理。
	FullMap::InstallInputHooks();
	FullMap::DrawMinimapMarks();
	return 0;
}

// 主循环键命令派发（0x55DEE0）：sub_55E420（消息编辑行输入）调用之后、
// 热键检查之前。原生机制是编辑行吃掉按键（清零 *this）→ 热键不触发；
// 我们的场景下强制复刻：文字输入激活期间把键清零。
// 0x55DEED = mov eax,[ebp]（this 在 ebp），5 字节。
//
// 补充（本轮问题 6）：地图打开期间同样要吞热键——否则选中"文字"工具打字时，
// 字母键会先被热键系统吃掉，导致输入框收不到字符。但必须放行"关闭键"，
// 否则用户无法用快捷键关掉地图。
DEFINE_HOOK(0x55DEED, MainLoop_KeyCommand_EditFocusGuard, 0x5)
{
	GET(DWORD*, pKey, EBP);
	if (!pKey || !*pKey)
		return 0;

	const DWORD key = *pKey & 0xFFFF;

	// 情况一：正在打字（原生编辑行或内置编辑器）→ 吞掉所有键，交给输入框。
	// 情况二：地图打开但没在打字 → 吞掉游戏热键（否则编队/移动命令会在
	//        地图底下生效），但必须放行"地图开关键"和 ESC，否则关不掉地图。
	const bool typing = FullMap::IsTypingActive();
	const bool mapOpen = FullMap::ShouldBlockGameInput();
	if (!typing && !mapOpen)
		return 0;

	if (!typing && FullMap::IsMapToggleKey(key))
		return 0;   // 放行开关热键

	static DWORD s_LastKeyLog = 0;
	static DWORD s_LastKey = 0;
	const DWORD now = timeGetTime();
	if (key != s_LastKey || now - s_LastKeyLog > 1000)
	{
		s_LastKey = key;
		s_LastKeyLog = now;
		Debug::Log("[FullMap] key 0x%X suppressed (typing=%d map=%d)\n",
			key, typing ? 1 : 0, mapOpen ? 1 : 0);
	}

	*pKey = 0;
	return 0;
}

// ============================================================================
// gamemd IAT 拦截（GetAsyncKeyState / PeekMessageA）
// 两个悬而未决的问题都出在"游戏自己直接调 user32"：
//   * 点击穿透：部分轮询路径绕过 IsKeyPressed(0x54F5C0) 直接 GetAsyncKeyState；
//   * 滚轮偶发失效：游戏的消息泵（PeekMessageA）先于渲染期轮询消费掉 WM_MOUSEWHEEL。
// ============================================================================

namespace
{
	// gamemd 导入表槽位（IDA 确认）
	constexpr DWORD IAT_GetAsyncKeyState = 0x7E13E0;
	constexpr DWORD IAT_PeekMessageA = 0x7E1484;

	decltype(&GetAsyncKeyState) g_RealGetAsyncKeyState = nullptr;
	decltype(&PeekMessageA) g_RealPeekMessageA = nullptr;

	// 游戏窗口子类化（comctl32 SetWindowSubclass）：
	// 打开战术地图期间吞掉鼠标键消息——点击的绝对源头。
	// 不得用原生 SetWindowLongPtr(GWLP_WNDPROC)：游戏窗口已被其他组件
	// （CnCNet/Ares 等）comctl32 子类化，强拆会返回 0xFFFFxxxx 魔法值，
	// 后续消息调用即崩溃；SetWindowSubclass 与既有子类链安全共存。
	constexpr UINT_PTR FullMapSubclassId = 0x5CA0;

	LRESULT CALLBACK FullMapSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*uSubclass*/, DWORD_PTR /*dwRefData*/)
	{
		if (FullMap::ShouldBlockGameInput())
		{
			switch (msg)
			{
			case WM_LBUTTONDOWN: case WM_LBUTTONUP:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP:
			case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP:
				return 0;

			// 注意：这里不碰光标。游戏的光标是它自己绘制的软件光标，
			// 强行 SetCursor 会让 Windows 系统指针冒出来盖住游戏光标（很丑）。
			// 只把 WM_MOUSEMOVE 吞掉，避免地图底下继续做悬停命中。
			case WM_MOUSEMOVE:
				return 0;
			default:
				break;
			}
		}
		else if (msg == WM_RBUTTONDOWN
			&& FullMap::TryEraseMarkAtScreen({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
		{
			// 世界层右键命中本人标记：删除并吞掉该次右键
			return 0;
		}
		return ::DefSubclassProc(hWnd, msg, wParam, lParam);
	}

	SHORT WINAPI FullMapGetAsyncKeyState(int vKey)
	{
		// 打开战术地图期间，游戏对鼠标键的一切询问都回答"未按下"
		if (FullMap::ShouldBlockGameInput()
			&& (vKey == VK_LBUTTON || vKey == VK_RBUTTON || vKey == VK_MBUTTON
				|| vKey == VK_XBUTTON1 || vKey == VK_XBUTTON2))
			return 0;

		return g_RealGetAsyncKeyState(vKey);
	}

	BOOL WINAPI FullMapPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT minMsg, UINT maxMsg, UINT flags)
	{
		const BOOL result = g_RealPeekMessageA(lpMsg, hWnd, minMsg, maxMsg, flags);
		if (result && (flags & PM_REMOVE) && lpMsg && lpMsg->message == WM_MOUSEWHEEL)
		{
			// 只在打开时接管滚轮；游戏本身不消费滚轮，吃掉避免重复
			if (FullMap::IsOpen)
			{
				FullMap::NotifyWheelDelta(GET_WHEEL_DELTA_WPARAM(lpMsg->wParam),
					(GET_KEYSTATE_WPARAM(lpMsg->wParam) & MK_CONTROL) != 0);
				// The wheel message has been removed and consumed. Keep PeekMessage's
				// success result so the game's message pump does not enter its idle
				// path and stall the next tactical draw for hundreds of milliseconds.
				lpMsg->message = WM_NULL;
				lpMsg->wParam = 0;
				lpMsg->lParam = 0;
				return TRUE;
			}
		}
		return result;
	}

	void PatchIATSlot(DWORD slot, void* fn, void** save)
	{
		DWORD oldProtect = 0;
		if (VirtualProtect(reinterpret_cast<void*>(slot), 4, PAGE_READWRITE, &oldProtect))
		{
			*save = *reinterpret_cast<void**>(slot);
			*reinterpret_cast<void**>(slot) = fn;
			VirtualProtect(reinterpret_cast<void*>(slot), 4, oldProtect, &oldProtect);
		}
	}
}

void FullMap::InstallInputHooks()
{
	static bool installed = false;
	if (installed)
		return;
	installed = true;

	PatchIATSlot(IAT_GetAsyncKeyState, &FullMapGetAsyncKeyState,
		reinterpret_cast<void**>(&g_RealGetAsyncKeyState));
	PatchIATSlot(IAT_PeekMessageA, &FullMapPeekMessageA,
		reinterpret_cast<void**>(&g_RealPeekMessageA));

	// 游戏窗口子类化（WWMouse 持有游戏 HWND）
	HWND gameWnd = nullptr;
	if (const auto* pMouse = WWMouseClass::Instance; pMouse)
		gameWnd = pMouse->hWnd;

	const bool subclassOk = gameWnd
		&& ::SetWindowSubclass(gameWnd, &FullMapSubclassProc, FullMapSubclassId, 0) != FALSE;

	Debug::Log("[FullMap] input hooks: GAKS=%p PMA=%p subclass=%d hwnd=%p\n",
		static_cast<void*>(g_RealGetAsyncKeyState), static_cast<void*>(g_RealPeekMessageA),
		subclassOk ? 1 : 0, static_cast<void*>(gameWnd));
}
