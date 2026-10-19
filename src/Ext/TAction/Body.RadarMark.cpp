#include "Body.h"

#include <YRpp.h>
#include <TagClass.h>
#include <TechnoClass.h>
#include <TriggerClass.h>
#include <AnimTypeClass.h>
#include <ScenarioClass.h>

#include <Utilities/Debug.h>

#include <New/RadarMark/RadarMark.h>

namespace
{
	// 按对象固定 tag：同一条触发反复执行时原地更新，不会叠出一堆标记
	void MakeMarkTag(char(&buffer)[64], TechnoClass* pTechno)
	{
		// 用 UniqueID 而不是指针：存读档后对象地址会变，重新执行 1108 才能对上读档还原的条目
		sprintf_s(buffer, "__RadarActionMark_%u", static_cast<unsigned int>(pTechno->UniqueID));
	}

	// 按路径点固定 tag：1109 按对象清，清不到这里，所以两者互不干扰
	void MakeWaypointMarkTag(char(&buffer)[64], int wpIndex)
	{
		sprintf_s(buffer, "__RadarActionMarkWP_%d", wpIndex);
	}

	// 动画只按 Value 中的地编下拉序号解析。
	AnimTypeClass* ResolveAnim(TActionClass* pThis, int actionId)
	{
		auto& animTypes = AnimTypeClass::Array;

		if (pThis->Value >= 0 && pThis->Value < animTypes.Count && animTypes[pThis->Value])
			return animTypes[pThis->Value];

		Debug::Log("[RadarMark] 行为 %d: 无效动画序号(value=%d count=%d)\n",
			actionId, pThis->Value, animTypes.Count);

		return nullptr;
	}
}

// 1108：把关联单位（挂了本触发标签的科技类型）以指定动画显示到小地图上。
// 动画取 Value（地编动画下拉的序号），见 ResolveAnim。
// Param3 = 是否在黑幕中也显示（0/1）
// 显示时长由动画自身决定（帧数 × Rate × LoopCount；LoopCount<=0 即常驻）
bool TActionExt::SetAssociatedTechnosRadarMark(TActionClass* pThis, HouseClass* pHouse,
	ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	if (!pTrigger)
		return false;

	AnimTypeClass* pAnim = ResolveAnim(pThis, 1108);
	if (!pAnim)
		return false;

	RadarArt art { pAnim };
	art.HideInShroud = (pThis->Param3 == 0);
	int const during = RadarMark::AnimDuration(pAnim);

	for (TechnoClass* pTechno : TechnoClass::Array)
	{
		if (!pTechno || !pTechno->AttachedTag || !pTechno->AttachedTag->ContainsTrigger(pTrigger))
			continue;

		char tag[64];
		MakeMarkTag(tag, pTechno);
		RadarMark::Add(pTechno, art, during, tag);
	}

	return true;
}

// 1109：清除关联单位身上的全部小地图标记（1108 加的 + [TechnoType] RadarAnim 的）。
bool TActionExt::ClearAssociatedTechnosRadarMark(TActionClass* pThis, HouseClass* pHouse,
	ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	if (!pTrigger)
		return false;

	for (TechnoClass* pTechno : TechnoClass::Array)
	{
		if (!pTechno || !pTechno->AttachedTag || !pTechno->AttachedTag->ContainsTrigger(pTrigger))
			continue;

		// 类型驱动的标记由 RadarMarkAuto 记账，删掉后不会被当帧补回
		RadarMark::RemoveAll(pTechno);
	}

	return true;
}

// 1110 / 1111：钉在路径点格上的小地图图标。用固定 tag 保证同一路径点反复执行只更新、不叠加。
bool TActionExt::SetWaypointRadarMark(TActionClass* pThis, HouseClass* pHouse,
	ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	int const wpIndex = pThis->Param3;
	if (wpIndex < 0 || wpIndex > 701 || !ScenarioClass::Instance)   // 路径点合法范围 [0..701]
		return false;

	CellStruct const cell = ScenarioClass::Instance->GetWaypointCoords(wpIndex);
	if (cell.X < 0 || cell.Y < 0)   // 路径点未定义
		return false;

	AnimTypeClass* pAnim = ResolveAnim(pThis, 1110);
	if (!pAnim)
		return false;

	RadarArt art { pAnim };
	art.HideInShroud = (pThis->Param4 == 0);

	char tag[64];
	MakeWaypointMarkTag(tag, wpIndex);
	RadarMark::Add(cell, art, RadarMark::AnimDuration(pAnim), tag);

	return true;
}

bool TActionExt::ClearWaypointRadarMark(TActionClass* pThis, HouseClass* pHouse,
	ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	int const wpIndex = pThis->Param3;
	if (wpIndex < 0 || wpIndex > 701)
		return false;

	char tag[64];
	MakeWaypointMarkTag(tag, wpIndex);
	RadarMark::RemoveByTag(tag);

	return true;
}

// 1112：移除所有当前小地图标记，保留脏矩形供下一帧擦除。无需参数。
bool TActionExt::ClearAllRadarMarks(TActionClass* pThis, HouseClass* pHouse,
	ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)
{
	RadarMark::ClearAll();
	return true;
}
