#pragma once

#include <Interop/Api/V1_0_0.h>
#include <Interop/Api/V1_1_0.h>
#include <Interop/Api/V1_2_0.h>

// =============================================================================
// 版本选择
//
// 提供方声明 v1.x 即具备 v1.x 的全部导出，因此取"不高于提供方版本的最高已知版本"；
// 提供方更新时同样回落到最高已知版本。
//
// 新增版本模块（如 Api/V1_3_0.h）需同步：本文件的 Level / LevelName / SelectLevel、
// VersionLatest、InteropApiTable 的 Load / Verify 分派。
// =============================================================================
namespace InteropApi
{
	// 本工程已完整支持的版本（新增版本模块时改成该模块的 Version）。
	constexpr InteropAPIVersion VersionLatest = V1_2_0::Version;

	// 命中的 API 版本。
	enum class Level
	{
		V1_0_0,
		V1_1_0,
		V1_2_0,
	};

	constexpr const char* LevelName(Level level)
	{
		switch (level)
		{
		case Level::V1_0_0: return "1.0.0";
		case Level::V1_1_0: return "1.1.0";
		default:            return "1.2.0";
		}
	}

	// 跨主版本时导出的语义可能已变，没有兼容承诺。
	constexpr bool IsMajorCompatible(const InteropAPIVersion& provided)
	{
		return provided.major == V1_0_0::Version.major;
	}

	constexpr bool IsAtLeast(const InteropAPIVersion& provided, const InteropAPIVersion& version)
	{
		if (provided.major != version.major)
			return provided.major > version.major;

		if (provided.minor != version.minor)
			return provided.minor > version.minor;

		return provided.patch >= version.patch;
	}

	constexpr Level SelectLevel(const InteropAPIVersion& provided)
	{
		if (IsAtLeast(provided, V1_2_0::Version))
			return Level::V1_2_0;

		if (IsAtLeast(provided, V1_1_0::Version))
			return Level::V1_1_0;

		return Level::V1_0_0;
	}
}
