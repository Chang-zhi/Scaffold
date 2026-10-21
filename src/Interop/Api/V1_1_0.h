#pragma once

#include <Interop/Api/V1_0_0.h>

// =============================================================================
// Interop API v1.1.0：新增剧本变量读写，AE_Attach 沿用 v1.0.0 的 11 参 ABI。
// =============================================================================
namespace InteropApi::V1_1_0
{
	constexpr InteropAPIVersion Version { 1, 1, 0 };

	using fnAE_Attach = V1_0_0::fnAE_Attach;

	// v1.0.0 清单 + 剧本变量（本版本声明的能力，缺失即提供方异常）。
#define FOREACH_INTEROP_FN_V1_1_0(FN) \
	FOREACH_INTEROP_FN_V1_0_0(FN) \
	FOREACH_INTEROP_FN_VARIABLES(FN)

	inline InteropApi::fnAE_Attach LoadAEAttach(HMODULE hProvider)
	{
		return V1_0_0::LoadAEAttach(hProvider);
	}
}
