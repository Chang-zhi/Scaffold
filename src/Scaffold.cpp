#include "Scaffold.h"
#include <Interop/ScaffoldInterop.h>

#include <Ext/Techno/Body.h>

#include <Drawing.h>
#include <SessionClass.h>
#include <Unsorted.h>

#include <Utilities/Debug.h>
#include <Utilities/Patch.h>
#include <Utilities/Macro.h>
#include "Utilities/AresHelper.h"
#include "Utilities/Parser.h"

HANDLE Scaffold::hInstance = 0;

char Scaffold::readBuffer[Scaffold::readLength];
wchar_t Scaffold::wideBuffer[Scaffold::readLength];
const char* Scaffold::AppIconPath = nullptr;

bool Scaffold::DisplayDamageNumbers = false;

const wchar_t* Scaffold::VersionDescription = L"Scaffold build #" _STR(BUILD_NUMBER) L". Please test the build before shipping.";

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
				35, 44, 32, 32, hWnd, nullptr,
				reinterpret_cast<HINSTANCE>(Scaffold::hInstance), nullptr);
			SendMessageW(state->icon, STM_SETICON,
				reinterpret_cast<WPARAM>(LoadIconW(nullptr,
					state->detached ? IDI_INFORMATION : IDI_WARNING)), 0);

			state->title = CreateWindowExW(
				0, L"STATIC", title, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				100, 38, 396, 30, hWnd, nullptr,
				reinterpret_cast<HINSTANCE>(Scaffold::hInstance), nullptr);
			SetControlFont(state->title, state->titleFont);

			state->body = CreateWindowExW(
				0, L"STATIC", body, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				100, 78, 406, 105, hWnd, nullptr,
				reinterpret_cast<HINSTANCE>(Scaffold::hInstance), nullptr);
			SetControlFont(state->body, state->bodyFont);

			state->footer = CreateWindowExW(
				0, L"STATIC", L"Continue starts YR. Close to cancel.",
				WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				30, 226, 300, 24, hWnd, nullptr,
				reinterpret_cast<HINSTANCE>(Scaffold::hInstance), nullptr);
			SetControlFont(state->footer, state->bodyFont);

			HWND const button = CreateWindowExW(
				0, L"BUTTON", L"Continue",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
				390, 222, 100, 32, hWnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
				reinterpret_cast<HINSTANCE>(Scaffold::hInstance), nullptr);
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

	bool ShowDebuggerNotice(bool detached)
	{
		HINSTANCE const hInstance = reinterpret_cast<HINSTANCE>(Scaffold::hInstance);
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
		state.detached = detached;
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

	void ShowDebugConsoleError()
	{
		MessageBoxW(
			nullptr,
			L"Scaffold could not allocate its debug console. Debug output will not "
			L"be available for this session.",
			DebugNoticeTitle,
			MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST
		);
	}
}
#endif

void Scaffold::ExeTerminate()
{
	Console::Release();
}

bool __stdcall DllMain(HANDLE hInstance, DWORD dwReason, LPVOID v)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		Scaffold::hInstance = hInstance;
	}
	return true;
}

void Scaffold::ExeRun()
{
	Patch::ApplyStatic();

	AresHelper::Init();

	// 部署超时空力场：vtable 包装
	TechnoExt::DeployWarp::InstallActionWrappers();

#ifndef IS_RELEASE_VER

	if (!ShowDebuggerNotice(Scaffold::DetachFromDebugger()))
		ExitProcess(ERROR_CANCELLED);

#endif

#ifndef IS_RELEASE_VER

	if (!Console::Create())
	{
		ShowDebugConsoleError();
	}

#endif

	// 控制台就绪后才上屏:此前 AresHelper 的探测结果走的是 LogDeferred。
	Debug::Log("[Scaffold] Init complete. Ares=%s, Phobos=%s, console=%s\n",
		AresHelper::CanUseAres ? "on" : "off",
		AresHelper::CanUsePhobos ? "on" : "off",
		Console::ConsoleHandle ? "on" : "off");
}

void Scaffold::CmdLineParse(char** ppArgs, int nNumArgs)
{
	bool foundInheritance = false;
	bool foundInclude = false;
	bool dontSetExceptionHandler = false;
	Parser<bool> boolParser { };

	// > 1 because the exe path itself counts as an argument, too!
	for (int i = 1; i < nNumArgs; i++)
	{
		const char* pArg = ppArgs[i];
		if (!pArg)
			continue;

		std::string arg = pArg;

		if (_stricmp(pArg, "-Icon") == 0)
		{
			if (i + 1 < nNumArgs && ppArgs[i + 1])
				Scaffold::AppIconPath = ppArgs[++i];

			continue;
		}
		if (_stricmp(pArg, "-Inheritance") == 0)
		{
			foundInheritance = true;
		}
		if (_stricmp(pArg, "-Include") == 0)
		{
			foundInclude = true;
		}
		if (arg.starts_with("-ExceptionHandler="))
		{
			auto delimIndex = arg.find("=");
			auto value = arg.substr(delimIndex + 1, arg.size() - delimIndex - 1);

			bool v = dontSetExceptionHandler;
			if (boolParser.TryParse(value.c_str(), &v))
				dontSetExceptionHandler = !v;
		}
	}

	if (foundInclude)
	{
		Patch::Apply_RAW(0x474200, // Apply CCINIClass_ReadCCFile1_DisableAres
			{ 0x8B, 0xF1, 0x8D, 0x54, 0x24, 0x0C }
		);

		Patch::Apply_RAW(0x474314, // Apply CCINIClass_ReadCCFile2_DisableAres
			{ 0x81, 0xC4, 0xA8, 0x00, 0x00, 0x00 }
		);
	}
	else
	{
		Patch::Apply_RAW(0x474230, // Revert CCINIClass_Load_Inheritance
			{ 0x8B, 0xE8, 0x88, 0x5E, 0x40 }
		);
	}

	if (foundInheritance)
	{
		Patch::Apply_RAW(0x528A10, // Apply INIClass_GetString_DisableAres
			{ 0x83, 0xEC, 0x0C, 0x33, 0xC0 }
		);

		Patch::Apply_RAW(0x526CC0, // Apply INIClass_GetKeyName_DisableAres
			{ 0x8B, 0x54, 0x24, 0x04, 0x83, 0xEC, 0x0C }
		);
	}
	else
	{
		Patch::Apply_RAW(0x528BAC, // Revert INIClass_GetString_Inheritance_NoEntry
			{ 0x8B, 0x7C, 0x24, 0x2C, 0x33, 0xC0, 0x8B, 0x4C, 0x24, 0x28 }
		);
	}

	Game::DontSetExceptionHandler = dontSetExceptionHandler;

	Debug::Log("Initialized version: " PRODUCT_VERSION "\n");
	Debug::Log("ExceptionHandler is %s\n", dontSetExceptionHandler ? "not present" : "present");
}


