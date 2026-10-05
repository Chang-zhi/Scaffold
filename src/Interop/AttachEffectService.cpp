#include "AttachEffectService.h"

#include <Interop/ScaffoldInterop.h>
#include <New/AttachEffect/AttachEffectTypeClass.h>

#include <climits>
#include <vector>

#include <FootClass.h>
#include <TechnoClass.h>
#include <TeamClass.h>
#include <HouseClass.h>

#include <Utilities/Debug.h>

namespace
{
	constexpr int recreationDelay_NoOverride = INT_MIN;

	bool IsEligibleMember(TechnoClass* pTechno)
	{
		return pTechno
			&& pTechno->IsAlive
			&& pTechno->Health > 0
			&& pTechno->IsOnMap
			&& !pTechno->InLimbo
			&& !pTechno->Transporter;
	}

	// 移除时的筛选: 不看 IsOnMap / 载具, 已加入的实例都要清掉,
	// 否则单位重新出现时会带着残留效果。
	bool IsRemovableMember(TechnoClass* pTechno)
	{
		return pTechno && pTechno->IsAlive;
	}

	// 动作参数以 int 暴露给脚本, 只有取用时才做检查。
	void LogBadKey(int key)
	{
		Debug::Log("[Scaffold] AttachEffectService: no [AttachEffectTypes] entry with key %d\n", key);
	}

	// "动作跑了却没效果"的三类原因只从日志里看得出来, 所以逐条说明:
	// 提供方没装 / 装了但这个导出没解析出来 / 导出存在但调用失败。
	template <typename Fn>
	bool CheckProvider(const char* what, Fn pExport)
	{
		if (!ScaffoldInterop::IsAvailable())
		{
			Debug::Log("[AE] %s: Interop provider unavailable; is Phobos v0.5-alpha+ installed?\n", what);
			return false;
		}

		if (!pExport)
		{
			Debug::Log("[AE] %s: provider does not export this entry point\n", what);
			return false;
		}

		return true;
	}

	// 单位 ID 是排查时唯一能跟游戏里对上号的东西, 跳过原因也一并写出来。
	void LogSkipped(const char* what, TechnoClass* pTechno, const char* why)
	{
		Debug::Log("[AE] %s: unit [%s] skipped (%s)\n", what, pTechno ? pTechno->get_ID() : "<null>", why);
	}

	// AE_Attach 有 v1.2(13 参) 与 v1.1(11 参) 两个版本, 任一解析成功即可用。
	bool HasAttachEntry()
	{
		// -- [v1.1 兼容] --
		if (ScaffoldInterop::AE_AttachV1)
			return true;
		// -- [v1.1 兼容] 结束 --

		return ScaffoldInterop::AE_Attach != nullptr;
	}

	int ApplyToOne(TechnoClass* pUnit, HouseClass* pOwner, AbstractClass* pSource,
		const char* name, int durationOverride, int& unknownType, int& failed)
	{
		const char* names[1] = { name };
		int count = 0;

		const HRESULT hr = ScaffoldInterop::AE_AttachCompat(
			pUnit, pOwner, pUnit, pSource, names, 1,
			durationOverride, -1, -1, recreationDelay_NoOverride, &count, false, false);

		if (hr == S_FALSE)
		{
			// 提供方不认识该类型名, 属于配置错误而非调用失败
			Debug::Log("[AE] AE_Attach: provider has no AE type [%s] (unit [%s], S_FALSE)\n",
				name, pUnit->get_ID());
			++unknownType;
			return 0;
		}

		if (FAILED(hr))
		{
			Debug::Log("[AE] AE_Attach: failed for [%s] on unit [%s], hr=0x%08X\n",
				name, pUnit->get_ID(), static_cast<unsigned>(hr));
			++failed;
			return 0;
		}

		return count;
	}

	// ---- 单个单位: 按类型名移除(typeCount 可为"全部类型") ----
	int DetachOne(TechnoClass* pUnit, const char** names, int typeCount, int& unknownType, int& failed)
	{
		int count = 0;
		const HRESULT hr = ScaffoldInterop::AE_Detach(pUnit, names, typeCount, &count);

		if (hr == S_FALSE)
		{
			Debug::Log("[AE] AE_Detach: provider has no AE matching the given name (unit [%s], S_FALSE)\n",
				pUnit->get_ID());
			++unknownType;
			return 0;
		}

		if (FAILED(hr))
		{
			Debug::Log("[AE] AE_Detach: failed on unit [%s], hr=0x%08X\n",
				pUnit->get_ID(), static_cast<unsigned>(hr));
			++failed;
			return 0;
		}

		return count;
	}

