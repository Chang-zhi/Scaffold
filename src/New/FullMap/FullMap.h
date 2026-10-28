#pragma once

/*
	全屏战术地图：高清地形画布 + 玩家标注板。

	显示层：按原版战术投影逐格绘制 TMP 高清地形画布到战术区（DSurface::Composite），
	叠加当前视野框、玩家标注（X/三角/准星/直线/箭头/矩形/椭圆/手绘/文字/测距）
	与右侧工具栏。独立的动态信息层绘制建筑地基、分类单位图标及悬停信息，
	地形层保留矿石密度纹理。原版小地图及其触发标记仍由雷达模块绘制。

	输入层：打开期间拦截游戏鼠标输入（喂给 Update 的键鼠参数改写 + 点击动作分发
	跳过），自身用 WWMouse + GetAsyncKeyState 轮询，模式同 MapChoiceBoxClass。
	放大后的无工具模式支持左键拖地图平移；白框边缘拖动相机，中键平移。

	联机：标注通过 EventExt(FullMapMark, 0x99) 广播；本机操作即时应用，Respond 端
	跳过自己已应用的事件，仅本人与盟友可见。纯显示数据，不影响逻辑层，无失步风险。

	存档：标注列表写入全局存档流（Scaffold.Ext.cpp 的 ScaffoldTypeRegistry）。
*/

#include <GeneralStructures.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

class HouseClass;
class ScaffoldStreamWriter;
class ScaffoldStreamReader;

enum class FullMapMarkKind : uint8_t
{
	Cross = 0,     // X 叉
	Triangle,      // 实心三角
	Crosshair,     // 准星（旧版保留，工具栏已移除入口）
	Line,          // 直线（两点）
	Arrow,         // 箭头（两点）
	Rect,          // 矩形框（两点）
	Ellipse,       // 椭圆（两点）
	Freehand,      // 手绘折线
	Text,          // 文字
	Distance,      // 测距线 + 距离标签

	// 战术徽章（点击放置：图标 + 短语标签）
	Attack,        // 进攻此处
	Defend,        // 防守此处
	Ore,           // 此处有矿石
	Tech,          // 此处有科技建筑
	Rally,         // 集结点
	Danger,        // 危险

	COUNT
};

// EventExt 载荷里的操作码
enum FullMapMarkOp : uint8_t
{
	FMMOp_Add = 0,
	FMMOp_Append,
	FMMOp_Remove,
	FMMOp_ClearOwn,
};

class FullMap
{
public:
	// 标注数量硬上限（INI 的 FullMap.MaxMarks 只能比它小）
	static constexpr int MaxMarks = 200;
	// 单条手绘折线点数上限
	static constexpr int MaxPoints = 120;
	// 文字长度上限（wchar 数，含结尾 0）
	static constexpr int MaxTextChars = 24;
	// 单个事件内联点数上限（EventExt payload 104 字节的约束）
	static constexpr int MaxPointsPerEvent = 22;

	// 纯 POD：无指针，整条走通用字节流序列化
	struct Mark
	{
		uint16_t Id = 0;             // 作者侧唯一序号
		FullMapMarkKind Kind = FullMapMarkKind::Cross;
		uint8_t ColorIdx = 0;        // 0 = 跟随作者方颜色；1..8 = 固定色板
		int8_t Author = -1;          // HouseClass::ArrayIndex
		int32_t PlacedFrame = 0;     // 放置逻辑帧（脉冲动画用；事件里不传，接收端取当前帧）
		CellStruct Cell {};          // 锚点 / 线段起点
		CellStruct Cell2 {};         // 线段终点（两点工具）
		uint8_t PointCount = 0;
		uint8_t TextLen = 0;         // 不含结尾 0
		uint16_t Reserved = 0;
		int16_t Points[MaxPoints][2] {};  // 手绘折线（格坐标）
		wchar_t Text[MaxTextChars] {};
	};
	static_assert(std::is_trivially_copyable_v<Mark>);

	// ---- 开关与状态 ----
	static bool IsOpen;

	// 输入钩子查询：打开、在场内、且渲染钩子仍在跑（切菜单/结算屏后自动放行）
	static bool ShouldBlockGameInput();

	static void Toggle();
	static void ForceClose();

	// 每帧入口（渲染钩子调用）：轮询输入 → 计算布局 → 绘制
	static void OnDraw();

	// 原版小地图色点（RadarClass::Render 侧栏 blit 后调用，见 Hooks.cpp）
	static void DrawMinimapMarks();

	// 战术视野内绘制标记（世界层钩子 0x6D4684 调用，FullMapMarksInWorld 控制）
	static void DrawWorldMarks();

	// 滚轮输入由 PeekMessageA 拦截转发（见 Hooks.cpp 的 InstallInputHooks）
	static void NotifyWheelDelta(short delta, bool ctrlHeld);

	// 该按键是否为"地图开关热键"（打开期间需要放行，否则关不掉地图）
	static bool IsMapToggleKey(DWORD key);

	// 拖动标记：当前鼠标指向的本人标记（用于拖动改位置），无则 nullptr
	static bool BeginDragMarkAt(Point2D screenPos);
	static void UpdateDragMarkTo(Point2D screenPos);
	static void EndDragMark();

	// 安装 gamemd IAT 拦截（GetAsyncKeyState / PeekMessageA）和窗口子类。
	static void InstallInputHooks();
	// 原版 RadarClass::GetMouseAction(0x6539D0) 的小地图左键入口。
	// 返回 true 表示本次原版左键事件应被 FullMap 消费。
	static bool HandleNativeMinimapClick(unsigned char eventFlags);

	// 正在文字输入（原生编辑行激活）——键命令抑制钩子查询
	static bool IsTypingActive();

	// 世界层右键命中测试：若屏幕坐标附近有本人的标记则删除并返回 true（消息将被吞掉）
	static bool TryEraseMarkAtScreen(Point2D pos);

	// 标记对本地玩家可见（本人或当前盟友；盟友关系变化实时生效）
	static bool IsMarkVisible(const Mark& m);

	// ---- 事件回放端（所有客户端执行；author == 本地玩家时由调用方跳过）----
	static void ApplyAdd(const Mark& mark);
	static void ApplyAppend(int8_t author, uint16_t id, const int16_t* points, uint8_t count);
	static void ApplyRemove(int8_t author, uint16_t id);
	static void ApplyClearOwn(int8_t author);

	static bool IsLocalAuthor(int8_t author);

	// ---- 存读档 ----
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
	static bool LoadGlobals(ScaffoldStreamReader& Stm);
	static bool CompleteLoad();

	// 对局清场（Scenario Clear）：清空全部状态
	static void Clear();

	// ---- 配置（RulesExt 从 [General] 读入）----
	static bool Enabled;
	static int MarkLimit;

private:
	FullMap() = default;
};
