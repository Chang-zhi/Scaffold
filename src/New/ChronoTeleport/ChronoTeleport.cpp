#include "ChronoTeleport.h"
#include <Utilities/SavegameDef.h>

#include <SuperClass.h>
#include <SuperWeaponTypeClass.h>
#include <ScenarioClass.h>
#include <HouseClass.h>
#include <RulesClass.h>
#include <MapClass.h>
#include <CellClass.h>
#include <TechnoTypeClass.h>

#include <Helpers/Macro.h>

#include <algorithm>
#include <cstdlib>

namespace
{
	struct VanillaLeaderCheck
	{
		bool IsUsable() const
			{ JMP_THIS(0x6EF9E0); }
	};

	void AssignMissionTargetObjectBranch(TeamClass* pTeam, AbstractClass* pTarget)
	{
		AbstractClass* const oldFocus = pTeam->QueuedFocus;
		if (pTarget != oldFocus && oldFocus)
		{
			for (auto pMember = pTeam->FirstUnit; pMember; pMember = pMember->NextTeamMember)
			{
				const bool isDest = (pMember->Destination == oldFocus);
				const bool isTarget = (pMember->Target == oldFocus);
				if (isDest || isTarget)
				{
					pMember->QueueMission(Mission::Guard, false);
					if (isDest)
						pMember->SetDestination(nullptr, true);
					if (isTarget)
						pMember->SetTarget(nullptr);
				}
			}
		}

		if (pTeam->Focus == oldFocus || !pTeam->Focus)
			pTeam->Focus = pTarget;
		pTeam->QueuedFocus = pTarget;
	}
}

std::vector<ChronoTeleportService::PendingFixEntry> ChronoTeleportService::PendingFixes;
std::vector<ChronoTeleportService::PendingWait> ChronoTeleportService::PendingWaits;

bool ChronoTeleportService::PendingFixEntry::Load(ScaffoldStreamReader& Stm, bool registerForChange)
{
	return Stm.Process(Unit, registerForChange).Process(OriginCell).Success();
}

bool ChronoTeleportService::PendingFixEntry::Save(ScaffoldStreamWriter& Stm) const
{
	Stm.Save(Unit);
	Stm.Save(OriginCell);
	return Stm.Success();
}

bool ChronoTeleportService::ResidueEntry::Load(ScaffoldStreamReader& Stm, bool registerForChange)
{
	return Stm.Process(Unit, registerForChange).Process(Cell, registerForChange).Success();
}

bool ChronoTeleportService::ResidueEntry::Save(ScaffoldStreamWriter& Stm) const
{
	Stm.Save(Unit);
	Stm.Save(Cell);
	return Stm.Success();
}

bool ChronoTeleportService::PendingWait::Load(ScaffoldStreamReader& Stm, bool registerForChange)
{
	return Stm.Process(Team, registerForChange).Process(LandingCell).Process(Completed)
		.Process(Members, registerForChange).Process(Residues, registerForChange).Success();
}

bool ChronoTeleportService::PendingWait::Save(ScaffoldStreamWriter& Stm) const
{
	Stm.Save(Team);
	Stm.Save(LandingCell);
	Stm.Save(Completed);
	return Savegame::WriteScaffoldStream(Stm, Members)
		&& Savegame::WriteScaffoldStream(Stm, Residues) && Stm.Success();
}

bool ChronoTeleportService::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	return Stm.Process(PendingFixes).Process(PendingWaits).Success();
}

bool ChronoTeleportService::LoadGlobals(ScaffoldStreamReader& Stm)
{
	Clear();
	return Stm.Process(PendingFixes).Process(PendingWaits).Success();
}

// 落点偏移表覆盖半径（±3 格）
static constexpr int LandingCells = 3;

// DriveLocomotionClass 的 ILocomotion vtable
static void* const DriveLocoVTable = reinterpret_cast<void*>(0x7E7EB0);

