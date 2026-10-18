#pragma once

#ifndef IS_RELEASE_VER

#include <Windows.h>

// 调试模式启动时的提示窗：说明调试器状态，等玩家确认后才继续启动。
class DebugNotice
{
public:
	// 返回值：true 继续启动，false 表示用户取消。
	static bool ShowDebuggerNotice(HINSTANCE hInstance, bool debuggerDetached);
	static void ShowConsoleError();
};

#endif
