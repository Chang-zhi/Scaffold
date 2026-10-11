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

	if (Scaffold::DetachFromDebugger())
	{
		MessageBoxW(NULL,
		L"You can now attach a debugger.\n\n"

		L"Press OK to continue YR execution.",
		L"Debugger Notice", MB_OK);
	}
	else
	{
		MessageBoxW(NULL,
		L"You can now attach a debugger.\n\n"

		L"To attach a debugger find the YR process in Process Hacker "
		L"/ Visual Studio processes window and detach debuggers from it, "
		L"then you can attach your own debugger. After this you should "
		L"terminate Syringe.exe because it won't automatically exit when YR is closed.\n\n"

		L"Press OK to continue YR execution.",
		L"Debugger Notice", MB_OK);
	}

#endif

#ifndef IS_RELEASE_VER

	if (!Console::Create())
	{
		MessageBoxW(NULL,
		L"Failed to allocate the debug console!",
		L"Debug Console Notice", MB_OK);
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
