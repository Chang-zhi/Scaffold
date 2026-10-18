#include "DebugNotice.h"

#ifndef IS_RELEASE_VER

namespace
{
	constexpr wchar_t DebugNoticeClassName[] = L"Scaffold.DebugNotice";
	constexpr wchar_t DebugNoticeTitle[] = L"Scaffold Debug Mode";
	constexpr int DebugNoticeClientWidth = 520;
	constexpr int DebugNoticeClientHeight = 275;

	struct DebugNoticeState
	{
		bool detached = false;
		bool continueRequested = false;
		HINSTANCE instance = nullptr;
		HWND icon = nullptr;
		HWND title = nullptr;
		HWND body = nullptr;
		HWND footer = nullptr;
		HFONT titleFont = nullptr;
		HFONT bodyFont = nullptr;
	};

	void SetControlFont(HWND control, HFONT font)
	{
		if (control)
			SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	}

	LRESULT CALLBACK DebugNoticeProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		DebugNoticeState* const state = reinterpret_cast<DebugNoticeState*>(
			GetWindowLongPtrW(hWnd, GWLP_USERDATA));

		if (message == WM_NCCREATE)
		{
			const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hWnd, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(create->lpCreateParams));
			return TRUE;
		}

		if (!state)
			return DefWindowProcW(hWnd, message, wParam, lParam);

		switch (message)
		{
		case WM_CREATE:
		{
			state->titleFont = CreateFontW(
				-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
			state->bodyFont = CreateFontW(
				-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

			const wchar_t* const title = state->detached
				? L"Debugger ready"
				: L"Manual setup required";
			const wchar_t* const body = state->detached
				? L"Syringe's debugger was detached automatically.\r\n"
				  L"You can attach your debugger to the YR process now."
				: L"Syringe's debugger is still attached.\r\n"
				  L"Detach it from the YR process in Process Hacker or Visual Studio,\r\n"
				  L"then attach your debugger and close Syringe.exe.";

			state->icon = CreateWindowExW(
				0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON,
				35, 44, 32, 32, hWnd, nullptr, state->instance, nullptr);
			SendMessageW(state->icon, STM_SETICON,
				reinterpret_cast<WPARAM>(LoadIconW(nullptr,
					state->detached ? IDI_INFORMATION : IDI_WARNING)), 0);

			state->title = CreateWindowExW(
				0, L"STATIC", title, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				100, 38, 396, 30, hWnd, nullptr, state->instance, nullptr);
			SetControlFont(state->title, state->titleFont);

			state->body = CreateWindowExW(
				0, L"STATIC", body, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				100, 78, 406, 105, hWnd, nullptr, state->instance, nullptr);
			SetControlFont(state->body, state->bodyFont);

			state->footer = CreateWindowExW(
				0, L"STATIC", L"Continue starts YR. Close to cancel.",
				WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				30, 226, 300, 24, hWnd, nullptr, state->instance, nullptr);
			SetControlFont(state->footer, state->bodyFont);

			HWND const button = CreateWindowExW(
				0, L"BUTTON", L"Continue",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
				390, 222, 100, 32, hWnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
				state->instance, nullptr);
			SetControlFont(button, state->bodyFont);
			SetFocus(button);
			return 0;
		}

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
			{
				state->continueRequested = true;
				DestroyWindow(hWnd);
				return 0;
			}
			break;

		case WM_CLOSE:
			state->continueRequested = false;
			DestroyWindow(hWnd);
			return 0;

		case WM_SYSCOMMAND:
			if ((wParam & 0xFFF0) == SC_CLOSE)
			{
				state->continueRequested = false;
				DestroyWindow(hWnd);
				return 0;
			}
			break;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			PAINTSTRUCT paint { };
			HDC const hdc = BeginPaint(hWnd, &paint);
			RECT client { };
			GetClientRect(hWnd, &client);

			HBRUSH const background = CreateSolidBrush(RGB(255, 255, 255));
			FillRect(hdc, &client, background);
			DeleteObject(background);

			HBRUSH const accent = CreateSolidBrush(state->detached
				? RGB(0, 120, 212)
				: RGB(196, 104, 28));
			RECT accentRect = client;
			accentRect.bottom = 6;
			FillRect(hdc, &accentRect, accent);
			DeleteObject(accent);

			HBRUSH const separator = CreateSolidBrush(RGB(230, 232, 235));
			RECT separatorRect = client;
			separatorRect.top = 205;
			separatorRect.bottom = 206;
			FillRect(hdc, &separatorRect, separator);
			DeleteObject(separator);

			EndPaint(hWnd, &paint);
			return 0;
		}

		case WM_CTLCOLORSTATIC:
			{
				HDC const hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, reinterpret_cast<HWND>(lParam) == state->footer
					? RGB(96, 101, 108)
					: RGB(32, 35, 39));
			}
			return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));

		case WM_NCDESTROY:
			if (state->titleFont)
				DeleteObject(state->titleFont);
			if (state->bodyFont)
				DeleteObject(state->bodyFont);
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
			return DefWindowProcW(hWnd, message, wParam, lParam);
		}

		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

	bool RegisterDebugNoticeClass(HINSTANCE hInstance)
	{
		WNDCLASSEXW existing { sizeof(existing) };
		if (GetClassInfoExW(hInstance, DebugNoticeClassName, &existing))
			return true;

		WNDCLASSEXW windowClass { sizeof(windowClass) };
		windowClass.hInstance = hInstance;
		windowClass.lpfnWndProc = DebugNoticeProc;
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
		windowClass.lpszClassName = DebugNoticeClassName;
		return RegisterClassExW(&windowClass) != 0;
	}
}