// =============================
// hooks

DEFINE_HOOK(0x7CD810, ExeRun, 0x9)
{
	Scaffold::ExeRun();

	return 0;
}

// Avoid confusing the profiler unless really necessary
#ifdef DEBUG
DEFINE_NAKED_HOOK(0x7CD8EA, _ExeTerminate)
{
	// Call WinMain
	SET_REG32(EAX, 0x6BB9A0);
	CALL(EAX);
	PUSH_REG(EAX);

	__asm {call Scaffold::ExeTerminate};

	// Jump back
	POP_REG(EAX);
	SET_REG32(EBX, 0x7CD8EF);
	__asm {jmp ebx};
}
#endif

DEFINE_HOOK(0x52F639, _YR_CmdLineParse, 0x5)
{
	GET(char**, ppArgs, ESI);
	GET(int, nNumArgs, EDI);

	Scaffold::CmdLineParse(ppArgs, nNumArgs);
	ScaffoldInterop::Init();
	Debug::LogDeferredFinalize();
	return 0;
}

#ifndef IS_RELEASE_VER

#pragma warning (disable : 4091)
#pragma warning (disable : 4245)

#include <Dbghelp.h>
#include <tlhelp32.h>

bool Scaffold::DetachFromDebugger()
{
	auto GetDebuggerProcessId = [](DWORD dwSelfProcessId) -> DWORD
		{
			DWORD dwParentProcessId = -1;
			HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (hSnapshot == INVALID_HANDLE_VALUE)
				return dwParentProcessId;

			PROCESSENTRY32 pe32 { };
			pe32.dwSize = sizeof(PROCESSENTRY32);
			if (Process32First(hSnapshot, &pe32))
			{
				do
				{
					if (pe32.th32ProcessID == dwSelfProcessId)
					{
						dwParentProcessId = pe32.th32ParentProcessID;
						break;
					}
				}
				while (Process32Next(hSnapshot, &pe32));
			}

			CloseHandle(hSnapshot);
			return dwParentProcessId;
		};

	HMODULE hModule = LoadLibraryA("ntdll.dll");
	if (hModule != NULL)
	{
		auto const NtRemoveProcessDebug =
			(NTSTATUS(__stdcall*)(HANDLE, HANDLE))GetProcAddress(hModule, "NtRemoveProcessDebug");
		auto const NtSetInformationDebugObject =
			(NTSTATUS(__stdcall*)(HANDLE, ULONG, PVOID, ULONG, PULONG))GetProcAddress(hModule, "NtSetInformationDebugObject");
		auto const NtQueryInformationProcess =
			(NTSTATUS(__stdcall*)(HANDLE, ULONG, PVOID, ULONG, PULONG))GetProcAddress(hModule, "NtQueryInformationProcess");
		auto const NtClose =
			(NTSTATUS(__stdcall*)(HANDLE))GetProcAddress(hModule, "NtClose");

		if (!NtRemoveProcessDebug || !NtSetInformationDebugObject || !NtQueryInformationProcess || !NtClose)
		{
			FreeLibrary(hModule);
			return false;
		}

		HANDLE hDebug = nullptr;
		HANDLE hCurrentProcess = GetCurrentProcess();
		NTSTATUS status = NtQueryInformationProcess(hCurrentProcess, 30, &hDebug, sizeof(HANDLE), 0);
		if (0 <= status)
		{
			ULONG killProcessOnExit = FALSE;
			status = NtSetInformationDebugObject(
				hDebug,
				1,
				&killProcessOnExit,
				sizeof(ULONG),
				NULL
			);
			if (0 <= status)
			{
				const auto pid = GetDebuggerProcessId(GetProcessId(hCurrentProcess));
				status = NtRemoveProcessDebug(hCurrentProcess, hDebug);
				if (0 <= status)
				{
					HANDLE hDbgProcess = pid != 0 && pid != static_cast<DWORD>(-1)
						? OpenProcess(PROCESS_TERMINATE, FALSE, pid)
						: nullptr;
					if (hDbgProcess && hDbgProcess != INVALID_HANDLE_VALUE)
					{
						BOOL ret = TerminateProcess(hDbgProcess, EXIT_SUCCESS);
						CloseHandle(hDbgProcess);
						NtClose(hDebug);
						FreeLibrary(hModule);
						return ret != FALSE;
					}
				}
			}
			NtClose(hDebug);
		}
		FreeLibrary(hModule);
	}

	return false;
}
#endif
