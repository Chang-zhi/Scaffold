#pragma once

// =============================================================================
// Interop API 版本号（布局须与提供方 GetInteropAPIVersion 一致，不得改动）
//
// 主版本号不同 -> 整个 API 禁用；次 / 修订号差异 -> 按不高于提供方的最高已知版本使用。
// 各版本导出了什么见 Api/V1_a_b_c.h，选择规则见 Api/ApiSelect.h。
// =============================================================================
struct InteropAPIVersion
{
	unsigned int major;
	unsigned int minor;
	unsigned int patch;
};

constexpr bool operator==(const InteropAPIVersion& lhs, const InteropAPIVersion& rhs)
{
	return lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.patch == rhs.patch;
}

constexpr bool operator!=(const InteropAPIVersion& lhs, const InteropAPIVersion& rhs)
{
	return !(lhs == rhs);
}

constexpr bool IsVersionOlder(const InteropAPIVersion& lhs, const InteropAPIVersion& rhs)
{
	if (lhs.major != rhs.major)
		return lhs.major < rhs.major;

	if (lhs.minor != rhs.minor)
		return lhs.minor < rhs.minor;

	return lhs.patch < rhs.patch;
}
