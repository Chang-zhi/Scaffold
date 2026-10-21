#pragma once

#include <Interop/Api/ApiDecl.h>

// =============================================================================
// Interop API v1.0.0：初始版本，AE_Attach 11 参（_AE_Attach@44），无剧本变量。
// =============================================================================
namespace InteropApi::V1_0_0
{
	constexpr InteropAPIVersion Version { 1, 0, 0 };

	// 提供方在 v1.0/v1.1 上的 AE_Attach 签名。
	using fnAE_Attach = HRESULT(__stdcall*)(
		void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
		const char** effectTypeNames, int typeCount,
		int durationOverride, int delay, int initialDelay, int recreationDelay,
		int* pAttachedCount);

	static_assert(InteropStdCallArgBytes<fnAE_Attach>::value == 44,
		"AE_Attach(v1.0/v1.1) 与提供方导出 _AE_Attach@44 不一致");

	// 本版本清单；AE_Attach 签名随版本变化，由 LoadAEAttach 单独处理。
#define FOREACH_INTEROP_FN_V1_0_0(FN) \
	FOREACH_INTEROP_FN_SHARED(FN)

	inline fnAE_Attach g_pfnAE_Attach = nullptr;

	// 旧签名没有末尾两个参数；调用点固定传 false，直接丢弃。
	inline HRESULT __stdcall AdaptAE_Attach(
		void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
		const char** effectTypeNames, int typeCount,
		int durationOverride, int delay, int initialDelay, int recreationDelay,
		int* pAttachedCount, bool /*selfOwned*/, bool /*hasDelay*/)
	{
		return g_pfnAE_Attach(pTarget, pInvokerHouse, pInvoker, pSource, effectTypeNames,
			typeCount, durationOverride, delay, initialDelay, recreationDelay, pAttachedCount);
	}

	// 返回统一 13 参入口；提供方没有该导出时为 nullptr。
	inline InteropApi::fnAE_Attach LoadAEAttach(HMODULE hProvider)
	{
		g_pfnAE_Attach = ResolveInteropExport<fnAE_Attach>(hProvider, "AE_Attach");

		return g_pfnAE_Attach ? &AdaptAE_Attach : nullptr;
	}
}
