#include "ScaffoldInterop.h"

#include "InteropApiTable.h"
#include "InteropModule.h"

#include <Utilities/Debug.h>

// ============================================================================
// 静态成员初始化
// ============================================================================

bool ScaffoldInterop::s_available = false;
HMODULE ScaffoldInterop::s_hProvider = nullptr;

#define GEN_STATIC_INIT(isRequired, member, fnType, exportName) InteropApi::fnType ScaffoldInterop::member = nullptr;
FOREACH_INTEROP_FN_SHARED(GEN_STATIC_INIT)
FOREACH_INTEROP_FN_VARIABLES(GEN_STATIC_INIT)
#undef GEN_STATIC_INIT

InteropApi::fnAE_Attach ScaffoldInterop::AE_Attach = nullptr;

// ============================================================================
// Init
// ============================================================================

void ScaffoldInterop::Init()
{
	Debug::Log("[Interop] Init: searching for an Interop API provider\n");

	bool conflict = false;
	s_hProvider = InteropModule::FindProvider(conflict);

	if (conflict)
	{
		// 冲突的双方已由 FindProvider 记录。
		Reset();
		Debug::Log("[Interop] [Error]: Interop API disabled\n");
		return;
	}

	if (!s_hProvider)
	{
		// 可选依赖，缺失不算错误：调用方各自回退到原版行为。
		s_available = false;
		InteropApiTable::Unload();
		Debug::Log("[Interop] No available provider found, Interop API disabled\n");
		return;
	}

	// 版本先于加载确定：它决定解析哪份清单、用哪个 AE_Attach 签名。
	InteropAPIVersion provided {};

	if (!GetVersion(provided))
	{
		Debug::Log("[Interop] [Error]: provider does not expose GetInteropAPIVersion\n");
		Reset();
		return;
	}

	if (!CheckVersion(provided))
	{
		Reset();
		return;
	}

	InteropApiTable::Load(s_hProvider, provided);

	if (!InteropApiTable::Verify(s_hProvider))
	{
		Reset();
		Debug::Log("[Interop] [Error]: required exports missing, Interop API disabled\n");
		return;
	}

	s_available = true;

	Debug::Log("[Interop] Loaded (provider API v%u.%u.%u, using ABI v%s; built for v%u.%u.%u)\n",
		provided.major, provided.minor, provided.patch,
		InteropApi::LevelName(InteropApi::SelectLevel(provided)),
		InteropApi::VersionLatest.major, InteropApi::VersionLatest.minor, InteropApi::VersionLatest.patch);
}

void ScaffoldInterop::Reset()
{
	InteropApiTable::Unload();
	s_hProvider = nullptr;
	s_available = false;
}

// ============================================================================
// GetVersion / CheckVersion
// ============================================================================

bool ScaffoldInterop::GetVersion(InteropAPIVersion& version)
{
	if (!s_hProvider)
		return false;

	auto const pfn = InteropApi::ResolveInteropExport<InteropApi::fnGetInteropAPIVersion>(
		s_hProvider, "GetInteropAPIVersion");

	return pfn && SUCCEEDED(pfn(&version));
}

bool ScaffoldInterop::CheckVersion(const InteropAPIVersion& provided)
{
	if (!InteropApi::IsMajorCompatible(provided))
	{
		Debug::Log("[Interop] [Error]: incompatible Interop API major version "
			"(supported v%u.x.x, provider is v%u.%u.%u)\n",
			InteropApi::VersionLatest.major,
			provided.major, provided.minor, provided.patch);

		return false;
	}

	if (IsVersionOlder(provided, InteropApi::VersionLatest))
	{
		// 老提供方按自己声明的版本解析，清单里没有的导出不算缺失。
		Debug::Log("[Interop] [Warning]: provider API v%u.%u.%u is older than v%u.%u.%u, "
			"using ABI v%s\n",
			provided.major, provided.minor, provided.patch,
			InteropApi::VersionLatest.major, InteropApi::VersionLatest.minor, InteropApi::VersionLatest.patch,
			InteropApi::LevelName(InteropApi::SelectLevel(provided)));
	}
	else if (IsVersionOlder(InteropApi::VersionLatest, provided))
	{
		// 同主版本内提供方承诺向后兼容，按最高已知版本使用。
		Debug::Log("[Interop] [Warning]: provider API v%u.%u.%u is newer than v%u.%u.%u, "
			"using ABI v%s\n",
			provided.major, provided.minor, provided.patch,
			InteropApi::VersionLatest.major, InteropApi::VersionLatest.minor, InteropApi::VersionLatest.patch,
			InteropApi::LevelName(InteropApi::SelectLevel(provided)));
	}

	return true;
}
