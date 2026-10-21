#include "InteropModule.h"

#include <Interop/Api/ApiDecl.h>

#include <Utilities/Debug.h>

#include <tlhelp32.h>

#include <cstring>

namespace
{
	// Debug::Log 接收的是窄字符格式串，因此模块名需要先转成窄字符再输出。
	std::string Narrow(const wchar_t* szWide)
	{
		if (!szWide || !*szWide)
			return {};

		const int needed = ::WideCharToMultiByte(CP_ACP, 0, szWide, -1, nullptr, 0, nullptr, nullptr);

		if (needed <= 1)
			return {};

		std::string narrow(static_cast<size_t>(needed), '\0');
		::WideCharToMultiByte(CP_ACP, 0, szWide, -1, narrow.data(), needed, nullptr, nullptr);
		narrow.resize(static_cast<size_t>(needed) - 1);

		return narrow;
	}

	void LogVersion(const char* prefix, const std::string& moduleName, const InteropAPIVersion& version)
	{
		Debug::Log("%s %s (Interop API v%u.%u.%u)\n",
			prefix, moduleName.c_str(), version.major, version.minor, version.patch);
	}
}

// ============================================================================
// 提供方发现
// ============================================================================

HMODULE InteropModule::FindProvider(bool& conflictDetected)
{
	if (conflictDetected)
		conflictDetected = false;

	HANDLE hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);

	if (hSnapshot == INVALID_HANDLE_VALUE)
	{
		Debug::Log("[Interop] [Error]: module snapshot failed (%lu)\n", ::GetLastError());
		return nullptr;
	}

	HMODULE hFound = nullptr;
	std::string foundName;
	InteropAPIVersion foundVersion {};

	MODULEENTRY32W me {};
	me.dwSize = sizeof(me);

	if (::Module32FirstW(hSnapshot, &me))
	{
		do
		{
			// 通过公共解析器查找，未修饰名与 __stdcall 修饰名均可被接受。
			auto const pfn = InteropApi::ResolveInteropExport<InteropApi::fnGetInteropAPIVersion>(
				me.hModule, "GetInteropAPIVersion");

			if (!pfn)
				continue;

			InteropAPIVersion version {};

			if (FAILED(pfn(&version)) || version.major < 1)
				continue;

			if (!hFound)
			{
				hFound = me.hModule;
				foundName = Narrow(me.szModule);
				foundVersion = version;

				LogVersion("[ScaffoldInterop] Provider found:", foundName, version);
				continue;
			}

			// 只有在 API 版本不一致时，第二个提供方才是问题；版本完全相同意味着
			// 二者按定义就是 ABI 兼容的。
			if (version != foundVersion)
			{
				Debug::Log("[Interop] [Error]: conflicting Interop providers detected\n");
				LogVersion("[ScaffoldInterop] [Error]:  ", foundName, foundVersion);
				LogVersion("[ScaffoldInterop] [Error]:  ", Narrow(me.szModule), version);
				Debug::Log("[Interop] [Error]: Interop API disabled\n");

				::CloseHandle(hSnapshot);

				if (conflictDetected)
					conflictDetected = true;

				return nullptr;
			}
		} while (::Module32NextW(hSnapshot, &me));
	}

	::CloseHandle(hSnapshot);

	return hFound;
}

// ============================================================================
// 诊断
// ============================================================================

void InteropModule::CollectDecorations(HMODULE hProvider, const char* exportName, std::vector<std::string>& out)
{
	out.clear();

	if (!hProvider || !exportName || !*exportName)
		return;

	auto const base = reinterpret_cast<const BYTE*>(hProvider);
	auto const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return;

	auto const nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);

	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return;

	auto const& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

	if (directory.VirtualAddress == 0 || directory.Size == 0)
		return;

	// 对于已加载的映像，导出目录用相对基址的 RVA 表示。
	auto const exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);

	if (exports->NumberOfNames == 0 || exports->AddressOfNames == 0)
		return;

	auto const names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);

	std::string prefix;
	prefix.reserve(64);
	prefix += '_';
	prefix += exportName;
	prefix += '@';

	for (DWORD i = 0; i < exports->NumberOfNames; ++i)
	{
		auto const name = reinterpret_cast<const char*>(base + names[i]);

		if (name && std::strncmp(name, prefix.c_str(), prefix.size()) == 0)
			out.emplace_back(name);
	}
}

void InteropModule::ReportUnresolvedExport(HMODULE hProvider, const char* exportName, bool isRequired)
{
	Debug::Log("[Interop] %s: %s export '%s' not found\n",
		isRequired ? "[Error]" : "[Warning]",
		isRequired ? "required" : "optional",
		exportName);

	// 顺便报告提供方实际导出的名称。这样上游一旦改动签名（会静默地改变修饰名），
	// 问题就会直接暴露出来，而不是留下一个莫名其妙的"not found"。
	std::vector<std::string> decorations;
	CollectDecorations(hProvider, exportName, decorations);

	if (decorations.empty())
	{
		Debug::Log("[Interop]   no '_%s@...' export present either\n", exportName);
		return;
	}

	for (const auto& decoration : decorations)
	{
		Debug::Log("[Interop]   provider exports '%s' - signature may have changed\n",
			decoration.c_str());
	}
}