// FootClass::Locomotor(+0x674) 指向接口对象，首 dword 即 vtable
static void* GetLocomotorVTable(FootClass* pMember)
{
	const auto pp = reinterpret_cast<void**>(reinterpret_cast<char*>(pMember) + 0x674);
	void* const pLoco = *pp;
	return pLoco ? *reinterpret_cast<void**>(pLoco) : nullptr;
}

// 传送是否仍在进行：传送期间运动器被换成非 Drive，落点放置后恢复
static bool IsWarpInProgress(FootClass* pMember)
{
	if (!pMember->IsAlive || pMember->Health <= 0 || pMember->Transporter)
		return false; // 已阵亡/已登机：不会再落点

	if (pMember->InLimbo)
		return true; // limbo 期间读不到运动器

	void* const vt = GetLocomotorVTable(pMember);
	return vt != nullptr && vt != DriveLocoVTable;
}

// 发射（对齐原版动作 56 / 0x6EFE60）
void ChronoTeleportService::FireToWaypoint(TeamClass* pTeam, int waypointIndex)
{
	if (!pTeam)
		return;

	// 等待阶段：本次成员传送与落点未结束前不推进脚本；
	// 推进后由 Tick 持续清掉落点残留目标（见 CleanupLandingResidue）
	for (auto it = PendingWaits.begin(); it != PendingWaits.end(); ++it)
	{
		// 已推进脚本的记录只做残留清理
		if (it->Team != pTeam || it->Completed)
			continue;

		bool waiting = false;
		for (auto pMember = pTeam->FirstUnit; pMember; pMember = pMember->NextTeamMember)
		{
			if (std::find(it->Members.begin(), it->Members.end(), pMember) == it->Members.end())
				continue; // 本次未登记

			if (it->IsPlaced(pMember))
				continue; // 引擎落点目标已写入（钩子已报告）

			if (IsWarpInProgress(pMember))
			{
				waiting = true; // 仍在传送/尚未落点，等它
				break;
			}
		}

		if (waiting)
		{
			pTeam->StepCompleted = false;
			return;
		}

		CleanupLandingResidue(*it);

		it->Completed = true; // 记录保留到 Tick 的残留清理结束
		pTeam->StepCompleted = true;
		return;
	}

	// 1. 队长：可用（0x6EF9E0）且已入队（或飞行器），LeadershipRating 最高
	FootClass* pLeader = nullptr;
	int bestRating = -1;
	for (auto pMember = pTeam->FirstUnit; pMember; pMember = pMember->NextTeamMember)
	{
		const auto rating = static_cast<TechnoTypeClass*>(pMember->GetType())->LeadershipRating;
		if (rating > bestRating
			&& reinterpret_cast<const VanillaLeaderCheck*>(pMember)->IsUsable()
			&& (pMember->IsInitiated || pMember->WhatAmI() == AbstractType::Aircraft))
		{
			pLeader = pMember;
			bestRating = rating;
		}
	}

	if (!pLeader)
	{
		pTeam->StepCompleted = true;
		return;
	}

	// 2. 取本方 ChronoSphere(3) / ChronoWarp(4)
	SuperClass* pSphere = nullptr;
	SuperClass* pWarp = nullptr;
	for (auto pSuper : pLeader->Owner->Supers)
	{
		if (pSuper->Type->Type == SuperWeaponType::ChronoSphere)
			pSphere = pSuper;
		else if (pSuper->Type->Type == SuperWeaponType::ChronoWarp)
			pWarp = pSuper;
	}

	if (!pSphere || !pWarp)
	{
		pTeam->StepCompleted = true;
		return;
	}

	HouseClass* const pHouse = pLeader->Owner;

	if (pSphere->IsReady && pHouse->GetPowerPercentage() >= 1.0)
	{
		// 路径点只能经原版接口读（数组在 ScenarioClass+0x632，YRpp 的 Waypoints 偏移差 2 字节）
		if (!ScenarioClass::Instance->IsDefinedWaypoint(waypointIndex))
		{
			pTeam->StepCompleted = true; // 路径点未定义 → 放弃本条
			return;
		}

		const CellStruct wpCell = ScenarioClass::Instance->GetWaypointCoords(waypointIndex);

		// 落点原样透传：候选格与"压到地形是否爆炸"由引擎 case 4 自行决定
		const auto pDestCell = MapClass::Instance.TryGetCellAt(wpCell);
		if (!pDestCell)
		{
			pTeam->StepCompleted = true; // 路径点在图外 → 放弃本条
			return;
		}

		// 源点取队伍现处格（原版用 SpawnCell，对已开拔的队伍会失效）
		FootClass* pSrcMember = nullptr;
		if (pLeader->WhatAmI() != AbstractType::Aircraft)
		{
			pSrcMember = pLeader;
		}
		else
		{
			for (auto pMember = pTeam->FirstUnit; pMember; pMember = pMember->NextTeamMember)
			{
				if (pMember->WhatAmI() != AbstractType::Aircraft)
				{
					pSrcMember = pMember;
					break;
				}
			}
		}

		if (!pSrcMember)
		{
			pTeam->StepCompleted = true; // 全是飞行器,没有可传送对象
			return;
		}

		const CellStruct srcCell = pSrcMember->CurrentMapCoords;

		// 记录须先于发射建立，钩子才能登记落点写入
		std::vector<FootClass*> trackedMembers;
		TrackTeam(pTeam, srcCell, trackedMembers);

		PendingWaits.push_back({ pTeam, pDestCell->MapCoords, false,
			std::move(trackedMembers), {} });

		pHouse->Fire_SW(static_cast<int>(pSphere->Type->Type), srcCell);

		// Ares 在 0x4FAE72 接管了 Fire_SW 内的源点拷贝，这里按原版语义补一次
		pWarp->ChronoMapCoords = pSphere->ChronoMapCoords;

		pHouse->Fire_SW(static_cast<int>(pWarp->Type->Type), pDestCell->MapCoords);

		AssignMissionTargetObjectBranch(pTeam, pDestCell);

		pTeam->StepCompleted = false; // 保持本动作，待落点放置完成后推进
		return;
	}

	// 3-B. 未就绪/缺电：按充能比例决定等或放弃（对齐 0x6F0086）
	if (!pSphere->IsPresent)
	{
		pTeam->StepCompleted = true; // 超武建筑没了 → 放弃本条
		return;
	}

	const auto rechargeTime = pSphere->GetRechargeTime();
	const auto remain = pSphere->RechargeTimer.GetTimeLeft();
	if (rechargeTime > 0
		&& 1.0 - RulesClass::Instance->AIMinorSuperReadyPercent < static_cast<double>(remain) / rechargeTime)
	{
		pTeam->StepCompleted = true; // 离就绪太远 → 放弃本条
	}
	// 否则不动 StepCompleted，下一帧继续等
}

