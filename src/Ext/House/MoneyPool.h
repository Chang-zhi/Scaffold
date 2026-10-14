#pragma once

#include <HouseClass.h>

#include <map>
#include <string>
#include <vector>

// 资金连锁：多个所属方共用一个资金池。
// 不在资金读写点做拦截，而是每逻辑帧开始时对账——把上一帧各成员的净变化汇总回池，再统一回写。
// 这样原版所有直接读写 `HouseClass + 0x30C`（AvailableMoney）的代码都无需 hook。
class MoneyPoolManager
{
public:
	struct Pool
	{
		std::vector<int> MemberIndices; // 成员在本局 HouseClass::Array 中的索引
		int Base = 0;                   // 上一帧同步后的池值
		bool Initialized = false;       // 是否已完成首次求和建池
	};

	static std::map<std::string, Pool> Pools;

	// 清空全部池（场景切换、清档）
	static void Clear();

	// 把所属方登记进指定池；poolName 为空表示不共享
	static void RegisterHouse(HouseClass* pHouse, const char* poolName);

	// 每逻辑帧调用一次：对账并回写池值
	static void SyncAllPools();

	// 把所属方移出所在池，池余额按成员数均分，其带走自己的一份；未入池返回 false
	static bool RemoveHouse(HouseClass* pHouse);

	// 连结两方的资金连锁：一方已入池则另一方加入该池；两方各在独立池则合并两池；
	// 两方都未入池则新建一池。三种情况下的资金都会相加
	static bool LinkHouses(HouseClass* pHouseA, HouseClass* pHouseB);

	// 读写所属方资金，字段为原版 `HouseClass + 0x30C`
	static int GetMoney(HouseClass* pHouse);
	static void SetMoney(HouseClass* pHouse, int value);
};
