#include "MoneyPool.h"

#include <Utilities/Debug.h>

#include <algorithm>
#include <climits>

// `AvailableMoney` 成员偏移。`GiveMoney`(0x4F9950) 只做 `house->Money += n`；
// `TakeMoney`(0x4F9790) 先扣此处、不足再从本所属方矿石仓提取。
static constexpr int HouseMoneyOffset = 0x30C;

std::map<std::string, MoneyPoolManager::Pool> MoneyPoolManager::Pools;

int MoneyPoolManager::GetMoney(HouseClass* pHouse)
{
	return *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(pHouse) + HouseMoneyOffset);
}

void MoneyPoolManager::SetMoney(HouseClass* pHouse, int value)
{
	*reinterpret_cast<int*>(reinterpret_cast<BYTE*>(pHouse) + HouseMoneyOffset) = value;
}

void MoneyPoolManager::Clear()
{
	Pools.clear();
}

namespace
{
	// 所属方在 Array 中的索引；ArrayIndex 不可靠时退化为线性查找，找不到返回 -1
	int FindHouseIndex(HouseClass* pHouse)
	{
		const auto& arr = HouseClass::Array;

		if (pHouse->ArrayIndex >= 0 && pHouse->ArrayIndex < arr.Count
			&& arr[pHouse->ArrayIndex] == pHouse)
		{
			return pHouse->ArrayIndex;
		}

		for (int i = 0; i < arr.Count; ++i)
		{
			if (arr[i] == pHouse)
				return i;
		}

		return -1;
	}

	// 所属方所在池的名字，未入池返回空串
	std::string FindPoolName(int index)
	{
		for (const auto& [name, pool] : MoneyPoolManager::Pools)
		{
			const auto& members = pool.MemberIndices;
			if (std::find(members.begin(), members.end(), index) != members.end())
				return name;
		}

		return std::string();
	}

	// 单池对账：把各成员相对上帧的净变化汇总回池，再统一回写
	void SyncOnePool(const std::string& name, MoneyPoolManager::Pool& pool)
	{
		// 只统计仍然有效的成员；所属方被移除时其索引自然失效
		long long sum = 0;
		int count = 0;

		for (const int index : pool.MemberIndices)
		{
			if (index < 0 || index >= HouseClass::Array.Count)
				continue;

			if (auto* pHouse = HouseClass::Array[index])
			{
				sum += MoneyPoolManager::GetMoney(pHouse);
				++count;
			}
		}

		if (count <= 0)
			return;

		if (!pool.Initialized)
		{
			// 首次建池：按各成员初始资金求和（规则 D）
			pool.Base = (sum > INT_MAX) ? INT_MAX : (sum < 0 ? 0 : static_cast<int>(sum));
			pool.Initialized = true;

			Debug::Log("[MoneyPool] Pool \"%s\" initialized: %d members, base=%d\n",
				name.c_str(), count, pool.Base);
		}
		else
		{
			// 新池值 = 旧池值 + 各成员相对旧值的净变化 = 总和 - 旧值 * (成员数 - 1)
			long long newBalance = sum - static_cast<long long>(pool.Base) * (count - 1);

			if (newBalance < 0)
				newBalance = 0;
			else if (newBalance > INT_MAX)
				newBalance = INT_MAX;

			pool.Base = static_cast<int>(newBalance);
		}

		for (const int index : pool.MemberIndices)
		{
			if (index < 0 || index >= HouseClass::Array.Count)
				continue;

			if (auto* pHouse = HouseClass::Array[index])
				MoneyPoolManager::SetMoney(pHouse, pool.Base);
		}
	}

	// 回写全部成员为池值
	void WriteBackPool(const MoneyPoolManager::Pool& pool)
	{
		for (const int index : pool.MemberIndices)
		{
			if (index < 0 || index >= HouseClass::Array.Count)
				continue;

			if (auto* pHouse = HouseClass::Array[index])
				MoneyPoolManager::SetMoney(pHouse, pool.Base);
		}
	}

	// 把未入池的所属方并入池：其当前资金计入池值，再回写全部成员
	void AbsorbIntoPool(const std::string& name, MoneyPoolManager::Pool& pool, int newIndex, HouseClass* newHouse)
	{
		SyncOnePool(name, pool);

		long long merged = static_cast<long long>(pool.Base) + MoneyPoolManager::GetMoney(newHouse);

		if (merged > INT_MAX)
			merged = INT_MAX;
		else if (merged < 0)
			merged = 0;

		pool.Base = static_cast<int>(merged);

		if (std::find(pool.MemberIndices.begin(), pool.MemberIndices.end(), newIndex) == pool.MemberIndices.end())
			pool.MemberIndices.push_back(newIndex);

		WriteBackPool(pool);

		Debug::Log("[MoneyPool] House \"%s\" absorbed into pool \"%s\", base=%d\n",
			newHouse->PlainName, name.c_str(), pool.Base);
	}

