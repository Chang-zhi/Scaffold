#pragma once

#include <FootClass.h>
#include <TeamClass.h>

#include <algorithm>
#include <vector>

class ScaffoldStreamReader;
class ScaffoldStreamWriter;

class ChronoTeleportService
{
public:
	static void FireToWaypoint(TeamClass* pTeam, int waypointIndex);

	// 传送后修正运动器残留目标（pRefTarget = DriveLocomotionClass+0x34）。
	// 只管本次登记的成员；登记表为空时钩子直接返回。
	static bool FixLocomotorTarget(FootClass* pOwner, CoordStruct* pRefTarget);

	// 每帧调用（挂点：ScriptExt::ProcessAction）：等待判定 + 落点残留清理
	static void Tick(TeamClass* pTeam);

	// 引擎写落点目标时通知（挂点：0x6CCCA8 落点放置的 SetDestination 调用点）
	static void NotifyWarpDestWritten(FootClass* pUnit);

	static void Clear();
	static void PointerGotInvalid(void* ptr, bool removed);
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
	static bool LoadGlobals(ScaffoldStreamReader& Stm);

	// 无待处理登记时运动器钩子据此直接返回
	static bool HasPending()
	{
		return !PendingFixes.empty();
	}

	// 该成员是否仍在某次传送事件中（时效判据，事件结束自动失效）
	static bool IsTracked(FootClass* pMember);

private:
	struct PendingFixEntry
	{
		FootClass* Unit;
		CellStruct OriginCell;                   // 发射时成员所在格（用于判断"已被传送离开"）
		bool Load(ScaffoldStreamReader& Stm, bool registerForChange);
		bool Save(ScaffoldStreamWriter& Stm) const;
	};

	// 落点残留：引擎落点放置写下的目标格（由 0x6CCCA8 钩子报告）。
	// Cell 在写入完成后抓取，仅当成员 Destination 仍是它时才清除。
	struct ResidueEntry
	{
		FootClass* Unit;
		CellClass* Cell;
		bool Load(ScaffoldStreamReader& Stm, bool registerForChange);
		bool Save(ScaffoldStreamWriter& Stm) const;
	};

	// 一次传送事件的记录：落点放置结束前不推进脚本，之后持续清理落点残留。
	// 产生、存续、结束全部由状态决定，无帧数窗口。
	struct PendingWait
	{
		TeamClass* Team;
		CellStruct LandingCell;                  // 本次落点格
		bool Completed;                          // 脚本已推进（此后只做落点残留清理）
		std::vector<FootClass*> Members;         // 本次登记（源点 5×5 内）的成员
		std::vector<ResidueEntry> Residues;      // 待清除的落点残留（钩子报告）
		bool Load(ScaffoldStreamReader& Stm, bool registerForChange);
		bool Save(ScaffoldStreamWriter& Stm) const;

		bool IsPlaced(FootClass* pMember) const
		{
			return std::any_of(Residues.begin(), Residues.end(),
				[pMember](const ResidueEntry& e) { return e.Unit == pMember; });
		}
	};

	static std::vector<PendingFixEntry> PendingFixes;
	static std::vector<PendingWait> PendingWaits;

	static void TrackTeam(TeamClass* pTeam, const CellStruct& srcCell, std::vector<FootClass*>& outMembers);

	// 清掉落点残留目标，使队伍的下一条脚本指令能重新指派这些成员
	static void CleanupLandingResidue(PendingWait& wait);

	// 记录存续判据：是否还有成员停留在落点区域 / 传送尚未收尾
	static bool AnyMemberNearLanding(const PendingWait& wait);
	static bool AnyMemberWarping(const PendingWait& wait);
};
