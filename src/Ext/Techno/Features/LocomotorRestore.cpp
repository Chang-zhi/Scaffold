#include <Ext/Techno/Body.h>
#include <Ext/Rules/Body.h>

#include <FootClass.h>

// 磁电武器（Warhead IsLocomotor=yes）命中单位时，原版会在 0x710000 里清空其目标、
// 把目的地改成磁电坦克，释放后单位停在原地。本功能在吸取前存下原任务，落地释放时恢复。

void TechnoExt::LocomotorRestoreState::Capture(FootClass* pTarget)
{
	if (!pTarget)
		return;

	// 仍在吸取中说明存的是被改写过的状态，保留最初那份
	if (this->Active && pTarget->IsAttackedByLocomotor)
		return;

	this->Clear();

	this->Target = pTarget->Target;
	this->ArchiveTarget = pTarget->ArchiveTarget;
	this->Destination = pTarget->Destination;
	this->LastDestination = pTarget->LastDestination;
	this->MegaDestination = pTarget->MegaDestination;
	this->MegaTarget = pTarget->MegaTarget;
	this->MegaMission = pTarget->MegaMission;
	this->OriginalMission = pTarget->CurrentMission;
	this->HaveAttackMoveTarget = pTarget->HaveAttackMoveTarget;

	// 本来就没任务（驻守等）就不必记
	this->Active = this->Target || this->ArchiveTarget || this->Destination
		|| this->MegaDestination || this->MegaTarget;
}

void TechnoExt::LocomotorRestoreState::Restore(FootClass* pTarget)
{
	const LocomotorRestoreState saved = *this;
	this->Clear();

	if (!saved.Active || !pTarget)
		return;

	if (!pTarget->IsAlive || pTarget->Health <= 0 || pTarget->InLimbo)
		return; // 已阵亡/已离场：只丢弃记录

	// 攻击移动的数据必须先于 SetDestination 写回
	pTarget->MegaDestination = saved.MegaDestination;
	pTarget->MegaTarget = saved.MegaTarget;
	pTarget->MegaMission = saved.MegaMission;
	pTarget->HaveAttackMoveTarget = saved.HaveAttackMoveTarget;

	AbstractClass* const pDest = (saved.MegaMission == Mission::AttackMove && saved.MegaDestination)
		? saved.MegaDestination : saved.Destination;

	if (pDest)
		pTarget->SetDestination(pDest, true);

	pTarget->LastDestination = saved.LastDestination;

	if (saved.Target)
		pTarget->SetTarget(saved.Target);

	// ArchiveTarget 可能是矿田格等非 Techno 对象，直接写回即可
	pTarget->ArchiveTarget = saved.ArchiveTarget;

	Mission mission = Mission::None;

	if (saved.MegaMission == Mission::AttackMove)
		mission = Mission::Move; // 攻击移动 = Move + Mega 数据
	else if (saved.Target)
		mission = Mission::Attack;
	else if (saved.ArchiveTarget && saved.OriginalMission == Mission::Guard)
		mission = Mission::Guard;
	else if (saved.OriginalMission == Mission::Harvest)
		mission = Mission::Harvest;
	else if (pDest)
		mission = Mission::Move;

	if (mission != Mission::None)
		pTarget->QueueMission(mission, true); // 与玩家下令一致：替换掉原版排队的 Guard
}

void TechnoExt::LocomotorRestoreState::Clear()
{
	*this = LocomotorRestoreState {};
}

void TechnoExt::LocomotorRestoreOnImbue(FootClass* pTarget)
{
	auto const pRules = RulesExt::Global();
	if (!pTarget || !pRules || !pRules->LocomotorRestoreTask)
		return;

	auto const pExt = TechnoExt::ExtMap.Find(pTarget);
	if (!pExt)
		return;

	pExt->LocomotorRestore.Capture(pTarget);
}

// 每帧检查（挂点：TechnoClass::AI）：
// 吸取结束时原版会先落地、再在 End_Piggyback 里恢复原始运动器，标志位随之清零。
// 因此等到标志位为 0 再恢复，此时 SetDestination 才会落到原始运动器上。
void TechnoExt::LocomotorRestoreCheck(TechnoClass* pThis)
{
	if (!pThis)
		return;

	auto const pExt = TechnoExt::ExtMap.Find(pThis);
	if (!pExt || !pExt->LocomotorRestore.Active)
		return;

	// 载入存档后规则可能被关掉：关掉就不再恢复
	auto const pRules = RulesExt::Global();
	if (!pRules || !pRules->LocomotorRestoreTask)
	{
		pExt->LocomotorRestore.Clear();
		return;
	}

	auto const pFoot = abstract_cast<FootClass*>(pThis);
	if (!pFoot)
	{
		pExt->LocomotorRestore.Clear();
		return;
	}

	if (pFoot->IsAttackedByLocomotor)
		return; // 仍在被吸取

	pExt->LocomotorRestore.Restore(pFoot);
}

// 吸取开始：原版 0x710000 此时还没清受害者目标、还没改写目的地。
// 该处原指令：mov eax, [esi+674h]（esi = 受害者）。
DEFINE_HOOK(0x71005D, FootClass_ImbueLocomotor_CaptureTask, 0x6)
{
	GET(FootClass*, pTarget, ESI);

	TechnoExt::LocomotorRestoreOnImbue(pTarget);

	return 0;
}
