#include "AresHelper.h"
#include "AresFunctions.h"
#include <Scaffold.h>
#include <Utilities/Debug.h>
#include <Utilities/ModuleHelper.h>
#include <Utilities/Patch.h>
#include <CRC.h>

class TechnoClass;
class TechnoTypeClass;

uintptr_t AresHelper::ScaffoldBaseAddress = 0x0;
uintptr_t AresHelper::AresBaseAddress = 0x0;
HMODULE AresHelper::AresDllHmodule = nullptr;
uintptr_t AresHelper::PhobosBaseAddress = 0x0;
HMODULE AresHelper::PhobosDllHmodule = nullptr;
AresHelper::Version AresHelper::AresVersion = AresHelper::Version::Unknown;
bool AresHelper::CanUseAres = false;
bool AresHelper::CanUsePhobos = false;

const AresHelper::AresTimestampMap AresHelper::AresTimestampBytes =
{
	{ 0x5fc37ef6, Version::Ares30 },
	{ 0x61daa114, Version::Ares30p },
};

#ifndef SCAFFOLD_DLL
#define SCAFFOLD_DLL "Scaffold.dll"
#endif

#ifndef PHOBOS_DLL
#define PHOBOS_DLL "Phobos.dll"
#endif

namespace
{
	bool IsSunModule(const ModuleHelper::ModuleInfo& module)
	{
		return (!_stricmp(module.ModuleName.c_str(), "Sun.exe")
			|| (!module.OriginalFilename.empty() && !_stricmp(module.OriginalFilename.c_str(), "Sun.exe")));
	}
}

void AresHelper::GetGameModulesBaseAddresses()
{
	AresBaseAddress = 0;
	AresDllHmodule = nullptr;
	AresVersion = Version::Unknown;
	CanUseAres = false;
	PhobosBaseAddress = 0;
	PhobosDllHmodule = nullptr;
	ScaffoldBaseAddress = 0;
	CanUsePhobos = false;

	std::vector<std::pair<std::string, BYTE*>> syringables;
	std::vector<std::pair<std::string, BYTE*>> others;
	for (const auto& module : ModuleHelper::Enumerate())
	{
		if (IsSunModule(module))
			continue;

		const char* moduleName = module.OriginalFilename.empty()
			? module.ModuleName.c_str()
			: module.OriginalFilename.c_str();
		if (module.IsSyringable)
		{
			if (!_stricmp(moduleName, "Ares.dll"))
			{
				AresBaseAddress = module.BaseAddress;
				AresDllHmodule = module.Handle;
			}
			else if (!_stricmp(moduleName, PHOBOS_DLL))
			{
				PhobosBaseAddress = module.BaseAddress;
				PhobosDllHmodule = module.Handle;
			}
			else if (!_stricmp(moduleName, SCAFFOLD_DLL))
			{
				ScaffoldBaseAddress = module.BaseAddress;
			}
			syringables.emplace_back(moduleName, reinterpret_cast<BYTE*>(module.BaseAddress));
		}
		else
		{
			others.emplace_back(moduleName, reinterpret_cast<BYTE*>(module.BaseAddress));
		}
	}
	CanUsePhobos = PhobosBaseAddress != 0;
#ifndef USING_MULTIFINITE_SYRINGE
	Debug::LogDeferred("Modules presumably injected by Syringe:\n");
	for (auto const& [modName, modBaseAddr] : syringables)
		Debug::LogDeferred("Module %s  base address : 0x%p.\n", modName.c_str(), modBaseAddr);
	Debug::LogDeferred("Other modules:\n");
	for (auto const& [modName, modBaseAddr] : others)
		Debug::LogDeferred("Module %s  base address : 0x%p.\n", modName.c_str(), modBaseAddr);
#endif
}

HMODULE AresHelper::FindModule(const char* moduleName, bool requireSyringable)
{
	if (!moduleName)
		return nullptr;

	ModuleHelper::ModuleInfo module;
	return ModuleHelper::Find(moduleName, module, requireSyringable) ? module.Handle : nullptr;
}

uintptr_t AresHelper::GetModuleBaseAddress(const char* moduleName, bool requireSyringable)
{
	if (!moduleName)
		return 0;

	ModuleHelper::ModuleInfo module;
	return ModuleHelper::Find(moduleName, module, requireSyringable) ? module.BaseAddress : 0;
}

bool AresHelper::IsModuleLoaded(const char* moduleName, bool requireSyringable)
{
	return moduleName && ModuleHelper::IsLoaded(moduleName, requireSyringable);
}

void AresHelper::Init()
{
	GetGameModulesBaseAddresses();
	Debug::LogDeferred(CanUsePhobos
		? "[Scaffold] Detected Phobos.\n"
		: "[Scaffold] Phobos not detected.\n");

	if (!AresBaseAddress)
	{
		Debug::LogDeferred("[Scaffold] Failed to detect Ares. Disabling integration.\n");
		return;
	}
	// find offset of PE header
	const int PEHeaderOffset = *(DWORD*)(AresBaseAddress + 0x3c);
	// find absolute address of PE header
	const DWORD* PEHeaderPtr = (DWORD*)(AresBaseAddress + PEHeaderOffset);
	// read the timedatestamp at 0x8 offset
	const DWORD TimeDateStamp = *(PEHeaderPtr + 2);

	try
	{
		AresVersion = AresTimestampBytes.at(TimeDateStamp);
	}
	catch (std::exception)
	{
		AresVersion = Version::Unknown;
	}
	CanUseAres = AresVersion != Version::Unknown;

	switch (AresVersion)
	{
	case Version::Ares30:
		Debug::LogDeferred("[Scaffold] Detected Ares 3.0.\n");
		AresFunctions::InitAres3_0();
		break;
	case Version::Ares30p:
		Debug::LogDeferred("[Scaffold] Detected Ares 3.0p1.\n");
		AresFunctions::InitAres3_0p1();
		break;
	default:
		Debug::LogDeferred("[Scaffold] Detected a version of Ares that is not supported by Scaffold. Disabling integration.\n");
		AresFunctions::InitNoAres();
		break;
	}
}