	// 把 src 池并入 dst 池，两池余额相加
	void MergePoolsInto(const std::string& src, const std::string& dst)
	{
		auto itSrc = MoneyPoolManager::Pools.find(src);
		auto itDst = MoneyPoolManager::Pools.find(dst);

		if (itSrc == MoneyPoolManager::Pools.end() || itDst == MoneyPoolManager::Pools.end() || itSrc == itDst)
			return;

		SyncOnePool(itSrc->first, itSrc->second);
		SyncOnePool(itDst->first, itDst->second);

		long long merged = static_cast<long long>(itDst->second.Base) + itSrc->second.Base;

		if (merged > INT_MAX)
			merged = INT_MAX;

		itDst->second.Base = static_cast<int>(merged);

		// 拷贝一份成员再合并，erase 之后不能再引用源池
		const std::vector<int> srcMembers = itSrc->second.MemberIndices;
		for (const int index : srcMembers)
		{
			if (std::find(itDst->second.MemberIndices.begin(), itDst->second.MemberIndices.end(), index)
				== itDst->second.MemberIndices.end())
			{
				itDst->second.MemberIndices.push_back(index);
			}
		}

		WriteBackPool(itDst->second);

		Debug::Log("[MoneyPool] Pool \"%s\" merged into \"%s\", base=%d\n",
			src.c_str(), dst.c_str(), itDst->second.Base);

		MoneyPoolManager::Pools.erase(itSrc);
	}
}

void MoneyPoolManager::RegisterHouse(HouseClass* pHouse, const char* poolName)
{
	if (!pHouse || !poolName || !*poolName)
		return;

	const int index = FindHouseIndex(pHouse);
	if (index < 0)
	{
		Debug::Log("[MoneyPool] House %s not in Array, skipped.\n", pHouse->PlainName);
		return;
	}

	auto& members = Pools[poolName].MemberIndices;
	if (std::find(members.begin(), members.end(), index) == members.end())
	{
		members.push_back(index);
		Debug::Log("[MoneyPool] House \"%s\" joined pool \"%s\" (index %d, size %d).\n",
			pHouse->PlainName, poolName, index, static_cast<int>(members.size()));
	}
}

void MoneyPoolManager::SyncAllPools()
{
	static bool firstTickLogged = false;
	if (!firstTickLogged)
	{
		firstTickLogged = true;
		Debug::Log("[MoneyPool] First sync tick, pools=%d\n", static_cast<int>(Pools.size()));
	}

	for (auto& [name, pool] : Pools)
		SyncOnePool(name, pool);
}

bool MoneyPoolManager::RemoveHouse(HouseClass* pHouse)
{
	if (!pHouse)
		return false;

	const int index = FindHouseIndex(pHouse);
	if (index < 0)
		return false;

	for (auto it = Pools.begin(); it != Pools.end(); ++it)
	{
		auto& pool = it->second;
		auto& members = pool.MemberIndices;
		const auto m = std::find(members.begin(), members.end(), index);
		if (m == members.end())
			continue;

		// 先把该所属方这一帧的变化归入池
		SyncOnePool(it->first, pool);

		int count = 0;
		for (const int idx : members)
		{
			if (idx >= 0 && idx < HouseClass::Array.Count && HouseClass::Array[idx])
				++count;
		}

		if (count <= 0)
			return false;

		// 资金均分：离开者带走自己的一份，剩余成员继续共享池的其余部分
		const int share = pool.Base / count;
		pool.Base -= share;

		members.erase(m);
		SetMoney(pHouse, share);
		WriteBackPool(pool);

		Debug::Log("[MoneyPool] House \"%s\" left pool \"%s\" with %d (pool left %d).\n",
			pHouse->PlainName, it->first.c_str(), share, pool.Base);

		if (members.empty())
			Pools.erase(it);

		return true;
	}

	return false;
}

bool MoneyPoolManager::LinkHouses(HouseClass* pHouseA, HouseClass* pHouseB)
{
	if (!pHouseA || !pHouseB || pHouseA == pHouseB)
		return false;

	const int indexA = FindHouseIndex(pHouseA);
	const int indexB = FindHouseIndex(pHouseB);

	if (indexA < 0 || indexB < 0)
		return false;

	const std::string poolA = FindPoolName(indexA);
	const std::string poolB = FindPoolName(indexB);

	// 双方都未入池：新建池，两方资金相加
	if (poolA.empty() && poolB.empty())
	{
		const std::string name = "#" + std::to_string(indexA) + "+" + std::to_string(indexB);
		auto& pool = Pools[name];

		pool.MemberIndices.push_back(indexA);
		pool.MemberIndices.push_back(indexB);

		long long merged = static_cast<long long>(GetMoney(pHouseA)) + GetMoney(pHouseB);

		if (merged > INT_MAX)
			merged = INT_MAX;
		else if (merged < 0)
			merged = 0;

		// 已在此处完成相加，标记为已建池，避免下一帧再求和一次
		pool.Base = static_cast<int>(merged);
		pool.Initialized = true;

		WriteBackPool(pool);

		Debug::Log("[MoneyPool] New pool \"%s\" created from \"%s\" and \"%s\", base=%d\n",
			name.c_str(), pHouseA->PlainName, pHouseB->PlainName, pool.Base);

		return true;
	}

	// 已在同一池：不动
	if (!poolA.empty() && poolA == poolB)
		return true;

	// 一方未入池：把它的资金并入另一方的池
	if (poolA.empty())
	{
		auto& pool = Pools[poolB];
		AbsorbIntoPool(poolB, pool, indexA, pHouseA);
		return true;
	}

	if (poolB.empty())
	{
		auto& pool = Pools[poolA];
		AbsorbIntoPool(poolA, pool, indexB, pHouseB);
		return true;
	}

	// 两个独立池：打通，资金相加
	MergePoolsInto(poolB, poolA);
	return true;
}
