#include "ModuleHelper.h"

#include <tlhelp32.h>

#include <cstring>

namespace
{
	std::string Narrow(const wchar_t* value)
	{
		if (!value || !*value)
			return {};

		const int length = WideCharToMultiByte(CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
		if (length <= 1)
			return {};

		std::string result(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(CP_ACP, 0, value, -1, result.data(), length, nullptr, nullptr);
		result.resize(static_cast<size_t>(length - 1));
		return result;
	}

	std::string BaseName(std::string_view path)
	{
		const auto separator = path.find_last_of("\\/");
		return std::string(path.substr(separator == std::string_view::npos ? 0 : separator + 1));
	}

	bool EqualsIgnoreCase(std::string_view left, std::string_view right)
	{
		const auto leftName = BaseName(left);
		const auto rightName = BaseName(right);
		if (_stricmp(leftName.c_str(), rightName.c_str()) == 0)
			return true;

		// Accept "Phobos" as a shorthand for "Phobos.dll". This keeps the
		// public helper convenient while retaining exact file-name matching.
		if (rightName.find('.') == std::string::npos)
			return leftName.size() == rightName.size() + 4
				&& _strnicmp(leftName.c_str(), rightName.c_str(), rightName.size()) == 0
				&& _stricmp(leftName.c_str() + rightName.size(), ".dll") == 0;

		return false;
	}

	void ReadOriginalFilename(const char* path, std::string& result)
	{
		DWORD ignored = 0;
		const DWORD bufferSize = GetFileVersionInfoSizeA(path, &ignored);
		if (!bufferSize)
			return;

		std::vector<BYTE> buffer(bufferSize);
		if (!GetFileVersionInfoA(path, 0, bufferSize, buffer.data()))
			return;

		LPVOID value = nullptr;
		UINT valueSize = 0;
		if (VerQueryValueA(buffer.data(), "\\StringFileInfo\\040904b0\\OriginalFilename", &value, &valueSize)
			&& value && valueSize > 1)
		{
			result.assign(static_cast<const char*>(value), valueSize - 1);
			return;
		}

		// Version resources are allowed to use a different language/code page.
		// Walk the translation table so renamed/non-US DLLs are still detectable.
		struct Translation
		{
			WORD Language;
			WORD CodePage;
		};
		Translation* translations = nullptr;
		UINT translationSize = 0;
		if (!VerQueryValueA(buffer.data(), "\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translations), &translationSize)
			|| !translations)
			return;

		for (UINT offset = 0; offset + sizeof(Translation) <= translationSize; offset += sizeof(Translation))
		{
			char query[128] = {};
			_snprintf_s(
				query,
				_TRUNCATE,
				"\\StringFileInfo\\%04x%04x\\OriginalFilename",
				translations[offset / sizeof(Translation)].Language,
				translations[offset / sizeof(Translation)].CodePage);
			value = nullptr;
			valueSize = 0;
			if (VerQueryValueA(buffer.data(), query, &value, &valueSize) && value && valueSize > 1)
			{
				result.assign(static_cast<const char*>(value), valueSize - 1);
				return;
			}
		}
	}
}

namespace ModuleHelper
{
	bool HasSyringeSection(HMODULE module)
	{
		if (!module)
			return false;

		auto* baseAddress = reinterpret_cast<unsigned char*>(module);
		auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(baseAddress);
		if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew < 0)
			return false;

		auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(baseAddress + dosHeader->e_lfanew);
		if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
			return false;

		const auto* sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
		for (WORD index = 0; index < ntHeaders->FileHeader.NumberOfSections; ++index)
		{
			if (strncmp(reinterpret_cast<const char*>(sectionHeader[index].Name), ".syhks00", IMAGE_SIZEOF_SHORT_NAME) == 0)
				return true;
		}

		return false;
	}

	std::vector<ModuleInfo> Enumerate()
	{
		std::vector<ModuleInfo> modules;
		const HANDLE snapshot = CreateToolhelp32Snapshot(
			TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
			GetCurrentProcessId());
		if (snapshot == INVALID_HANDLE_VALUE)
			return modules;

		MODULEENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		if (Module32First(snapshot, &entry))
		{
			do
			{
				ModuleInfo module;
				module.Handle = entry.hModule;
				module.BaseAddress = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
				module.ImageSize = entry.modBaseSize;
				module.ModuleName = Narrow(entry.szModule);
				const auto modulePath = Narrow(entry.szExePath);
				ReadOriginalFilename(modulePath.c_str(), module.OriginalFilename);
				module.IsSyringable = HasSyringeSection(module.Handle);
				modules.emplace_back(std::move(module));
			}
			while (Module32Next(snapshot, &entry));
		}

		CloseHandle(snapshot);
		return modules;
	}

	bool Find(std::string_view moduleName, ModuleInfo& result, bool requireSyringable)
	{
		if (moduleName.empty())
			return false;

		for (const auto& module : Enumerate())
		{
			if (requireSyringable && !module.IsSyringable)
				continue;
			if (EqualsIgnoreCase(module.ModuleName, moduleName)
				|| (!module.OriginalFilename.empty() && EqualsIgnoreCase(module.OriginalFilename, moduleName)))
			{
				result = module;
				return true;
			}
		}

		return false;
	}

	bool IsLoaded(std::string_view moduleName, bool requireSyringable)
	{
		ModuleInfo ignored;
		return Find(moduleName, ignored, requireSyringable);
	}
}