// 传送后保队注册表：登记源点 5×5 内的成员（实测 |d|=2 的单位也会被引擎指派落点目标）。
// 未真正被传送的成员由 FixLocomotorTarget 的"位移 >3 格"守卫排除。
void ChronoTeleportService::TrackTeam(TeamClass* pTeam, const CellStruct& srcCell, std::vector<FootClass*>& outMembers)
{
	for (auto pMember = pTeam->FirstUnit; pMember; pMember = pMember->NextTeamMember)
	{
		if (!pMember->IsAlive || pMember->Health <= 0 || pMember->InLimbo)
			continue;
		if (pMember->Transporter)
			continue;
		if (pMember->WhatAmI() == AbstractType::Aircraft)
			continue;

		const CellStruct memberCell = pMember->CurrentMapCoords;
		if (std::abs(memberCell.X - srcCell.X) > 2 || std::abs(memberCell.Y - srcCell.Y) > 2)
			continue;

		const auto it = std::find_if(PendingFixes.begin(), PendingFixes.end(),
			[pMember](const PendingFixEntry& e) { return e.Unit == pMember; });

		if (it != PendingFixes.end())
			it->OriginCell = memberCell; // 已在表中：刷新原点格
		else
			PendingFixes.push_back({ pMember, memberCell });

		outMembers.push_back(pMember);
	}
}

