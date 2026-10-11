#pragma once

#include <Windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace ModuleHelper
{
	struct ModuleInfo
	{
		HMODULE Handle = nullptr;
		uintptr_t BaseAddress = 0;
		DWORD ImageSize = 0;
		std::string ModuleName;
		std::string OriginalFilename;
		bool IsSyringable = false;
	};

	// Enumerates the modules currently loaded in the current process. The module
	// name is always available; OriginalFilename is populated when the PE version
	// resource contains it.
	std::vector<ModuleInfo> Enumerate();

	// Finds a module by its file name (case-insensitive). Both the loader name and
	// the version resource's OriginalFilename are considered. A name without a
	// .dll suffix also matches the corresponding DLL.
	bool Find(std::string_view moduleName, ModuleInfo& result, bool requireSyringable = false);

	bool IsLoaded(std::string_view moduleName, bool requireSyringable = false);

	// Syringe marks injected PE images with this section. This is useful when a
	// caller needs to distinguish an injected DLL from an ordinary loaded DLL.
	bool HasSyringeSection(HMODULE module);
}
