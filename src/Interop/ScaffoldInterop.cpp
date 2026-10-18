#include "ScaffoldInterop.h"

#include "InteropApiTable.h"
#include "InteropModule.h"

#include <Utilities/Debug.h>

// ============================================================================
// 静态成员初始化
// ============================================================================

bool ScaffoldInterop::s_available = false;
HMODULE ScaffoldInterop::s_hProvider = nullptr;

#define GEN_STATIC_INIT(isRequired, member, fnType, exportName) fnType ScaffoldInterop::member = nullptr;
FOREACH_INTEROP_FN(GEN_STATIC_INIT)
#undef GEN_STATIC_INIT

// AE_Attach 不经过 FOREACH 列表(见 ScaffoldInterop.h 的说明)。
fnAE_Attach ScaffoldInterop::AE_Attach = nullptr;
// -- [v1.1 兼容] --
fnAE_AttachV1 ScaffoldInterop::AE_AttachV1 = nullptr;
// -- [v1.1 兼容] 结束 --

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
		// 这不是错误。提供方属于可选依赖，缺失时所有调用方都会回退到原版行为。
		s_available = false;
		InteropApiTable::Unload();
		Debug::Log("[Interop] No available provider found, Interop API disabled\n");
		return;
	}

	InteropApiTable::Load(s_hProvider);

	if (!InteropApiTable::Verify(s_hProvider))
	{
		Reset();
		Debug::Log("[Interop] [Error]: required exports missing, Interop API disabled\n");
		return;
	}

	s_available = true;

	// 版本闸门是加载契约的一部分，而不是可选项：它正是把"静默的 ABI 不匹配"
	// 变成一条日志的关键。
	if (!CheckVersion())
	{
		Reset();
		return;
	}

	InteropAPIVersion provided {};

	if (GetVersion(provided))
	{
		Debug::Log("[Interop] Loaded (provider API v%u.%u.%u, built for v%u.%u.%u)\n",
			provided.major, provided.minor, provided.patch,
			INTEROP_VERSION_CURRENT.major, INTEROP_VERSION_CURRENT.minor, INTEROP_VERSION_CURRENT.patch);
	}
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

	auto const pfn = ResolveInteropExport<fnGetInteropAPIVersion>(s_hProvider, "GetInteropAPIVersion");

	return pfn && SUCCEEDED(pfn(&version));
}

bool ScaffoldInterop::CheckVersion()
{
	InteropAPIVersion provided {};

	if (!GetVersion(provided))
	{
		Debug::Log("[Interop] [Error]: provider does not expose GetInteropAPIVersion\n");
		return false;
	}

	if (!IsInteropMajorCompatible(provided))
	{
		Debug::Log("[Interop] [Error]: incompatible Interop API major version "
			"(built for v%u.x.x, provider is v%u.%u.%u)\n",
			INTEROP_VERSION_CURRENT.major,
			provided.major, provided.minor, provided.patch);

		return false;
	}

	if (provided != INTEROP_VERSION_CURRENT)
	{
		Debug::Log("[Interop] [Warning]: Interop API version differs "
			"(built for v%u.%u.%u, provider is v%u.%u.%u); continuing\n",
			INTEROP_VERSION_CURRENT.major, INTEROP_VERSION_CURRENT.minor, INTEROP_VERSION_CURRENT.patch,
			provided.major, provided.minor, provided.patch);
	}

	return true;
}

// ============================================================================
// AE_Attach 转发(v1.2 的 13 参 / [v1.1 兼容] v1.1 的 11 参)
// ============================================================================

HRESULT ScaffoldInterop::AE_AttachCompat(void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
	const char** effectTypeNames, int typeCount, int durationOverride, int delay, int initialDelay,
	int recreationDelay, int* pAttachedCount, bool selfOwned, bool hasDelay)
{
	if (AE_Attach)
	{
		return AE_Attach(pTarget, pInvokerHouse, pInvoker, pSource, effectTypeNames, typeCount,
			durationOverride, delay, initialDelay, recreationDelay, pAttachedCount, selfOwned, hasDelay);
	}

	// -- [v1.1 兼容] --
	// 老提供方没有 selfOwned / hasDelay: 调用点固定传 false / false, 行为一致。
	if (AE_AttachV1)
	{
		return AE_AttachV1(pTarget, pInvokerHouse, pInvoker, pSource, effectTypeNames, typeCount,
			durationOverride, delay, initialDelay, recreationDelay, pAttachedCount);
	}
	// -- [v1.1 兼容] 结束 --

	return E_NOTIMPL;
}
