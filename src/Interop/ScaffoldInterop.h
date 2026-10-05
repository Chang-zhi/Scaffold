#pragma once

#include <Interop/InteropApi.h>

// =============================================================================
// ScaffoldInterop - Interop API 提供方的外观（Facade）
//
// 启动阶段调用一次 Init()，之后所有对下列函数指针的使用都必须先经过
// IsAvailable() 判断。
//
// 提供方通常是 Phobos.dll。本 DLL 只有在离开提供方就无法实现的功能上才需要它，
// 例如触发动作 653/654 所使用的 int 范围剧本变量。当提供方不存在时，整个 API
// 保持禁用，调用方回退到原版行为。
//
// 本类刻意保持为薄外观：模块发现放在 InteropModule，指针表放在 InteropApiTable，
// ABI 定义放在 InteropApi.h。
// =============================================================================
class ScaffoldInterop
{
public:
	// 定位提供方、解析函数指针并校验 API 版本。未安装提供方时调用也是安全的。
	// 任一环节失败都会回滚整张表，使 IsAvailable() 返回 false。
	static void Init();

	static bool IsAvailable() { return s_available; }
	static HMODULE GetProviderModule() { return s_hProvider; }

	// 读取提供方对外声明的 API 版本。
	static bool GetVersion(InteropAPIVersion& version);

	// 将提供方的 API 版本与 INTEROP_VERSION_CURRENT 做校验。
	// 主版本号不一致视为致命错误；次版本号/修订号差异仅告警，因为提供方承诺在同一
	// 主版本号内保持向后兼容。
	static bool CheckVersion();

	// 函数指针，由 Init() 填充。当 IsAvailable() 返回 false 时全部为 nullptr，
	// 包括那些解析失败的可选条目。
#define GEN_STATIC_MEMBER(isRequired, member, fnType, exportName) static fnType member;
	FOREACH_INTEROP_FN(GEN_STATIC_MEMBER)
#undef GEN_STATIC_MEMBER

	// AE_Attach 在 Interop API v1.1 / v1.2 之间改过签名, 因此不进上面的功能表,
	// 而是两个版本都解析、由 AE_AttachCompat() 转发到实际存在的那个。
	// [v1.1 兼容] 不再兼容 v1.1 时: 删掉下面的 fnAE_AttachV1 成员, 把
	//   FN(false, AE_Attach, fnAE_Attach, "AE_Attach") \
	// 加回 FOREACH_INTEROP_FN 列表, AE_AttachCompat 里只保留第一个分支。
	static fnAE_Attach AE_Attach;

	// -- [v1.1 兼容] --
	static fnAE_AttachV1 AE_AttachV1;
	// -- [v1.1 兼容] 结束 --

	// 调用提供方的 AE_Attach: 优先 v1.2 的 13 参版本, 退回 v1.1 的 11 参版本。
	// 两个版本都不可用时返回 E_NOTIMPL(调用方的 CheckProvider 会先拦下来)。
	static HRESULT AE_AttachCompat(
		void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
		const char** effectTypeNames, int typeCount,
		int durationOverride, int delay, int initialDelay, int recreationDelay,
		int* pAttachedCount, bool selfOwned, bool hasDelay);

private:
	// 回滚整张表并清除提供方引用。
	static void Reset();

	static bool s_available;
	static HMODULE s_hProvider;
};
