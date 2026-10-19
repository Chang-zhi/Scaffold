#pragma once

#include <Utilities/Container.h>
#include <Utilities/Template.h>

#include <Helpers/Template.h>

#include <TActionClass.h>

class HouseClass;

enum class ScaffoldTriggerAction : unsigned int
{
	// 指定类型设置路径点标签
	SetWaypointTextBoxByType = 549,

	// 在指定路径点绘制文本...
	SetWaypointTextBoxByData = 550,

	// 清除指定路径点的文本...
	ClearWaypointTextBox = 551,

	// 清除所有路径点文本...
	ClearAllWaypointTextBoxs = 552,

	// 将指定小队全部成员关联到指定标签...
	BindAllTeamMemberToTag = 553,

	// 将指定所属方的指定小队全部成员关联到指定标签...
	BindOwnerTeamMemberToTag = 554,

	// 将特定科技类型全部关联到指定标签...
	BindAllTechnoTypeToTag = 555,

	// 将指定所属方的特定科技类型全部关联到指定标签...
	BindOwnerTechnoTypeToTag = 556,

	// 为指定所属方添加金钱数额...
	GiveHouseMoney = 557,

	// 为向指定所属方扣除金钱数额...
	TakeHouseMoney = 558,

	// 设置指定所属方的金钱数额...
	SetHouseMoney = 559,

	// 在指定路径点添加指定所属方的基地节点...
	AddBaseNodeForHouseAtWaypoint = 560,

	// 移除指定路径点的指定所属方的所有基地节点...
	RemoveAllBaseNodeForHouseAtWaypoint = 561,

	// 移除指定所属方的指定建筑类型的所有基地节点...
	RemoveBaseNodesOfBuildingTypeForHouse = 562,

	// 安全地销毁标签...
	DestroyAllTagByTagTypeSafely = 563,

	// 为路径点上的科技类型绑定标签...
	BindTagToTechnoTypeAtWaypoint = 564,

	// 为路径点上指定所属方的科技类型绑定标签...
	BindTagToTechnoTypeOfHouseAtWaypoint = 565,

	// 为路径点范围内的指定科技类型绑定标签...
	BindTagToSpecificTechnoTypeWithinWaypointRange = 566,

	// 将路径点范围内触发所属方的指定科技类型关联到指定标签...
	BindTagToSpecificTechnoTypeOfSpecificOwnerWithinWaypointRange = 567,

	// 为路径点范围内的所有科技类型绑定标签...
	BindTagToAllTechnoTypesWithinWaypointRange = 568,

	// 为路径点范围内指定所属方的所有科技类型绑定标签...
	BindTagToAllTechnoTypesOfSpecificOwnerWithinWaypointRange = 569,

	// 统一指定标签类型的所有实例
	UnifyAllInstancesOfSameTagType = 570,

	// 设置关联单位单位的可招募属性...
	SetRecruitableForFoot = 571,

	// 为路径点范围内除指定类型外的所有科技类型绑定标签...
	BindTagsToAllTechTypesInWaypointRangeExceptSpecified = 572,

	// 为路径点范围内触发所属方的除指定类型外的所有科技类型绑定标签...
	BindTagsToAllTechTypesOfTriggerOwnerInWaypointRangeExceptSpecified = 573,

	// 更新所有建筑的动画
	UpdateAllBuildingAnims = 574,

	// 更新关联建筑的动画
	UpdateAssociatedBuildingsAnims = 575,

	// 更新指定所属方所有建筑的动画
	UpdateOwnerBuildingsAnimations = 576,

	// 建立作战小队(考虑限制)...
	CreateTeamConsideringLimits = 577,

	// 将路径点范围内指定所属方的所有可招募单位加入已存在的作战小队...
	RecruitNearbyFootToTeam = 578,

	// 在关联单位上绘制文本(根据类型)...
	SetUnitTextBoxByTriggerType = 579,

	// 在关联单位上绘制文本(根据数据)...
	SetUnitTextBoxByTriggerData = 580,

