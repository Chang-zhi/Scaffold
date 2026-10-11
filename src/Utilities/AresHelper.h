#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <Windows.h>

class AresHelper
{
public:
	enum class Version
	{
		Unknown = -1,
		Ares30 = 0,
		Ares30p,
	};

private:
	typedef std::unordered_map<DWORD, Version> AresTimestampMap;

	// timestamp bytes for each version
	static const AresTimestampMap AresTimestampBytes;

	static void GetGameModulesBaseAddresses();

public:
	static HMODULE AresDllHmodule;
	static uintptr_t AresBaseAddress;
	static HMODULE PhobosDllHmodule;
	static uintptr_t PhobosBaseAddress;
	static uintptr_t ScaffoldBaseAddress;

	// numeric id of currently used version, zero-indexed, -1 is unknown or missing
	static Version AresVersion;
	// is Ares detected and version known?
	static bool CanUseAres;
	// is a Syringe-injected Phobos module present? This only reports presence;
	// it does not imply that a Phobos ABI is available.
	static bool CanUsePhobos;

	// Finds a loaded module by file name. Matching is case-insensitive and also
	// accepts a DLL name without the .dll suffix. Set requireSyringable to true
	// when the module must contain Syringe's .syhks00 section.
	static HMODULE FindModule(const char* moduleName, bool requireSyringable = false);
	static uintptr_t GetModuleBaseAddress(const char* moduleName, bool requireSyringable = false);
	static bool IsModuleLoaded(const char* moduleName, bool requireSyringable = false);

	static void Init();
};
