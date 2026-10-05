#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>

// =============================================================================
// Scaffold Interop - API 定义
//
// 本文件是本 DLL 所消费的 Interop ABI 的唯一事实来源：API 版本、导出函数签名
// 以及用于解析这些导出的功能表。
//
// Interop API 由进程内的另一个模块提供（通常是 Phobos.dll）。它是**可选**依赖：
// 当提供方不存在时，本 DLL 依靠原版回退逻辑继续运行。
// =============================================================================

// -----------------------------------------------------------------------------
// 版本信息（语义化版本 SemVer 2.0.0）
//   主版本号：存在破坏性改动        -> 版本不匹配则禁用整个 API
//   次版本号：新增向后兼容的接口    -> 仅告警，仍然可用
//   修订号　：向后兼容的问题修复    -> 同次版本号
// -----------------------------------------------------------------------------
struct InteropAPIVersion
{
	unsigned int major;
	unsigned int minor;
	unsigned int patch;
};

// 本工程编译时所依据的 API 版本，需与提供方自身的版本常量保持一致
// （Phobos: src/Interop/Version.h -> INTEROP_API_VERSION_*）。
constexpr InteropAPIVersion INTEROP_VERSION_CURRENT = { 1, 2, 0 };

constexpr bool operator==(const InteropAPIVersion& lhs, const InteropAPIVersion& rhs)
{
	return lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.patch == rhs.patch;
}

constexpr bool operator!=(const InteropAPIVersion& lhs, const InteropAPIVersion& rhs)
{
	return !(lhs == rhs);
}

// 提供方承诺在同一主版本号内保持向后兼容，因此只有主版本号不一致才判定不可用。
constexpr bool IsInteropMajorCompatible(const InteropAPIVersion& provided)
{
	return provided.major == INTEROP_VERSION_CURRENT.major;
}

// ============================================================================
// 导出函数签名
//
// 所有游戏对象指针均声明为 void*，由调用方自行转换。
//
// 此处的声明必须与提供方的导出完全一致：在 x86 __stdcall 约定下，修饰后的符号名
// 会编码参数总字节数，因此修改这里的签名会直接改变我们查找的符号名
// （见 InteropDecoratedName）。
// ============================================================================

typedef HRESULT(__stdcall* fnAE_Attach)(
	void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
	const char** effectTypeNames, int typeCount,
	int durationOverride, int delay, int initialDelay, int recreationDelay,
	int* pAttachedCount, bool selfOwned, bool hasDelay
);

typedef HRESULT(__stdcall* fnAE_Detach)(
	void* pTarget, const char** effectTypeNames, int typeCount, int* pRemovedCount
);

typedef HRESULT(__stdcall* fnAE_DetachByGroups)(
	void* pTarget, const char** groupNames, int groupCount, int* pRemovedCount
);

typedef HRESULT(__stdcall* fnAE_TransferEffects)(void* pSource, void* pTarget);

typedef HRESULT(__stdcall* fnConvertToType)(void* pThis, void* pToType);

typedef HRESULT(__stdcall* fnBullet_SetFirerOwner)(void* pBullet, void* pHouse);

typedef double(__stdcall* fnCalculateExtraThreatCallback)(
	void* pThis, void* pTarget, double originalThreat
);

typedef double(__stdcall* fnCalculateSightCallback)(
	void* pThis, double originalSight
);

typedef HRESULT(__stdcall* fnRegisterCalculateExtraThreatCallback)(fnCalculateExtraThreatCallback callback);

typedef HRESULT(__stdcall* fnRegisterCalculateSightCallback)(fnCalculateSightCallback callback);

typedef HRESULT(__stdcall* fnEventExt_AddEvent)(void* pEventExt);

typedef HRESULT(__stdcall* fnVariables_GetLocal)(int index, int* pValue);

typedef HRESULT(__stdcall* fnVariables_SetLocal)(int index, int value);

typedef HRESULT(__stdcall* fnVariables_GetGlobal)(int index, int* pValue);

typedef HRESULT(__stdcall* fnVariables_SetGlobal)(int index, int value);

typedef HRESULT(__stdcall* fnGetInteropAPIVersion)(InteropAPIVersion* pVersion);

// ============================================================================
// Interop 功能表
//
//   FN(是否必需, 成员名, 函数指针类型, 导出名)
//
// 是否必需：
//   true  - 缺了它整个 API 就没有意义；解析失败会禁用 Interop，从而保证调用方
//           通过 IsAvailable() 能可靠地得到 false。
//   false - 可选；解析失败只输出告警。即使当前没有任何调用点，条目也应保留，
//           用作文档与后续扩展。
//
// 请保持必需项与可选项分组排列，以便一眼看出真正的依赖面。每个条目都是独立的
// 查找，因此提供方修改某个可选函数的签名，不会再连带拖垮整个 API。
// ============================================================================
#define FOREACH_INTEROP_FN(FN) \
	FN(true,  Variables_GetLocal,   fnVariables_GetLocal,   "Variables_GetLocal_Phobos") \
	FN(true,  Variables_SetLocal,   fnVariables_SetLocal,   "Variables_SetLocal_Phobos") \
	FN(true,  Variables_GetGlobal,  fnVariables_GetGlobal,  "Variables_GetGlobal_Phobos") \
	FN(true,  Variables_SetGlobal,  fnVariables_SetGlobal,  "Variables_SetGlobal_Phobos") \
	FN(false, AE_Detach,                           fnAE_Detach,                           "AE_Detach") \
	FN(false, AE_DetachByGroups,                   fnAE_DetachByGroups,                   "AE_DetachByGroups") \
	FN(false, AE_TransferEffects,                  fnAE_TransferEffects,                  "AE_TransferEffects") \
	FN(false, ConvertToType,                       fnConvertToType,                       "ConvertToType_Phobos") \
	FN(false, Bullet_SetFirerOwner,                fnBullet_SetFirerOwner,                "Bullet_SetFirerOwner") \
	FN(false, RegisterCalculateExtraThreatCallback, fnRegisterCalculateExtraThreatCallback, "RegisterCalculateExtraThreatCallback") \
	FN(false, RegisterCalculateSightCallback,      fnRegisterCalculateSightCallback,      "RegisterCalculateSightCallback") \
	FN(false, EventExt_AddEvent,                   fnEventExt_AddEvent,                   "EventExt_AddEvent")