	// ---- 单个单位: 按分组名移除 ----
	int DetachByGroupsOne(TechnoClass* pUnit, const char** groups, int groupCount, int& unknownGroup, int& failed)
	{
		int count = 0;
		const HRESULT hr = ScaffoldInterop::AE_DetachByGroups(pUnit, groups, groupCount, &count);

		if (hr == S_FALSE)
		{
			Debug::Log("[AE] AE_DetachByGroups: provider has no AE group [%s] (unit [%s], S_FALSE)\n",
				groupCount > 0 && groups[0] ? groups[0] : "<none>", pUnit->get_ID());
			++unknownGroup;
			return 0;
		}

		if (FAILED(hr))
		{
			Debug::Log("[AE] AE_DetachByGroups: failed for group [%s] on unit [%s], hr=0x%08X\n",
				groupCount > 0 && groups[0] ? groups[0] : "<none>", pUnit->get_ID(), static_cast<unsigned>(hr));
			++failed;
			return 0;
		}

		return count;
	}

	// 全部附加效果类型名; 调用期间指针保持稳定。
	std::vector<const char*> AllTypeNames()
	{
		std::vector<const char*> names;
		names.reserve(AttachEffectTypeClass::Array.size());

		for (auto const& pType : AttachEffectTypeClass::Array)
			names.push_back(static_cast<const char*>(pType->Name));

		return names;
	}

	void LogApply(const char* name, int unknownType, int failed)
	{
		if (unknownType)
			Debug::Log("[Scaffold] AttachEffectService: provider has no AE type [%s] (%d units skipped)\n",
				name, unknownType);

		if (failed)
			Debug::Log("[Scaffold] AttachEffectService: AE_Attach failed for [%s] on %d units\n",
				name, failed);
	}

	// 单目标移除的日志(组名 / 类型名都走这里)。
	void LogDetachOne(const char* what, int unknown, int failed)
	{
		if (unknown)
			Debug::Log("[Scaffold] AttachEffectService: provider has no AE matching [%s]\n", what);

		if (failed)
			Debug::Log("[Scaffold] AttachEffectService: AE removal failed for [%s]\n", what);
	}
}

const char* AttachEffectService::ResolveName(int key)
{
	// 参数是 [AttachEffectTypes] 里的键, 不是数组下标 —— 键可以不连续(跳跃),
	// 也可能与装载顺序不同, 所以按键找而不是按位置找。
	for (auto const& pType : AttachEffectTypeClass::Array)
	{
		if (pType->Key == key)
			return static_cast<const char*>(pType->Name);
	}

	return nullptr;
}

int AttachEffectService::ApplyToTeam(TeamClass* pTeam, int nameIndex, int durationOverride)
{
	if (!pTeam)
		return 0;

	// 可选依赖缺失时不做事, 但要留下原因: 调用方(脚本动作)照常推进, 不因未装 Phobos 卡住地图。
	if (!CheckProvider("ApplyToTeam", HasAttachEntry()))
		return 0;

	const char* const name = ResolveName(nameIndex);

	if (!name)
	{
		LogBadKey(nameIndex);
		return 0;
	}

	int attached = 0;
	int unknownType = 0;
	int failed = 0;

	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (!IsEligibleMember(pUnit))
		{
			LogSkipped("ApplyToTeam", pUnit, "dead / off-map / in limbo / carried");
			continue;
		}

		attached += ApplyToOne(pUnit, pTeam->Owner, pTeam, name, durationOverride, unknownType, failed);
	}

	Debug::Log("[AE] ApplyToTeam [%s]: key=%d -> [%s], durationOverride=%d, attached=%d, unknownType=%d, failed=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", nameIndex, name, durationOverride,
		attached, unknownType, failed);

	LogApply(name, unknownType, failed);

	return attached;
}

