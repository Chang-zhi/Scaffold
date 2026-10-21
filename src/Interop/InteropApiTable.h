#pragma once

#include <Interop/Api/ApiVersion.h>

#include <Windows.h>

// =============================================================================
// Interop 功能表 - 按提供方版本加载 / 校验 / 回滚
//
// 版本决定解析哪份清单（Api/V1_a_b_c.h）与哪个 AE_Attach 签名，结果发布到
// ScaffoldInterop 的调用面。Unload() 复位调用面，避免"半加载"被当成可用。
// =============================================================================
namespace InteropApiTable
{
	// 按 provided 选择的版本解析导出，并填充调用面。
	void Load(HMODULE hProvider, const InteropAPIVersion& provided);

	// 复位调用面，使其变为空。
	void Unload();

	// 对每个未能解析的条目输出报告。
	// 当所选版本的必需条目都存在时返回 true。
	bool Verify(HMODULE hProvider);
}