	// 在指定小队所有单位上绘制文本(根据类型)...
	SetUnitTextBoxByTeamType = 581,

	// 在指定小队所有单位上绘制文本(根据数据)...
	SetUnitTextBoxByTeamData = 582,

	// 移除指定类型的所有文本...
	ClearUnitTextBoxByType = 583,

	// 移除关联单位上的文本...
	ClearUnitTextBoxByTag = 584,

	// 移除指定科技类型上的文本...
	ClearUnitTextBoxByTechType = 585,

	// 移除指定所属方的指定科技类型上的文本...
	ClearUnitTextBoxByHouseAndType = 586,

	// 移除指定小队类型的所有单位上的文本...
	ClearUnitTextBoxByTeam = 587,

	// 移除所有单位上的文本...
	ClearAllUnitTextBoxs = 588,

	// 移除所有文本(含路径点)...
	ClearAllTextBoxs = 589,

	// ---- ChoiceBox Actions (590-599) ----

	// 在路径点创建选择框（根据类型）...
	SetWaypointChoiceBox = 590,

	// 在屏幕坐标创建选择框（根据类型）...
	SetScreenChoiceBox = 591,

	// 清除指定编号的选择框...
	ClearChoiceBoxByID = 592,

	// 清除所有选择框...
	ClearAllChoiceBoxs = 593,

	// ---- Script Manipulation Actions (650-659) ----

	// 清空指定脚本的内容
	ClearScript = 650,

	// 复制源脚本的内容到目标脚本
	CopyScript = 651,

	// 以直接参数修改指定脚本的指定行
	ModifyScriptByParam = 652,

	// 以局部变量修改指定脚本的指定行
	ModifyScriptByLocalVar = 653,

	// 以全局变量修改指定脚本的指定行
	ModifyScriptByGlobalVar = 654,

	// 重新指定作战小队类型绑定的脚本
	RebindTeamTypeScript = 655,

	// 恢复作战小队类型绑定的脚本索引到初始状态
	ResetTeamTypeScript = 656,

	// 恢复所有作战小队类型绑定的脚本索引到初始状态
	ResetAllTeamTypeScripts = 657,

	// 恢复指定脚本的内容到初始状态
	RestoreScriptContent = 658,

	// 恢复所有脚本的内容到初始状态
	RestoreAllScriptContents = 659,

	// 将指定作战小队类型的所有实例的脚本执行行号跳转到指定位置
	SeekTeamTypeScript = 660,

	// 修改指定作战小队类型的 Max 值
	SetTeamTypeMaxValue = 661,

	// 绘制关联单位的移动路径
	RegisterFootPathVisualizer = 662,

	// 停止绘制关联单位的移动路径
	UnregisterFootPathVisualizer = 663,

	// 设置任务简报
	SetMissionBriefing = 664,

	// 设置超时标题
	SetOverParTitle = 665,

	// 设置超时信息
	SetOverParMessage = 666,

	// 设置按时标题
	SetUnderParTitle = 667,

	// 设置按时信息
	SetUnderParMessage = 668,

	// ---- TaskForce Editing Actions (670-677) ----

	// 清空指定作战小队类型的所有内容
	ClearTaskForce = 670,

	// 复制源作战小队类型的内容到目标作战小队类型
	CopyTaskForce = 671,

	// 修改指定作战小队类型的指定条目
	ModifyTaskForceEntry = 672,

	// 重新指定作战小队类型绑定的特遣部队
	RebindTeamTypeTaskForce = 673,

	// 恢复指定作战小队类型的内容到初始状态
	RestoreTaskForce = 674,

	// 恢复所有作战小队类型的内容到初始状态
	RestoreAllTaskForces = 675,

	// 恢复指定作战小队类型绑定的特遣部队到初始状态
	ResetTeamTypeTaskForce = 676,

	// 恢复所有作战小队类型绑定的特遣部队到初始状态
	ResetAllTeamTypeTaskForces = 677,

	// 将指定分组的指定所属方的所有单位加入指定小队类型
	RecruitGroupToTeam = 678,

	// 解除指定所属方所有已部署的单位
	UndeployHouseUnits = 679,