int AttachEffectService::ApplyToTechno(TechnoClass* pTechno, int nameKey, int durationOverride)
{
	if (!pTechno)
		return 0;

	if (!CheckProvider("ApplyToTechno", HasAttachEntry()))
		return 0;

	if (!IsEligibleMember(pTechno))
	{
		LogSkipped("ApplyToTechno", pTechno, "dead / off-map / in limbo / carried");
		return 0;
	}

	const char* const name = ResolveName(nameKey);

	if (!name)
	{
		LogBadKey(nameKey);
		return 0;
	}

	int unknownType = 0;
	int failed = 0;

	// 与小队动作一致: invoker / source 取单位自身, 归属方取单位所属方。
	const int attached = ApplyToOne(pTechno, pTechno->Owner, pTechno, name, durationOverride, unknownType, failed);

	Debug::Log("[AE] ApplyToTechno [%s]: key=%d -> [%s], durationOverride=%d, attached=%d, unknownType=%d, failed=%d\n",
		pTechno->get_ID(), nameKey, name, durationOverride, attached, unknownType, failed);

	LogApply(name, unknownType, failed);

	return attached;
}

int AttachEffectService::RemoveFromTeam(TeamClass* pTeam, int nameIndex)
{
	if (!pTeam)
		return 0;

	if (!CheckProvider("RemoveFromTeam", ScaffoldInterop::AE_Detach))
		return 0;

	const char* const name = ResolveName(nameIndex);

	if (!name)
	{
		LogBadKey(nameIndex);
		return 0;
	}

	const char* names[1] = { name };
	int removed = 0;
	int unknownType = 0;
	int failed = 0;

	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (!IsRemovableMember(pUnit))
		{
			LogSkipped("RemoveFromTeam", pUnit, "not alive");
			continue;
		}

		removed += DetachOne(pUnit, names, 1, unknownType, failed);
	}

	Debug::Log("[AE] RemoveFromTeam [%s]: key=%d -> [%s], removed=%d, unknownType=%d, failed=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", nameIndex, name, removed, unknownType, failed);

	if (unknownType)
		Debug::Log("[Scaffold] AttachEffectService: provider has no AE type [%s] (%d members skipped)\n",
			name, unknownType);

	if (failed)
		Debug::Log("[Scaffold] AttachEffectService: AE_Detach failed for [%s] on %d members\n",
			name, failed);

	return removed;
}

int AttachEffectService::RemoveFromTechno(TechnoClass* pTechno, int nameKey)
{
	if (!pTechno)
		return 0;

	if (!CheckProvider("RemoveFromTechno", ScaffoldInterop::AE_Detach))
		return 0;

	if (!IsRemovableMember(pTechno))
	{
		LogSkipped("RemoveFromTechno", pTechno, "not alive");
		return 0;
	}

	const char* const name = ResolveName(nameKey);

	if (!name)
	{
		LogBadKey(nameKey);
		return 0;
	}

	const char* names[1] = { name };
	int unknownType = 0;
	int failed = 0;
	const int removed = DetachOne(pTechno, names, 1, unknownType, failed);

	Debug::Log("[AE] RemoveFromTechno [%s]: key=%d -> [%s], removed=%d, unknownType=%d, failed=%d\n",
		pTechno->get_ID(), nameKey, name, removed, unknownType, failed);

	LogDetachOne(name, unknownType, failed);

	return removed;
}

int AttachEffectService::RemoveGroupsFromTeam(TeamClass* pTeam, const char* groupName)
{
	if (!pTeam || !groupName || !groupName[0])
		return 0;

	if (!CheckProvider("RemoveGroupsFromTeam", ScaffoldInterop::AE_DetachByGroups))
		return 0;

	// 组名就是 Phobos 的 Groups= 标签, 不需要 Scaffold 侧再维护一份分组表。
	const char* groups[1] = { groupName };
	int removed = 0;
	int unknownGroup = 0;
	int failed = 0;

	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (!IsRemovableMember(pUnit))
		{
			LogSkipped("RemoveGroupsFromTeam", pUnit, "not alive");
			continue;
		}

		removed += DetachByGroupsOne(pUnit, groups, 1, unknownGroup, failed);
	}

	Debug::Log("[AE] RemoveGroupsFromTeam [%s]: group=[%s], removed=%d, unknownGroup=%d, failed=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", groupName, removed, unknownGroup, failed);

	if (unknownGroup)
		Debug::Log("[Scaffold] AttachEffectService: provider has no AE group [%s] (%d members skipped)\n",
			groupName, unknownGroup);

	if (failed)
		Debug::Log("[Scaffold] AttachEffectService: AE_DetachByGroups failed for [%s] on %d members\n",
			groupName, failed);

	return removed;
}

