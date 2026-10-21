#pragma once

#include <Interop/Api/ApiVersion.h>

#include <Windows.h>

#include <cstddef>
#include <string>

// =============================================================================
// Interop ABI：跨版本稳定的导出签名与导出名解析
//
// 各版本独有的部分（导出清单、AE_Attach 签名）在 Api/V1_a_b_c.h。
// 游戏对象指针一律声明为 void*，由调用方转换。
// =============================================================================
namespace InteropApi
{
	// AE_Attach 是唯一改过签名的导出（v1.0/v1.1 为 11 参，v1.2 起 13 参）。
	// 这里统一按 13 参声明，旧提供方由 Api/V1_0_0.h 的转发函数补齐。
	using fnAE_Attach = HRESULT(__stdcall*)(
		void* pTarget, void* pInvokerHouse, void* pInvoker, void* pSource,
		const char** effectTypeNames, int typeCount,
		int durationOverride, int delay, int initialDelay, int recreationDelay,
		int* pAttachedCount, bool selfOwned, bool hasDelay);

	// 自 v1.0 起未改动过的导出
	using fnAE_Detach = HRESULT(__stdcall*)(
		void* pTarget, const char** effectTypeNames, int typeCount, int* pRemovedCount);

	using fnAE_DetachByGroups = HRESULT(__stdcall*)(
		void* pTarget, const char** groupNames, int groupCount, int* pRemovedCount);

	using fnAE_TransferEffects = HRESULT(__stdcall*)(void* pSource, void* pTarget);

	using fnConvertToType = HRESULT(__stdcall*)(void* pThis, void* pToType);

	using fnBullet_SetFirerOwner = HRESULT(__stdcall*)(void* pBullet, void* pHouse);

	using fnCalculateExtraThreatCallback = double(__stdcall*)(
		void* pThis, void* pTarget, double originalThreat);

	using fnCalculateSightCallback = double(__stdcall*)(void* pThis, double originalSight);

	using fnRegisterCalculateExtraThreatCallback = HRESULT(__stdcall*)(
		fnCalculateExtraThreatCallback callback);

	using fnRegisterCalculateSightCallback = HRESULT(__stdcall*)(
		fnCalculateSightCallback callback);

	using fnEventExt_AddEvent = HRESULT(__stdcall*)(void* pEventExt);

	// 剧本变量（v1.1 起）
	using fnVariables_GetLocal = HRESULT(__stdcall*)(int index, int* pValue);
	using fnVariables_SetLocal = HRESULT(__stdcall*)(int index, int value);
	using fnVariables_GetGlobal = HRESULT(__stdcall*)(int index, int* pValue);
	using fnVariables_SetGlobal = HRESULT(__stdcall*)(int index, int value);

	using fnGetInteropAPIVersion = HRESULT(__stdcall*)(InteropAPIVersion* pVersion);

	// ---- 导出名解析 ----

	// x86 下 __stdcall 的每个参数都占 4 字节栈槽。
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

	// 签名守卫：期望值即提供方 DEFINE_EXPORT 的参数总字节数（也就是修饰名里的数字）。
	// 提供方改签名时必须同步，否则解析失败只剩一条运行期告警。
#define INTEROP_ASSERT_SIGNATURE(fnType, argBytes) \
	static_assert(InteropStdCallArgBytes<fnType>::value == argBytes, \
		#fnType " 的签名与提供方导出不一致")

	INTEROP_ASSERT_SIGNATURE(fnAE_Detach, 16);
	INTEROP_ASSERT_SIGNATURE(fnAE_DetachByGroups, 16);
	INTEROP_ASSERT_SIGNATURE(fnAE_TransferEffects, 8);
	INTEROP_ASSERT_SIGNATURE(fnConvertToType, 8);
	INTEROP_ASSERT_SIGNATURE(fnBullet_SetFirerOwner, 8);
	INTEROP_ASSERT_SIGNATURE(fnRegisterCalculateExtraThreatCallback, 4);
	INTEROP_ASSERT_SIGNATURE(fnRegisterCalculateSightCallback, 4);
	INTEROP_ASSERT_SIGNATURE(fnEventExt_AddEvent, 4);
	INTEROP_ASSERT_SIGNATURE(fnVariables_GetLocal, 8);
	INTEROP_ASSERT_SIGNATURE(fnVariables_SetLocal, 8);
	INTEROP_ASSERT_SIGNATURE(fnVariables_GetGlobal, 8);
	INTEROP_ASSERT_SIGNATURE(fnVariables_SetGlobal, 8);
	INTEROP_ASSERT_SIGNATURE(fnGetInteropAPIVersion, 4);

#undef INTEROP_ASSERT_SIGNATURE

	// "AE_Attach" + 52 -> "_AE_Attach@52"
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

	// 先查未修饰名（.def / 别名导出），再查 __stdcall 修饰名 "_名字@<参数字节数>"
	// （普通 dllexport 的产物）。修饰名由签名推导，查找名与调用签名不会脱节。
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

	// 导出清单：FN(是否必需, 成员名, 函数指针类型, 导出名)
	// 缺必需项禁用整个 API，可选项只告警。两个清单是字段并集，
	// 版本是否真的提供某项由 Api/V1_a_b_c.h 的清单决定（v1.0 没有剧本变量）。
#define FOREACH_INTEROP_FN_SHARED(FN) \
	FN(false, AE_Detach,                            fnAE_Detach,                            "AE_Detach") \
	FN(false, AE_DetachByGroups,                    fnAE_DetachByGroups,                    "AE_DetachByGroups") \
	FN(false, AE_TransferEffects,                   fnAE_TransferEffects,                   "AE_TransferEffects") \
	FN(false, ConvertToType,                        fnConvertToType,                        "ConvertToType_Phobos") \
	FN(false, Bullet_SetFirerOwner,                 fnBullet_SetFirerOwner,                 "Bullet_SetFirerOwner") \
	FN(false, RegisterCalculateExtraThreatCallback, fnRegisterCalculateExtraThreatCallback, "RegisterCalculateExtraThreatCallback") \
	FN(false, RegisterCalculateSightCallback,       fnRegisterCalculateSightCallback,       "RegisterCalculateSightCallback") \
	FN(false, EventExt_AddEvent,                    fnEventExt_AddEvent,                    "EventExt_AddEvent")

#define FOREACH_INTEROP_FN_VARIABLES(FN) \
	FN(true,  Variables_GetLocal,  fnVariables_GetLocal,  "Variables_GetLocal_Phobos") \
	FN(true,  Variables_SetLocal,  fnVariables_SetLocal,  "Variables_SetLocal_Phobos") \
	FN(true,  Variables_GetGlobal, fnVariables_GetGlobal, "Variables_GetGlobal_Phobos") \
	FN(true,  Variables_SetGlobal, fnVariables_SetGlobal, "Variables_SetGlobal_Phobos")

	// 已解析的函数指针；该版本不提供的条目保持 nullptr。
	struct Functions
	{
#define GEN_INTEROP_FIELD(isRequired, member, fnType, exportName) fnType member = nullptr;
		FOREACH_INTEROP_FN_SHARED(GEN_INTEROP_FIELD)
		FOREACH_INTEROP_FN_VARIABLES(GEN_INTEROP_FIELD)
#undef GEN_INTEROP_FIELD

		// 统一 13 参调用面；提供方为 v1.0/v1.1 时指向版本模块的转发函数。
		fnAE_Attach AE_Attach = nullptr;
	};
}
