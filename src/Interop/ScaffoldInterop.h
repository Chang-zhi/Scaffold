#pragma once

#include <Interop/Api/ApiSelect.h>

// =============================================================================
// ScaffoldInterop - Interop API 调用面（外观）
//
// Init() 调用一次：找提供方 -> 按它声明的版本加载 -> 平铺函数指针供业务调用，
// 用前先判 IsAvailable()。提供方（通常是 Phobos.dll）属可选依赖，缺失时调用方
// 各自回退到原版行为。
//
// 调用面抹平版本差异：AE_Attach 恒为 13 参，老提供方由版本模块转发。
// 分工：模块发现 InteropModule，版本分派 InteropApiTable，ABI 定义 Api/，封装入口 Services/。
// =============================================================================
class ScaffoldInterop
{
public:
	// 定位提供方、校验版本、按版本解析函数指针；任一环节失败都回滚整张表。
	// 未安装提供方时调用也是安全的。
	static void Init();

	static bool IsAvailable() { return s_available; }
	static HMODULE GetProviderModule() { return s_hProvider; }

	static bool GetVersion(InteropAPIVersion& version);

	// 校验主版本号（不一致即致命），次 / 修订号差异只告警并说明按哪个版本使用。
	static bool CheckVersion(const InteropAPIVersion& provided);

	// 由 Init() 填充；IsAvailable() 为 false 时全部为 nullptr（含解析失败的可选项）。
#define GEN_STATIC_MEMBER(isRequired, member, fnType, exportName) static InteropApi::fnType member;
	FOREACH_INTEROP_FN_SHARED(GEN_STATIC_MEMBER)
	FOREACH_INTEROP_FN_VARIABLES(GEN_STATIC_MEMBER)
#undef GEN_STATIC_MEMBER

	// 统一 13 参；老提供方指向版本模块的转发函数。
	static InteropApi::fnAE_Attach AE_Attach;

private:
	// 回滚整张表并清除提供方引用。
	static void Reset();

	static bool s_available;
	static HMODULE s_hProvider;
};