bool ChronoTeleportService::FixLocomotorTarget(FootClass* pOwner, CoordStruct* pRefTarget)
{
	if (!pOwner || !pRefTarget)
		return false;

	const auto it = std::find_if(PendingFixes.begin(), PendingFixes.end(),
		[pOwner](const PendingFixEntry& e) { return e.Unit == pOwner; });
	if (it == PendingFixes.end())
		return false;

	// 记录只在本次传送事件期间有效：事件结束后未消费的直接丢弃；
	// 条件未成立时保留（成员尚未被传送时不至于白白用掉这次机会）
	const PendingWait* pWait = nullptr;
	for (const auto& wait : PendingWaits)
	{
		if (std::find(wait.Members.begin(), wait.Members.end(), pOwner) != wait.Members.end())
		{
			pWait = &wait;
			break;
		}
	}

	if (!pWait)
	{
		PendingFixes.erase(it);
		return false;
	}

	const CellStruct unitCell = pOwner->CurrentMapCoords;
	const CellStruct refCell = CellClass::Coord2Cell(*pRefTarget);

	// 已被传送离开原点（>3 格）且参考目标仍指向落点区域 = 落点残留目标
	const bool movedAway = std::abs(unitCell.X - it->OriginCell.X) > 3
		|| std::abs(unitCell.Y - it->OriginCell.Y) > 3;
	const bool refInLanding = std::abs(refCell.X - pWait->LandingCell.X) <= LandingCells
		&& std::abs(refCell.Y - pWait->LandingCell.Y) <= LandingCells;

	if (movedAway && refInLanding)
	{
		*pRefTarget = pOwner->GetCoords(); // 改成自身坐标 → 本次调用按"已到达"处理
		PendingFixes.erase(it);
		return true;
	}

	return false; // 条件未成立：保留记录，等真正出现旧目标
}

void ChronoTeleportService::Clear()
{
	PendingFixes.clear();
	PendingWaits.clear();
}

// 清掉引擎落点放置写入的残留目标（否则原版 MembersInPlaceCheck 会认为成员"在途"，
// 后续脚本不再指派它们）。只清 Destination 仍是引擎写下的那个格子对象的情形
// （CellClass 全图唯一，队伍的正常指令必然换对象），不靠坐标范围或帧数。
void ChronoTeleportService::CleanupLandingResidue(PendingWait& wait)
{
	// 队伍当前目标格；它若在落点区域内，说明"去落点"是正当指令，不做兜底清除
	CellStruct focusCell {};
	bool hasFocusCell = false;
	if (wait.Team->Focus && wait.Team->Focus->WhatAmI() == AbstractType::Cell)
	{
		focusCell = static_cast<CellClass*>(wait.Team->Focus)->MapCoords;
		hasFocusCell = true;
	}

	const bool focusInLanding = hasFocusCell
		&& std::abs(focusCell.X - wait.LandingCell.X) <= LandingCells
		&& std::abs(focusCell.Y - wait.LandingCell.Y) <= LandingCells;

	for (auto it = wait.Residues.begin(); it != wait.Residues.end(); )
	{
		FootClass* const pMember = it->Unit;

		if (std::find(wait.Members.begin(), wait.Members.end(), pMember) == wait.Members.end())
		{
			it = wait.Residues.erase(it); // 已不在队伍中（阵亡/离队/被载具收走）
			continue;
		}

		const auto pDest = pMember->Destination;
		const bool isCell = pDest && pDest->WhatAmI() == AbstractType::Cell;

		if (it->Cell == nullptr)
		{
			// 钩子报写入后的第一帧：引擎已完成写入，抓取目标格
			if (!isCell)
			{
				it = wait.Residues.erase(it); // 目标已被别的逻辑改掉：无残留可清
				continue;
			}

			it->Cell = static_cast<CellClass*>(pDest);
		}

		if (!isCell)
		{
			it = wait.Residues.erase(it); // 目标已被清空：残留已不存在
			continue;
		}

		if (static_cast<CellClass*>(pDest) != it->Cell)
		{
			it = wait.Residues.erase(it); // 目标换成了别的格子对象：是队伍的新指令
			continue;
		}

		pMember->SetDestination(nullptr, true);
		it = wait.Residues.erase(it); // 已清除：该成员不再阻挡重新指派
	}

	// 兜底：个别成员的目标不经上面的钩子写入。判据为
	// 本次传送成员 && 目标在落点区域内 && 队伍当前目标不在落点区域内。
	if (focusInLanding)
		return;

	for (auto pMember = wait.Team->FirstUnit; pMember; pMember = pMember->NextTeamMember)
	{
		if (std::find(wait.Members.begin(), wait.Members.end(), pMember) == wait.Members.end())
			continue;
		if (wait.IsPlaced(pMember))
			continue; // 已由钩子精确处理

		const auto pDest = pMember->Destination;
		if (!pDest || pDest->WhatAmI() != AbstractType::Cell)
			continue;

		const CellStruct destCell = static_cast<CellClass*>(pDest)->MapCoords;
		if (std::abs(destCell.X - wait.LandingCell.X) > LandingCells
			|| std::abs(destCell.Y - wait.LandingCell.Y) > LandingCells)
			continue; // 目标是别处的正常指令

		pMember->SetDestination(nullptr, true);
	}
}

