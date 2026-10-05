#include "InteropApiTable.h"

#include "InteropApi.h"
#include "InteropModule.h"
#include "ScaffoldInterop.h"

#include <Utilities/Debug.h>

// =============================================================================
// 功能表只在 ScaffoldInterop.h 中声明一次，在这里展开实现。让"成员变量"和
// "查找逻辑"由同一份 FOREACH_INTEROP_FN 列表驱动，正是防止两者脱节的关键。
// =============================================================================

void InteropApiTable::Load(HMODULE hProvider)
{
#define GEN_LOAD(isRequired, member, fnType, exportName) \
	ScaffoldInterop::member = ResolveInteropExport<fnType>(hProvider, exportName);

	FOREACH_INTEROP_FN(GEN_LOAD)

#undef GEN_LOAD

	// AE_Attach 有两个签名版本, 两个都试着解析(见 ScaffoldInterop.h 的说明)。
	ScaffoldInterop::AE_Attach = ResolveInteropExport<fnAE_Attach>(hProvider, "AE_Attach");
	// -- [v1.1 兼容] --
	ScaffoldInterop::AE_AttachV1 = ResolveInteropExport<fnAE_AttachV1>(hProvider, "AE_Attach");
	// -- [v1.1 兼容] 结束 --
}

void InteropApiTable::Unload()
{
#define GEN_UNLOAD(isRequired, member, fnType, exportName) \
	ScaffoldInterop::member = nullptr;

	FOREACH_INTEROP_FN(GEN_UNLOAD)

#undef GEN_UNLOAD

	ScaffoldInterop::AE_Attach = nullptr;
	// -- [v1.1 兼容] --
	ScaffoldInterop::AE_AttachV1 = nullptr;
	// -- [v1.1 兼容] 结束 --
}

bool InteropApiTable::Verify(HMODULE hProvider)
{
	bool allRequiredPresent = true;

#define GEN_VERIFY(isRequired, member, fnType, exportName) \
	if (!ScaffoldInterop::member) \
	{ \
		if (isRequired) \
			allRequiredPresent = false; \
		\
		InteropModule::ReportUnresolvedExport(hProvider, exportName, isRequired); \
	}

	FOREACH_INTEROP_FN(GEN_VERIFY)

#undef GEN_VERIFY

	// AE_Attach: 两个签名版本任一可用即视为就位。
	if (!ScaffoldInterop::AE_Attach)
	{
		// -- [v1.1 兼容] --
		// 老提供方只导出 11 参版本, 这是预期情况, 说明一句而不是当成签名漂移报警。
		if (ScaffoldInterop::AE_AttachV1)
		{
			Debug::Log("[ScaffoldInterop] AE_Attach: provider exports the legacy 11-arg "
				"signature (Interop API v1.1); using it\n");

			return allRequiredPresent;
		}
		// -- [v1.1 兼容] 结束 --

		InteropModule::ReportUnresolvedExport(hProvider, "AE_Attach", false);
	}

	return allRequiredPresent;
}