	// 设置简单难度最佳时间
	SetParTimeEasy = 669,

	// 设置普通难度最佳时间
	SetParTimeMedium = 680,

	// 设置困难难度最佳时间
	SetParTimeDifficult = 681,

	// 设置游戏速度
	SetGameSpeed = 682,

	// 禁止读档
	DisableLoadGame = 683,

	// 禁止存档
	DisableSaveGame = 684,

	// 恢复读档
	EnableLoadGame = 685,

	// 恢复存档
	EnableSaveGame = 686,

	// 变卖指定所属方的全部建筑...
	SellAllBuildingsOfHouse = 687,

	// ---- 触发组 / 随机触发 Actions (688-693) ----

	// 随机允许触发(按触发组)...
	RandomEnableTriggersByGroup = 688,

	// 随机禁止触发(按触发组)...
	RandomDisableTriggersByGroup = 689,

	// 随机允许触发(按名称)...
	RandomEnableTriggersByName = 690,

	// 随机禁止触发(按名称)...
	RandomDisableTriggersByName = 691,

	// 向触发组添加触发(按ID)...
	AddTriggerToGroupById = 692,

	// 从触发组移除触发(按ID)...
	RemoveTriggerFromGroupById = 693,

	// ---- AI 触发开关 (694-695) ----

	// 允许指定 AI 触发...
	EnableAITriggerById = 694,

	// 禁止指定 AI 触发...
	DisableAITriggerById = 695,

	// ---- AttachEffect Actions (696-699, 1100-1105) ----
	ApplyAttachEffectToTeamType = 696,
	RemoveAttachEffectFromTeamType = 697,
	RemoveAttachEffectByGroupFromTeamType = 698,
	RemoveAllAttachEffectsFromTeamType = 699,

	// 从 1100 起编号: 700~ 段留给其它 DLL, 避免编号撞车
	ApplyAttachEffectToAssociatedTechnos = 1100,
	RemoveAttachEffectFromAssociatedTechnos = 1101,
	RemoveAttachEffectByGroupFromAssociatedTechnos = 1102,
	RemoveAllAttachEffectsFromAssociatedTechnos = 1103,
	ApplyAttachEffectToHouseTechnos = 1104,
	RemoveAttachEffectFromHouseTechnos = 1105,

	// ---- 资金连锁 Actions (1106-1107) ----

	// 移除指定所属方的资金连锁，此后其资金独立...
	RemoveHouseMoneyPool = 1106,

	// 连结指定所属方与另一所属方的资金连锁...
	LinkHouseMoneyPools = 1107,

	// 将关联单位以指定动画显示到小地图上...
	SetAssociatedTechnosRadarMark = 1108,

	// 清除关联单位的小地图标记...
	ClearAssociatedTechnosRadarMark = 1109,

	// 在指定路径点显示小地图图标...
	SetWaypointRadarMark = 1110,

	// 移除指定路径点的小地图图标...
	ClearWaypointRadarMark = 1111,

	// 移除所有小地图标记（无需参数）...
	ClearAllRadarMarks = 1112,

	// 测试用
	// testAction = 1150,
};

class TActionExt
{
public:
	using base_type = TActionClass;

	static constexpr DWORD Canary = 0xAAAABBBB;

	class ExtData final : public Extension<TActionClass>
	{
	public:
		ExtData(TActionClass* const OwnerObject) : Extension<TActionClass>(OwnerObject)
		{ }

		virtual ~ExtData() = default;

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(ScaffoldStreamReader& Stm) override;
		virtual void SaveToStream(ScaffoldStreamWriter& Stm) override;

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	static bool Execute(TActionClass* pThis, HouseClass* pHouse,
			ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location, bool& bHandled);

#pragma push_macro("ACTION_FUNC")
#define ACTION_FUNC(name) \
static bool name(TActionClass* pThis, HouseClass* pHouse, \
	ObjectClass* pObject, TriggerClass* pTrigger, CellStruct const& location)