// 该成员是否仍在某次传送事件中（保队改写与免踢人的时效判据）
bool ChronoTeleportService::IsTracked(FootClass* pMember)
{
	if (!pMember)
		return false;

	for (const auto& wait : PendingWaits)
	{
		if (std::find(wait.Members.begin(), wait.Members.end(), pMember) != wait.Members.end())
			return true;
	}

	return false;
}

// 引擎写落点目标时通知（调用点见文件末尾的钩子）。
// 此刻引擎尚未执行 SetDestination，故只登记；目标格的抓取与清除留待 CleanupLandingResidue。
void ChronoTeleportService::NotifyWarpDestWritten(FootClass* pUnit)
{
	if (!pUnit)
		return;

	for (auto& wait : PendingWaits)
	{
		if (std::find(wait.Members.begin(), wait.Members.end(), pUnit) == wait.Members.end())
			continue;

		const auto it = std::find_if(wait.Residues.begin(), wait.Residues.end(),
			[pUnit](const ResidueEntry& e) { return e.Unit == pUnit; });

		if (it != wait.Residues.end())
			it->Cell = nullptr; // 重复写入：重新抓取目标格
		else
			wait.Residues.push_back({ pUnit, nullptr });
	}
}

// 本次传送的成员中是否还有传送尚未收尾的（落点写入可能晚于传送收尾）
bool ChronoTeleportService::AnyMemberWarping(const PendingWait& wait)
{
	for (auto pMember = wait.Team->FirstUnit; pMember; pMember = pMember->NextTeamMember)
	{
		if (std::find(wait.Members.begin(), wait.Members.end(), pMember) == wait.Members.end())
			continue;

		if (IsWarpInProgress(pMember))
			return true;
	}

	return false;
}

// 本次传送的成员中是否还有停留在落点区域的（决定残留清理是否还需要继续）
bool ChronoTeleportService::AnyMemberNearLanding(const PendingWait& wait)
{
	for (auto pMember = wait.Team->FirstUnit; pMember; pMember = pMember->NextTeamMember)
	{
		if (std::find(wait.Members.begin(), wait.Members.end(), pMember) == wait.Members.end())
			continue;

		const CellStruct c = pMember->CurrentMapCoords;
		if (std::abs(c.X - wait.LandingCell.X) <= LandingCells
			&& std::abs(c.Y - wait.LandingCell.Y) <= LandingCells)
			return true;
	}

	return false;
}