bool DebugNotice::ShowDebuggerNotice(HINSTANCE hInstance, bool debuggerDetached)
{
	if (!hInstance || !RegisterDebugNoticeClass(hInstance))
		return true;

	const DWORD windowStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
	const DWORD extendedStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
	RECT windowRect { 0, 0, DebugNoticeClientWidth, DebugNoticeClientHeight };
	AdjustWindowRectEx(&windowRect, windowStyle, FALSE, extendedStyle);
	const int windowWidth = windowRect.right - windowRect.left;
	const int windowHeight = windowRect.bottom - windowRect.top;

	RECT workArea { };
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
	const int x = workArea.left + ((workArea.right - workArea.left) - windowWidth) / 2;
	const int y = workArea.top + ((workArea.bottom - workArea.top) - windowHeight) / 2;

	DebugNoticeState state { };
	state.detached = debuggerDetached;
	state.instance = hInstance;
	HWND const owner = GetForegroundWindow();
	const bool restoreOwner = owner && IsWindowEnabled(owner);
	if (restoreOwner)
		EnableWindow(owner, FALSE);

	HWND const dialog = CreateWindowExW(
		extendedStyle,
		DebugNoticeClassName,
		DebugNoticeTitle,
		windowStyle,
		x, y, windowWidth, windowHeight,
		owner, nullptr, hInstance, &state);

	if (!dialog)
	{
		if (restoreOwner)
			EnableWindow(owner, TRUE);
		return true;
	}

	ShowWindow(dialog, SW_SHOWNORMAL);
	// 注入进程里首次创建的非客户区可能沿用空标题，上屏后再设一次并重绘标题栏。
	SetWindowTextW(dialog, DebugNoticeTitle);
	SetWindowPos(dialog, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	RedrawWindow(dialog, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
	UpdateWindow(dialog);
	SetForegroundWindow(dialog);

	MSG message { };
	while (IsWindow(dialog))
	{
		const int result = GetMessageW(&message, nullptr, 0, 0);
		if (result == -1)
			break;
		if (result == 0)
		{
			PostQuitMessage(static_cast<int>(message.wParam));
			break;
		}

		if (!IsDialogMessageW(dialog, &message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	if (restoreOwner)
	{
		EnableWindow(owner, TRUE);
		SetForegroundWindow(owner);
	}

	return state.continueRequested;
}

void DebugNotice::ShowConsoleError()
{
	MessageBoxW(
		nullptr,
		L"Scaffold could not allocate its debug console. Debug output will not "
		L"be available for this session.",
		DebugNoticeTitle,
		MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST
	);
}

#endif