// 注意: AE_Attach 不在此列表中 —— 它在 Interop API v1.1(11 参)与 v1.2(13 参)之间
// 改过签名, 由 ScaffoldInterop 单独解析两个版本(见本文件末尾的 [v1.1 兼容] 段,
// 以及 ScaffoldInterop.h 中 AE_Attach / AE_AttachV1 / AE_AttachCompat 的说明)。

// ============================================================================
// 导出名解析
// ============================================================================

// x86 下 __stdcall 的每个参数都占用 4 字节栈槽。
constexpr size_t InteropStdCallSlot(size_t size)
{
	return (size + 3u) & ~size_t { 3u };
}

template <typename T>
struct InteropStdCallArgBytes
{
	static constexpr size_t value = 0;
};

template <typename R, typename... Args>
struct InteropStdCallArgBytes<R(__stdcall*)(Args...)>
{
	static constexpr size_t value = (InteropStdCallSlot(sizeof(Args)) + ... + size_t { 0 });
};

// "AE_Attach" + 52 -> "_AE_Attach@52"（13 个参数 x 4 字节栈槽）
inline std::string InteropDecoratedName(const char* exportName, size_t argBytes)
{
	std::string name;
	name.reserve(64);
	name += '_';
	name += exportName;
	name += '@';
	name += std::to_string(argBytes);

	return name;
}

// 解析单个 Interop 导出，兼容提供方的两种命名方式：
//   1. 未修饰名 —— 提供方通过 .def 文件或别名导出时使用；
//   2. x86 __stdcall 修饰名 "_名字@<参数字节数>" —— 由普通的
//      `extern "C" __declspec(dllexport) ... __stdcall` 导出产生，
//      Phobos 正是以这种方式导出 Interop API。
//
// 修饰名由上面声明的签名推导得出，因此"查找的名字"与"传入的参数"永远不会脱节。
template <typename FnT>
inline FnT ResolveInteropExport(HMODULE hModule, const char* exportName)
{
	if (!hModule || !exportName)
		return nullptr;

	if (auto const pfn = reinterpret_cast<FnT>(::GetProcAddress(hModule, exportName)))
		return pfn;

	constexpr size_t argBytes = InteropStdCallArgBytes<FnT>::value;
	const std::string decorated = InteropDecoratedName(exportName, argBytes);

	return reinterpret_cast<FnT>(::GetProcAddress(hModule, decorated.c_str()));
}

// ============================================================================
// 签名守卫
//
// 提供方(Phobos)以 `extern "C" __declspec(dllexport) ... __stdcall` 导出, 符号名因此
// 编码了参数总字节数。签名一旦漂移, 上面查找的修饰名就会与提供方脱节, 而解析失败只
// 产生运行期告警(见 InteropModule::ReportUnresolvedExport)—— 很容易被忽略。
// 这里用编译期断言把脱节提前变成构建错误。
//
// 断言值与 Phobos 仓库 src/Interop/AttachEffect.h 的导出签名一一对应, 改动提供方
// 签名时必须同步更新。
// ============================================================================
static_assert(InteropStdCallArgBytes<fnAE_Attach>::value == 52,
	"AE_Attach 签名与提供方导出 _AE_Attach@52 不一致");
static_assert(InteropStdCallArgBytes<fnAE_Detach>::value == 16,
	"AE_Detach 签名与提供方导出 _AE_Detach@16 不一致");
static_assert(InteropStdCallArgBytes<fnAE_DetachByGroups>::value == 16,
	"AE_DetachByGroups 签名与提供方导出 _AE_DetachByGroups@16 不一致");
static_assert(InteropStdCallArgBytes<fnAE_TransferEffects>::value == 8,
	"AE_TransferEffects 签名与提供方导出 _AE_TransferEffects@8 不一致");

// ============================================================================
// [v1.1 兼容] Interop API v1.1 的 AE_Attach
//
// v1.1(Phobos v0.5.0.0-alpha1, commit 5a72f20 等)的 AE_Attach 是 11 参版本,
// 修饰名为 _AE_Attach@44; v1.2 在其末尾追加 selfOwned / hasDelay 变成 13 参
// (_AE_Attach@52)。两者的前 11 个参数完全相同, 且调用点固定传 false / false,
// 因此老版本可以直接转发。
//
// 不再需要兼容 v1.1 时: 删除所有标注 [v1.1 兼容] 的代码块(本文件 /
// ScaffoldInterop.h / ScaffoldInterop.cpp / InteropApiTable.cpp /
// AttachEffectService.cpp), 并把
//   FN(false, AE_Attach, fnAE_Attach, "AE_Attach") \
// 加回 ScaffoldInterop.h 中 FOREACH_INTEROP_FN 列表的 AE_Detach 之前。
// ============================================================================
typedef HRESULT(__stdcall* fnAE_AttachV1)(
	void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
	const char** effectTypeNames, int typeCount,
	int durationOverride, int delay, int initialDelay, int recreationDelay,
	int* pAttachedCount
);

static_assert(InteropStdCallArgBytes<fnAE_AttachV1>::value == 44,
	"AE_Attach(v1.1) 签名与提供方导出 _AE_Attach@44 不一致");