	ACTION_FUNC(SetWaypointTextBoxByType);
	ACTION_FUNC(SetWaypointTextBoxByData);
	ACTION_FUNC(ClearWaypointTextBox);
	ACTION_FUNC(ClearAllWaypointTextBoxs);
	ACTION_FUNC(BindAllTeamMemberToTag);
	ACTION_FUNC(BindOwnerTeamMemberToTag);
	ACTION_FUNC(BindAllTechnoTypeToTag);
	ACTION_FUNC(BindOwnerTechnoTypeToTag);
	ACTION_FUNC(GiveHouseMoney);
	ACTION_FUNC(TakeHouseMoney);
	ACTION_FUNC(SetHouseMoney);
	ACTION_FUNC(AddBaseNodeForHouseAtWaypoint);
	ACTION_FUNC(RemoveAllBaseNodeForHouseAtWaypoint);
	ACTION_FUNC(RemoveBaseNodesOfBuildingTypeForHouse);
	ACTION_FUNC(DestroyAllTagByTagTypeSafely);
	ACTION_FUNC(BindTagToTechnoTypeAtWaypoint);
	ACTION_FUNC(BindTagToTechnoTypeOfHouseAtWaypoint);
	ACTION_FUNC(BindTagToSpecificTechnoTypeWithinWaypointRange);
	ACTION_FUNC(BindTagToSpecificTechnoTypeOfSpecificOwnerWithinWaypointRange);
	ACTION_FUNC(BindTagToAllTechnoTypesWithinWaypointRange);
	ACTION_FUNC(BindTagToAllTechnoTypesOfSpecificOwnerWithinWaypointRange);
	ACTION_FUNC(UnifyAllInstancesOfSameTagType);
	ACTION_FUNC(SetRecruitableForFoot);
	ACTION_FUNC(BindTagsToAllTechTypesInWaypointRangeExceptSpecified);
	ACTION_FUNC(BindTagsToAllTechTypesOfTriggerOwnerInWaypointRangeExceptSpecified);
	ACTION_FUNC(UpdateAllBuildingAnims);
	ACTION_FUNC(UpdateAssociatedBuildingsAnims);
	ACTION_FUNC(UpdateOwnerBuildingsAnimations);
	ACTION_FUNC(CreateTeamConsideringLimits);
	ACTION_FUNC(RecruitNearbyFootToTeam);
	ACTION_FUNC(SetUnitTextBoxByTriggerType);
	ACTION_FUNC(SetUnitTextBoxByTriggerData);
	ACTION_FUNC(SetUnitTextBoxByTeamType);
	ACTION_FUNC(SetUnitTextBoxByTeamData);
	ACTION_FUNC(ClearUnitTextBoxByType);
	ACTION_FUNC(ClearUnitTextBoxByTag);
	ACTION_FUNC(ClearUnitTextBoxByTechType);
	ACTION_FUNC(ClearUnitTextBoxByHouseAndType);
	ACTION_FUNC(ClearUnitTextBoxByTeam);
	ACTION_FUNC(ClearAllUnitTextBoxs);
	ACTION_FUNC(ClearAllTextBoxs);

	// ---- ChoiceBox Actions ----
	ACTION_FUNC(SetWaypointChoiceBox);
	ACTION_FUNC(SetScreenChoiceBox);
	ACTION_FUNC(ClearChoiceBoxByID);
	ACTION_FUNC(ClearAllChoiceBoxs);

	// ---- Script Manipulation Actions ----
	ACTION_FUNC(ClearScript);
	ACTION_FUNC(CopyScript);
	ACTION_FUNC(ModifyScriptByParam);
	ACTION_FUNC(ModifyScriptByLocalVar);
	ACTION_FUNC(ModifyScriptByGlobalVar);
	ACTION_FUNC(RebindTeamTypeScript);
	ACTION_FUNC(ResetTeamTypeScript);
	ACTION_FUNC(ResetAllTeamTypeScripts);
	ACTION_FUNC(RestoreScriptContent);
	ACTION_FUNC(RestoreAllScriptContents);
	ACTION_FUNC(SeekTeamTypeScript);
	ACTION_FUNC(SetTeamTypeMaxValue);