// 每帧调用（挂点：ScriptExt::ProcessAction）
void ChronoTeleportService::Tick(TeamClass* pTeam)
{
	if (!pTeam)
		return;

	// 已推进脚本的记录：持续清理落点残留（写入逐成员、时机不定，一次性清理会漏）
	for (auto it = PendingWaits.begin(); it != PendingWaits.end(); )
	{
		if (it->Team != pTeam || !it->Completed)
		{
			++it;
			continue;
		}

		CleanupLandingResidue(*it);

		// 记录存续：仍有残留待清 / 仍有成员传送未收尾 / 仍有成员在落点区域
		if (!it->Residues.empty() || AnyMemberWarping(*it) || AnyMemberNearLanding(*it))
			++it;
		else
			it = PendingWaits.erase(it);
	}

	// 清掉已不存在于 TeamClass::Array 的队伍留下的记录（避免指针复用误命中）
	PendingWaits.erase(std::remove_if(PendingWaits.begin(), PendingWaits.end(),
		[](const PendingWait& w) {
			for (auto pTeam2 : TeamClass::Array)
				if (pTeam2 == w.Team) return false;
			return true;
		}), PendingWaits.end());
}

void ChronoTeleportService::PointerGotInvalid(void* ptr, bool removed)
{
	const auto pFoot = static_cast<FootClass*>(ptr);
	if (!pFoot)
		return;

	PendingFixes.erase(
		std::remove_if(PendingFixes.begin(), PendingFixes.end(),
			[pFoot](const PendingFixEntry& e) { return e.Unit == pFoot; }),
		PendingFixes.end());

	// 传送记录中同样剔除已销毁的成员（记录存续判定据此收敛）
	for (auto& wait : PendingWaits)
	{
		wait.Members.erase(
			std::remove(wait.Members.begin(), wait.Members.end(), pFoot), wait.Members.end());
		wait.Residues.erase(
			std::remove_if(wait.Residues.begin(), wait.Residues.end(),
				[pFoot](const ResidueEntry& e) { return e.Unit == pFoot; }),
			wait.Residues.end());
	}
}

// 运动器一侧挂点

// Drive 运动器目标修正（内部 0x4B28A3 会拿 this+0x34 的目标调 UpdatePathfinding）。
// 只对本表内成员生效；登记表为空立即返回。该地址 Ares 无钩子。
DEFINE_HOOK(0x4B2630, DriveLocomotion_Process_FixStaleDest, 0x5)
{
	if (!ChronoTeleportService::HasPending())
		return 0;

	GET(void*, pLoco, ECX);

	auto pOwner = *reinterpret_cast<FootClass**>(reinterpret_cast<char*>(pLoco) + 0x0C);
	auto pRefTarget = reinterpret_cast<CoordStruct*>(reinterpret_cast<char*>(pLoco) + 0x34);

	ChronoTeleportService::FixLocomotorTarget(pOwner, pRefTarget);

	return 0;
}

// 免踢人：寻路失败且距参考格 >1 格时引擎在这里 LiberateMember 把成员踢出小队。
// 原版 56/57 同帧就 AssignMissionTarget(对象) 覆盖掉落点残留目标，故不会命中；
// 5512 落点是格子且脚本会推进到别的路径点，成员会短暂带着残留目标 → 本次成员跳过。
// 该段 Ares 无钩子。
DEFINE_HOOK(0x4D410D, FootClass_UpdatePathfinding_PreventLiberate, 0xA)
{
	GET(FootClass*, pOwner, EBP);

	if (!pOwner || !ChronoTeleportService::IsTracked(pOwner))
		return 0;

	return 0x4D4117; // 跳过 push 0 / push -1 / push ebp / call LiberateMember
}

// 落点放置：引擎对每个被放置的成员调 SetDestination(落点偏移格, 1)。
// 在此登记该事件，作为"落点已写入"的精确信号（不依赖帧数/动画时长）。
DEFINE_HOOK(0x6CCCA8, SuperClass_Launch_ChronoWarpDestWrite, 0x6)
{
	GET(FootClass*, pUnit, ECX);

	ChronoTeleportService::NotifyWarpDestWritten(pUnit);

	return 0;
}
