#pragma once

#include <Utilities/SavegameDef.h>

namespace RadarMarkStream
{
	constexpr int MaxRecords = 100000;

	template <typename T>
	bool LoadRecords(ScaffoldStreamReader& Stm, std::vector<T>& records)
	{
		int count = 0;
		if (!Stm.Load(count) || count < 0 || count > MaxRecords)
			return false;

		// 一次性分配最终大小，再登记字段地址，避免 vector 扩容使 Swizzle 地址失效。
		records.clear();
		records.resize(static_cast<size_t>(count));
		for (auto& record : records)
		{
			if (!Stm.Process(record).Success())
				return false;
		}
		return Stm.Success();
	}
}