	// ---- TaskForce Editing Actions ----
	ACTION_FUNC(ClearTaskForce);
	ACTION_FUNC(CopyTaskForce);
	ACTION_FUNC(ModifyTaskForceEntry);
	ACTION_FUNC(RebindTeamTypeTaskForce);
	ACTION_FUNC(RestoreTaskForce);
	ACTION_FUNC(RestoreAllTaskForces);
	ACTION_FUNC(ResetTeamTypeTaskForce);
	ACTION_FUNC(ResetAllTeamTypeTaskForces);
	ACTION_FUNC(RecruitGroupToTeam);
	ACTION_FUNC(UndeployHouseUnits);

	// 绘制关联单位移动路径
	ACTION_FUNC(RegisterFootPathVisualizer);
	ACTION_FUNC(UnregisterFootPathVisualizer);

	// ---- 任务简报 / 最佳时间 Actions ----
	ACTION_FUNC(SetMissionBriefing);
	ACTION_FUNC(SetOverParTitle);
	ACTION_FUNC(SetOverParMessage);
	ACTION_FUNC(SetUnderParTitle);
	ACTION_FUNC(SetUnderParMessage);
	ACTION_FUNC(SetParTimeEasy);
	ACTION_FUNC(SetParTimeMedium);
	ACTION_FUNC(SetParTimeDifficult);

	// 设置游戏速度
	ACTION_FUNC(SetGameSpeed);

	// 存读档权限控制 Actions
	ACTION_FUNC(DisableLoadGame);
	ACTION_FUNC(DisableSaveGame);
	ACTION_FUNC(EnableLoadGame);
	ACTION_FUNC(EnableSaveGame);

	// 变卖指定所属方的全部建筑
	ACTION_FUNC(SellAllBuildingsOfHouse);

	// ---- 触发组 / 随机触发 Actions ----
	ACTION_FUNC(RandomEnableTriggersByGroup);
	ACTION_FUNC(RandomDisableTriggersByGroup);
	ACTION_FUNC(RandomEnableTriggersByName);
	ACTION_FUNC(RandomDisableTriggersByName);
	ACTION_FUNC(AddTriggerToGroupById);
	ACTION_FUNC(RemoveTriggerFromGroupById);

	// ---- AI 触发开关 ----
	ACTION_FUNC(EnableAITriggerById);
	ACTION_FUNC(DisableAITriggerById);

	// ---- AttachEffect Actions ----
	ACTION_FUNC(ApplyAttachEffectToTeamType);
	ACTION_FUNC(RemoveAttachEffectFromTeamType);
	ACTION_FUNC(RemoveAttachEffectByGroupFromTeamType);
	ACTION_FUNC(RemoveAllAttachEffectsFromTeamType);
	ACTION_FUNC(ApplyAttachEffectToAssociatedTechnos);
	ACTION_FUNC(RemoveAttachEffectFromAssociatedTechnos);
	ACTION_FUNC(RemoveAttachEffectByGroupFromAssociatedTechnos);
	ACTION_FUNC(RemoveAllAttachEffectsFromAssociatedTechnos);
	ACTION_FUNC(ApplyAttachEffectToHouseTechnos);
	ACTION_FUNC(RemoveAttachEffectFromHouseTechnos);

	// ---- 资金连锁 Actions ----
	ACTION_FUNC(RemoveHouseMoneyPool);
	ACTION_FUNC(LinkHouseMoneyPools);

	// 小地图标记
	ACTION_FUNC(SetAssociatedTechnosRadarMark);
	ACTION_FUNC(ClearAssociatedTechnosRadarMark);
	ACTION_FUNC(SetWaypointRadarMark);
	ACTION_FUNC(ClearWaypointRadarMark);
	ACTION_FUNC(ClearAllRadarMarks);

	// 测试用
	// ACTION_FUNC(testAction);


#undef ACTION_FUNC
#pragma pop_macro("ACTION_FUNC")

	class ExtContainer final : public Container<TActionExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};
