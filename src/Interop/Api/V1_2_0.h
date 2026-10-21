#pragma once

#include <Interop/Api/V1_1_0.h>

// =============================================================================
// Interop API v1.2.0：AE_Attach 追加 selfOwned / hasDelay（13 参，_AE_Attach@52），
// 导出集合同 v1.1.0。
// =============================================================================
namespace InteropApi::V1_2_0
{
	constexpr InteropAPIVersion Version { 1, 2, 0 };

	// 该签名即统一调用面，无需转发。
	using fnAE_Attach = InteropApi::fnAE_Attach;

	static_assert(InteropStdCallArgBytes<fnAE_Attach>::value == 52,
		"AE_Attach(v1.2) 与提供方导出 _AE_Attach@52 不一致");

#define FOREACH_INTEROP_FN_V1_2_0(FN) \
	FOREACH_INTEROP_FN_V1_1_0(FN)

	inline fnAE_Attach LoadAEAttach(HMODULE hProvider)
	{
		return ResolveInteropExport<fnAE_Attach>(hProvider, "AE_Attach");
	}
}