int AttachEffectService::RemoveGroupsFromTechno(TechnoClass* pTechno, const char* groupName)
{
	if (!pTechno || !groupName || !groupName[0])
		return 0;

	if (!CheckProvider("RemoveGroupsFromTechno", ScaffoldInterop::AE_DetachByGroups))
		return 0;

	if (!IsRemovableMember(pTechno))
	{
		LogSkipped("RemoveGroupsFromTechno", pTechno, "not alive");
		return 0;
	}

	const char* groups[1] = { groupName };
	int unknownGroup = 0;
	int failed = 0;
	const int removed = DetachByGroupsOne(pTechno, groups, 1, unknownGroup, failed);

	Debug::Log("[AE] RemoveGroupsFromTechno [%s]: group=[%s], removed=%d, unknownGroup=%d, failed=%d\n",
		pTechno->get_ID(), groupName, removed, unknownGroup, failed);

	LogDetachOne(groupName, unknownGroup, failed);

	return removed;
}

int AttachEffectService::RemoveAllFromTeam(TeamClass* pTeam)
{
	if (!pTeam)
		return 0;

	if (!CheckProvider("RemoveAllFromTeam", ScaffoldInterop::AE_Detach))
		return 0;

	if (AttachEffectTypeClass::Array.empty())
	{
		Debug::Log("[AE] RemoveAllFromTeam: no [AttachEffectTypes] entries loaded, nothing to submit\n");
		return 0;
	}

	// 注意不能加 const: interop 要的是 const char**, const 向量的 data() 是 const char* const*。
	auto names = AllTypeNames();
	const int typeCount = static_cast<int>(names.size());

	int removed = 0;
	int unknownTypes = 0;
	int failed = 0;

	for (auto pUnit = pTeam->FirstUnit; pUnit; pUnit = pUnit->NextTeamMember)
	{
		if (!IsRemovableMember(pUnit))
		{
			LogSkipped("RemoveAllFromTeam", pUnit, "not alive");
			continue;
		}

		// 逐成员一次性提交全部类型名: Phobos 内部对每个类型调用 RemoveAllOfType。
		removed += DetachOne(pUnit, names.data(), typeCount, unknownTypes, failed);
	}

	Debug::Log("[AE] RemoveAllFromTeam [%s]: types=%d, removed=%d, unknownTypes=%d, failed=%d\n",
		pTeam->Type ? pTeam->Type->get_ID() : "<none>", typeCount, removed, unknownTypes, failed);

	// S_FALSE = 这些名字提供方一个都不认识, 多半是 [AttachEffectTypes] 与提供方脱节。
	if (unknownTypes)
		Debug::Log("[Scaffold] AttachEffectService: provider recognized none of the %d AE types (%d members skipped); is [AttachEffectTypes] in sync?\n",
			typeCount, unknownTypes);

	if (failed)
		Debug::Log("[Scaffold] AttachEffectService: AE_Detach (all %d types) failed on %d members\n",
			typeCount, failed);

	return removed;
}

int AttachEffectService::RemoveAllFromTechno(TechnoClass* pTechno)
{
	if (!pTechno)
		return 0;

	if (!CheckProvider("RemoveAllFromTechno", ScaffoldInterop::AE_Detach))
		return 0;

	if (AttachEffectTypeClass::Array.empty())
	{
		Debug::Log("[AE] RemoveAllFromTechno: no [AttachEffectTypes] entries loaded, nothing to submit\n");
		return 0;
	}

	if (!IsRemovableMember(pTechno))
	{
		LogSkipped("RemoveAllFromTechno", pTechno, "not alive");
		return 0;
	}

	// 注意不能加 const: interop 要的是 const char**, const 向量的 data() 是 const char* const*。
	auto names = AllTypeNames();
	const int typeCount = static_cast<int>(names.size());

	int unknownTypes = 0;
	int failed = 0;
	const int removed = DetachOne(pTechno, names.data(), typeCount, unknownTypes, failed);

	Debug::Log("[AE] RemoveAllFromTechno [%s]: types=%d, removed=%d, unknownTypes=%d, failed=%d\n",
		pTechno->get_ID(), typeCount, removed, unknownTypes, failed);

	if (unknownTypes)
		Debug::Log("[Scaffold] AttachEffectService: provider recognized none of the %d AE types; is [AttachEffectTypes] in sync?\n",
			typeCount);

	if (failed)
		Debug::Log("[Scaffold] AttachEffectService: AE_Detach (all %d types) failed\n", typeCount);

	return removed;
}
