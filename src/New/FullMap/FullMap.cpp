#include "FullMap.h"
#include "FullMapTerrain.h"
#include "FullMapObjects.h"
#include "FullMapGeometry.h"
#include "FullMapInput.h"
#include "FullMapSettings.h"
#include "FullMapPerf.h"
#include "FullMapOpenGL.h"

#include <New/RadarMark/RadarMark.h>
#include <Ext/Event/Body.h>

#include <YRPP.h>
#include <RadarClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <WWMouseClass.h>
#include <HouseClass.h>
#include <InputManagerClass.h>
#include <MapClass.h>
#include <CellClass.h>
#include <MessageListClass.h>
#include <Unsorted.h>
#include <StringTable.h>
#include <Utilities/Debug.h>
#include <Utilities/SavegameDef.h>
#include <Scaffold.h>

#include <Windows.h>
#include <Imm.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "imm32.lib")

// ============================================================================
// 内部状态
// ============================================================================

namespace
{
	// ---- 尺寸常量（像素） ----
	constexpr int Margin = 6;          // 与视野边缘的留白
	constexpr int ToolbarWidth = 54;   // 右侧工具栏宽
	constexpr int StatusHeight = 22;   // 底部状态条高
	constexpr int ToolBtnW = 46;
	constexpr int MarkRadius = 7;      // 点标记圆盘半径（固定屏幕尺寸，不随缩放膨胀）
	constexpr int EndRadius = 3;       // 两点标记端点圆半径

	// 游戏按键镜像表（sub_53EC90：dword_AA0444 置位时游戏从这里轮询按键，含鼠标键）
	constexpr DWORD KeyMirrorFlagAddr = 0xAA0444;
	constexpr DWORD KeyMirrorAddr = 0xAA0168;
	// 主循环的鼠标事件位标志
	constexpr DWORD MouseButtonFlagsAddr = 0xABCE14;

	// 脉冲动画时长（逻辑帧）：新放置标记扩散提示
	constexpr int PulseFrames = 45;
	// 双击判定：间隔与位移
	constexpr DWORD DoubleClickMs = 400;
	constexpr int DoubleClickDist = 12;
	// 拖拽平移阈值（像素）
	constexpr int PanDragDist = 5;

	constexpr double Pi = 3.14159265358979323846;

	// ---- 色板：0 = 自动（作者方颜色） ----
	const ColorStruct Palette[] = {
		{ 0, 0, 0 },        // Auto
		{ 255, 255, 255 },  // 白
		{ 255, 255, 0 },    // 黄
		{ 255, 64, 64 },    // 红
		{ 0, 255, 255 },    // 青
		{ 64, 255, 64 },    // 绿
		{ 255, 160, 0 },    // 橙
		{ 255, 0, 255 },    // 品红
		{ 80, 160, 255 },   // 蓝
	};
	constexpr int PaletteCount = sizeof(Palette) / sizeof(Palette[0]);

	// ---- 工具栏按钮 Id ----
	constexpr int BtnToolBase = 0;      // 0..9 = FullMapMarkKind
	constexpr int BtnEraser = 100;
	constexpr int BtnColorBase = 200;   // 200+i = 色板下标
	constexpr int BtnClearOwn = 300;
	constexpr int BtnClose = 400;
	constexpr int BtnBadgeCat = 500;    // 已废弃（徽章常驻底部）
	constexpr int BtnBadgeBase = 510;   // 510+i = 徽章下标（Attack 起）
	constexpr int BadgeCount = 6;       // 进攻/防守/矿石/科技/集结/危险
	constexpr int BtnZoomIn = 610;
	constexpr int BtnZoomOut = 611;
	constexpr int BtnNoTool = 600;      // 无工具（仅查看/移动视野）

	struct ButtonRect
	{
		int Id;
		RectangleStruct Rect;
	};

	struct Layout
	{
		RectangleStruct View {};
		RectangleStruct PlayableClip {};
		RectangleStruct MapArea {};    // 地图实际可绘制区域，避开工具栏与状态条
		RectangleStruct MapRect {};     // 放大雷达显示区（受缩放与平移影响）
		RectangleStruct FitRect {};     // 未缩放时的适配矩形（缩放/平移基准）
		RectangleStruct Toolbar {};
		RectangleStruct Status {};
		int SourceX = 0;
		int SourceY = 0;
		int BmpW = 0;
		int BmpH = 0;
		std::vector<ButtonRect> Buttons;
	};

	// ---- 全局状态 ----
	std::vector<FullMap::Mark> g_Marks;
	uint16_t g_NextId = 1;
	Layout g_Layout;

	// 工具状态
	FullMapMarkKind s_ToolKind = FullMapMarkKind::Cross;
	bool s_Eraser = false;
	uint8_t s_ColorIdx = 0;             // 色板下标（0=自动）
	bool s_Pending = false;             // 两点工具已取起点（两击兜底模式）
	CellStruct s_PendingCell {};
	// 两点工具的现代拖动绘制：按下记录锚点，拖动实时预览，松开落标
	bool s_DragShape = false;
	// 左键手势在按下时确定：拖动白框边缘移动相机，放大后拖动地图平移。
	bool s_ViewDrag = false;
	bool s_MapPanDrag = false;
	Point2D s_ViewDragOffset {};
	// 拖动已有标记改位置（问题 7）：记住被拖的标记与拖动起点
	FullMap::Mark* s_DragMark = nullptr;
	bool s_DragMarkEnd2 = false;      // true = 抓住终点（Cell2），false = 抓住起点（Cell）
	CellStruct s_DragMarkGrab {};     // 抓取时的原始格（用于计算偏移）
	Point2D s_DragMarkStart {};

	// 手绘
	bool s_FreehandActive = false;
	uint16_t s_FreehandId = 0;
	CellStruct s_FreehandLastCell {};
	std::vector<std::pair<int16_t, int16_t>> s_PointBuf; // 待发 Append 的点

	// 徽章常驻工具栏右下（不再需要开关/横条）
	// 工具栏滚动：分辨率过小时工具区可滚（非 Ctrl 滚轮在工具栏上滚动）
	float s_ToolbarScroll = 0.0f;
	int s_ToolbarScrollMax = 0;

	// 无工具模式：放大后左键拖地图平移，拖白框边缘移动视野，双击跳转。
	bool s_NoTool = true;

	// 文字输入：优先复用原生消息输入（MessageListClass 编辑行，联机 Enter 聊天同款，
	// 支持 IME 中文）；原生不可用时回退到内置编辑器（ASCII + Ctrl+V 粘贴）
	bool s_Typing = false;
	bool s_NativeTyping = false;
	CellStruct s_TypingAnchor {};
	std::wstring s_TextBuf;
	bool s_KeyWasDown[256] = {};

	// 地图内双击
	DWORD s_LastClickTick = 0;
	Point2D s_LastClickPos { -1000, -1000 };
	// 共用手势：关闭时由原版雷达事件驱动，打开时由 FullMap 输入层驱动。
	FullMapInput::MinimapDoubleClick s_MinimapDoubleClick;

	// Ctrl+滚轮缩放（1.0 = 适配视野）；按真实毫秒 dt 插值，避免帧率波动影响手感
	float s_Zoom = 1.0f;
	float s_ZoomTarget = 1.0f;
	bool s_ZoomUseCenter = false;
	DWORD s_LastZoomTick = 0;
	// 滚轮诊断：消息泵里只计数/记时（绝不写文件），渲染侧统一输出
	int s_WheelDiagCount = 0;
	short s_WheelDiagDelta = 0;
	DWORD s_WheelDiagTick = 0;
	DWORD s_WheelDiagLastLog = 0;
	// 卡顿看门狗：滚轮事件到达 → 渲染消费 的间隔，若很大说明整帧被别的东西阻塞
	DWORD s_WheelArriveTick = 0;
	DWORD s_PrevRenderTick = 0;
	DWORD s_MaxWheelAge = 0;
	DWORD s_MaxDrawGap = 0;
	DWORD s_PerfWindowStart = 0;
	int s_PerfFrames = 0;

	// 拖拽平移（屏幕像素偏移）与点击/拖拽判别
	Point2D s_PanOffset { 0, 0 };
	bool s_PressDown = false;
	Point2D s_PressPos {};
	bool s_PressMoved = false;
	Point2D s_LastMouse { -1000, -1000 };

	// 存档版本
	constexpr uint32_t SaveVersion = 1;

	// 输入钩子的活跃判据：渲染钩子最近一次运行的时间戳
	DWORD g_LastDrawTick = 0;

	DWORD g_PrevDrawDelta = 0;   // 上一帧绘制间隔（诊断用）

	// 左键沿检测（帧缓存，同 MapChoiceBoxClass）
	int s_MouseFrame = -1;
	bool s_LeftDown = false;
	bool s_LeftJustPressed = false;
	bool s_RightDown = false;
	bool s_RightJustPressed = false;

	bool g_ToolbarDrawing = false;

	// ============================================================================
	// 基础工具
	// ============================================================================

	RectangleStruct NativeRadarInputBounds()
	{
		// Exact bounds read by RadarClass::GetMouseAction @0x653A4D..0x653A83.
		return RadarClass::Instance.unknown_rect_149C;
	}

	Point2D NativeRadarPoint(Point2D client)
	{
		const auto point = FullMapInput::RadarPoint({ static_cast<double>(client.X), static_cast<double>(client.Y) },
			*reinterpret_cast<const uint8_t*>(0xA8EB7C) != 0,
			DSurface::ViewBounds.Width);
		return { static_cast<int>(point.X), static_cast<int>(point.Y) };
	}

	bool TrackMinimapClick(bool down, Point2D point)
	{
		const auto rect = NativeRadarInputBounds();
		return s_MinimapDoubleClick.Update(down,
			{ static_cast<double>(point.X), static_cast<double>(point.Y) },
			{ rect.X, rect.Y, rect.Width, rect.Height }, timeGetTime());
	}

	void PollMinimapClose()
	{
		auto* mouse = WWMouseClass::Instance;
		if (!mouse) return;
		// Original hover input uses these virtual getters. Unlike the native
		// button mirror, FullMap's own user32 import is not suppressed.
		const Point2D client { mouse->GetX(), mouse->GetY() };
		if (TrackMinimapClick((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, NativeRadarPoint(client)))
		{
			Debug::Log("[FullMap] minimap toggle source=fullmap-input stateBefore=1 stateAfter=0 client=(%d,%d)\n",
				client.X, client.Y);
			FullMap::ForceClose();
		}
	}


	int PlayerIndex()
	{
		auto* pPlayer = HouseClass::CurrentPlayer;
		return pPlayer ? pPlayer->ArrayIndex : -1;
	}

	HouseClass* GetHouse(int index)
	{
		if (index < 0 || index >= HouseClass::Array.Count)
			return nullptr;
		return HouseClass::Array.GetItem(index);
	}

	ColorStruct ResolveColor(const FullMap::Mark& m)
	{
		if (m.ColorIdx == 0)
		{
			if (auto* pHouse = GetHouse(m.Author))
				return pHouse->Color;

			auto* pPlayer = HouseClass::CurrentPlayer;
			return pPlayer ? pPlayer->Color : ColorStruct { 255, 255, 255 };
		}
		if (m.ColorIdx > 0 && m.ColorIdx < PaletteCount)
			return Palette[m.ColorIdx];
		return Palette[1];
	}

	FullMap::Mark* FindMark(int8_t author, uint16_t id)
	{
		for (auto& m : g_Marks)
		{
			if (m.Id == id && m.Author == author)
				return &m;
		}
		return nullptr;
	}

	void InsertToList(const FullMap::Mark& m)
	{
		g_Marks.push_back(m);
		// 超限时按最旧淘汰（各客户端事件序一致 → 结果一致）
		while (static_cast<int>(g_Marks.size()) > FullMap::MarkLimit)
			g_Marks.erase(g_Marks.begin());
	}

	uint16_t AllocId()
	{
		const uint16_t id = g_NextId;
		g_NextId = g_NextId >= 0xFFFF ? 1 : static_cast<uint16_t>(g_NextId + 1);
		return id;
	}

	bool KindHasEndPoint(FullMapMarkKind kind)
	{
		return kind == FullMapMarkKind::Line || kind == FullMapMarkKind::Arrow
			|| kind == FullMapMarkKind::Rect || kind == FullMapMarkKind::Ellipse
			|| kind == FullMapMarkKind::Distance;
	}

	// ---- 战术徽章 ----
	bool IsBadgeKind(FullMapMarkKind kind)
	{
		return kind >= FullMapMarkKind::Attack && kind <= FullMapMarkKind::Danger;
	}

	// 徽章含义色（主体颜色；玩家颜色只占边角小点，大图标下更易识别含义）
	ColorStruct BadgeMeaningColor(FullMapMarkKind kind)
	{
		switch (kind)
		{
		case FullMapMarkKind::Attack: return { 226, 62, 62 };   // 红：进攻
		case FullMapMarkKind::Defend: return { 72, 118, 226 };  // 蓝：防守
		case FullMapMarkKind::Ore: return { 238, 198, 64 };     // 金：矿石
		case FullMapMarkKind::Tech: return { 64, 188, 188 };    // 青：科技
		case FullMapMarkKind::Rally: return { 96, 198, 96 };    // 绿：集结
		case FullMapMarkKind::Danger: return { 238, 142, 44 };  // 橙：危险
		default: return { 255, 255, 255 };
		}
	}

	const wchar_t* BadgeLabel(FullMapMarkKind kind)
	{
		switch (kind)
		{
		case FullMapMarkKind::Attack: return StringTable::TryFetchString("SF:FULLMAP_B_ATK", L"进攻");
		case FullMapMarkKind::Defend: return StringTable::TryFetchString("SF:FULLMAP_B_DEF", L"防守");
		case FullMapMarkKind::Ore: return StringTable::TryFetchString("SF:FULLMAP_B_ORE", L"矿石");
		case FullMapMarkKind::Tech: return StringTable::TryFetchString("SF:FULLMAP_B_TECH", L"科技");
		case FullMapMarkKind::Rally: return StringTable::TryFetchString("SF:FULLMAP_B_RALLY", L"集结");
		case FullMapMarkKind::Danger: return StringTable::TryFetchString("SF:FULLMAP_B_DANGER", L"危险");
		default: return L"";
		}
	}

	// 徽章图形符号（白色线条，参考主流游戏图标语言：剑/盾/美刀/扳手/旗帜/感叹号）
	void DrawLine2(Point2D a, Point2D b, int colorInt);        // 定义见绘制原语段
	void DrawEllipse(RectangleStruct rc, int colorInt);        // 定义见绘制原语段
	bool IsEmpty(RectangleStruct r);                           // 定义见裁剪段
	RectangleStruct ClipRect(RectangleStruct r);               // 定义见裁剪段
	const FullMap::Mark* HitTestMark(Point2D mouse, bool onlyOwn, bool anchorsOnly = false); // 定义见交互动段
	void DrawFilledTri3(Point2D p0, Point2D p1, Point2D p2, int colorInt); // 定义见绘制原语段
	void DrawDisc(Point2D c, int r, int colorInt);                       // 定义见绘制原语段

	// 徽章字形（2px 白线，尺寸按半径 s 缩放）：
	//   攻击=剑 / 防御=盾牌 / 矿石=美刀 / 科技=双头扳手 / 集结=旗子 / 危险=感叹号
	// 徽章绘制辅助：单点写入（带视野裁剪），以及可指定粗细的线段
	void Plot(int x, int y, int colorInt)
	{
		const auto& vb = DSurface::ViewBounds;
		if (x < vb.X || x >= vb.X + vb.Width || y < vb.Y || y >= vb.Y + vb.Height)
			return;
		RectangleStruct px { x, y, 1, 1 };
		DSurface::Composite->FillRect(&px, colorInt);
	}

	// 粗线（用 t x t 方块刷，1:1 像素下比多次偏移画线更整齐）
	void PlotThickLine(Point2D a, Point2D b, int colorInt, int t)
	{
		const int n = std::max(std::abs(b.X - a.X), std::abs(b.Y - a.Y));
		for (int i = 0; i <= n; ++i)
		{
			const int x = a.X + (b.X - a.X) * i / std::max(1, n);
			const int y = a.Y + (b.Y - a.Y) * i / std::max(1, n);
			for (int ox = 0; ox < t; ++ox)
				for (int oy = 0; oy < t; ++oy)
					Plot(x + ox, y + oy, colorInt);
		}
	}

	// 任意凸/凹多边形填充（奇偶判定），用于实心图标
	void FillPolygon(const Point2D* pts, int count, int colorInt)
	{
		if (count < 3)
			return;
		int minX = pts[0].X, maxX = pts[0].X, minY = pts[0].Y, maxY = pts[0].Y;
		for (int i = 1; i < count; ++i)
		{
			minX = std::min(minX, pts[i].X); maxX = std::max(maxX, pts[i].X);
			minY = std::min(minY, pts[i].Y); maxY = std::max(maxY, pts[i].Y);
		}
		auto edge = [](Point2D a, Point2D b, int x, int y)
		{
			return static_cast<long long>(b.X - a.X) * (y - a.Y)
				- static_cast<long long>(b.Y - a.Y) * (x - a.X);
		};

		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				int sign = 0;
				bool inside = true;
				for (int i = 0; i < count; ++i)
				{
					const long long v = edge(pts[i], pts[(i + 1) % count], x, y);
					if (v == 0)
						continue;
					const int s2 = v > 0 ? 1 : -1;
					if (sign == 0)
						sign = s2;
					else if (sign != s2)
					{
						inside = false;
						break;
					}
				}
				if (inside)
					Plot(x, y, colorInt);
			}
		}
	}

	// 美元符号：竖线 + 上下横 + 中间 S 形实心块
	void DrawBadgeDollar(Point2D c, int s, int colorInt)
	{
		const int w = std::max(1, s / 5);
		// 竖线（贯穿）
		for (int y = -s; y <= s; ++y)
			for (int x = -w; x <= w; ++x)
				Plot(c.X + x, c.Y + y, colorInt);
		// 上横、下横
		for (int x = -s * 3 / 5; x <= s * 3 / 5; ++x)
		{
			for (int t = 0; t < w + 1; ++t)
			{
				Plot(c.X + x, c.Y - s + t, colorInt);
				Plot(c.X + x, c.Y + s - t, colorInt);
			}
		}
		// S 形斜杠：用实心圆点串成粗线，避免细线在小尺寸发虚
		const int n = std::max(6, s * 2);
		for (int i = 0; i <= n; ++i)
		{
			const int x = -s * 3 / 5 + (s * 6 / 5) * i / n;
			const int y = -s + (s * 2) * i / n;
			DrawDisc({ c.X + x, c.Y + y }, std::max(1, s / 9), colorInt);
		}
	}

	// 扳手头：开口环（用实心圆环 + 挖掉一个缺口）
	void DrawBadgeWrenchHead(Point2D c, int ox, int oy, int r, int colorInt)
	{
		const int hx = c.X + ox;
		const int hy = c.Y + oy;
		for (int y = -r; y <= r; ++y)
		{
			for (int x = -r; x <= r; ++x)
			{
				const int d2 = x * x + y * y;
				if (d2 > r * r || d2 < (r - 2) * (r - 2))
					continue;
				// 朝外的缺口
				const bool gap = (ox < 0) ? (x > 0 && std::abs(y) < r) : (x < 0 && std::abs(y) < r);
				if (gap)
					continue;
				Plot(hx + x, hy + y, colorInt);
			}
		}
	}

	// 徽章字形：15x15 手工像素位图（经典游戏图标做法）。
	// 小尺寸下像素图比矢量图形更可控——每个像素都经过设计，不会出现"糊成一团"。
	static const char* const s_Glyphs[6][15] = {
		// 0 攻击：斜置长剑
		{ "............##.", "...........###.", "..........###..", ".........###...",
		  "........###....", ".......###.....", "..##..###......", "..######.......",
		  "...####........", "....###........", "....##.........", "...##..........",
		  "..##...........", "...............", "..............." },
		// 1 防御：纹章盾
		{ ".#############.", ".#############.", ".#############.", ".#############.",
		  "..###########..", "..###########..", "..###########..", "...#########...",
		  "...#########...", "....#######....", "....#######....", ".....#####.....",
		  "......###......", ".......#.......", "..............." },
		// 2 矿石：美元符号
		{ "......###......", ".....#####.....", "....##.#.##....", "...##..#..##...",
		  "...##..#.......", "....##.#.......", ".....###.......", "......###......",
		  ".......###.....", ".......#.##....", "...##..#..##...", "....##.#.###...",
		  ".....#####.....", "......###......", "......###......" },
		// 3 科技：45° 开口扳手（两个开口环头 + 斜柄）
		{ "...............", "...............", "....##.........", "....###........",
		  "..##..##.......", "..##..##.......", "...#####.......", "....#######....",
		  ".......#####...", ".......##..##..", ".......##..##..", "........###....",
		  ".........##....", "...............", "..............." },
		// 4 集结：旗帜
		{ "..##...........", "..##########...", "..###########..", "..############.",
		  "..###########..", "..##########...", "..#########....", "..########.....",
		  "..#######......", "..######.......", "..#####........", "..####.........",
		  "..##...........", "..##...........", "..##..........." },
		// 5 危险：感叹号
		{ "......###......", "......###......", ".....#####.....", ".....#####.....",
		  ".....#####.....", ".....#####.....", ".....#####.....", "......###......",
		  "......###......", "...............", ".....#####.....", ".....#####.....",
		  "......###......", "...............", "..............." },
	};

	int BadgeGlyphIndex(FullMapMarkKind kind)
	{
		switch (kind)
		{
		case FullMapMarkKind::Attack: return 0;
		case FullMapMarkKind::Defend: return 1;
		case FullMapMarkKind::Ore: return 2;
		case FullMapMarkKind::Tech: return 3;
		case FullMapMarkKind::Rally: return 4;
		case FullMapMarkKind::Danger: return 5;
		default: return -1;
		}
	}

	// 按像素绘制：15x15 位图，按半径 s 做整数倍缩放（s>=8 时 1:1，更大时最近邻放大）
	void DrawBadgeGlyph(FullMapMarkKind kind, Point2D c, int s, int colorInt)
	{
		const int idx = BadgeGlyphIndex(kind);
		if (idx < 0)
			return;

		// 位图 15x15，希望占用的屏幕边长约 2*s*0.85
		const int target = std::max(9, s * 2 * 17 / 20);
		const int scale = std::max(1, target / 15);
		const int glyphSize = 15 * scale;
		const int ox = c.X - glyphSize / 2;
		const int oy = c.Y - glyphSize / 2;

		for (int gy = 0; gy < 15; ++gy)
		{
			const char* row = s_Glyphs[idx][gy];
			for (int gx = 0; gx < 15; ++gx)
			{
				if (row[gx] != '#')
					continue;
				for (int sy = 0; sy < scale; ++sy)
					for (int sx = 0; sx < scale; ++sx)
						Plot(ox + gx * scale + sx, oy + gy * scale + sy, colorInt);
			}
		}
	}

	// 徽章扩散环：像水波一样向外扩散并逐渐变淡。
	// 用"相位错开的多个环 + 半径线性增长 + 透明度随半径衰减"实现，
	// 环走到最大半径时透明度已≈0，所以看不到循环重置的突变。
	constexpr int BadgeRingPeriod = 110;   // 每个环一轮的逻辑帧
	constexpr int BadgeRingMax = 52;       // 最大半径（扩散范围）
	constexpr int BadgeRingCount = 3;      // 同屏环数量（相位均匀错开）

	// 返回 0..BadgeRingMax 的半径
	int BadgeRingRadius(int frame, int phase)
	{
		const int t = (frame + phase) % BadgeRingPeriod;
		return t * BadgeRingMax / BadgeRingPeriod;
	}

	// 返回该半径处的透明度（0..100）：越往外越淡，到边缘完全消失
	int BadgeRingOpacity(int radius)
	{
		const int pct = 100 - radius * 100 / std::max(1, BadgeRingMax);
		// 前段保持较实、后段快速淡出，观感更接近真实波纹
		return std::clamp(pct * pct / 100, 0, 100);
	}

	// 标记变化时戳一下原版雷达的脏点表：否则视野不动时 RadarClass::Render
	// 不会重绘，侧栏小地图上的标记就得不到实时刷新
	Point2D CellToRadarPx(CellStruct cell); // 定义见下文坐标段

	void PokeMinimap(const FullMap::Mark& m)
	{
		auto& radar = RadarClass::Instance;
		const auto& box = radar.unknown_rect_149C;
		const int w = std::max(1, box.Width);
		const int h = std::max(1, box.Height);

		Point2D pts[2] = { CellToRadarPx(m.Cell), CellToRadarPx(m.Cell2) };
		for (Point2D& p : pts)
		{
			p.X = std::clamp(p.X, 0, w - 1);
			p.Y = std::clamp(p.Y, 0, h - 1);
			radar.RefreshCrd(&p);
		}
	}

	// 本机提交：本地立即生效 + 广播事件（Respond 端跳过作者本人）
	void CommitAdd(const FullMap::Mark& m)
	{
		InsertToList(m);
		PokeMinimap(m);
		EventExt::RaiseFullMapMarkAdd(m);
	}

	void CommitRemove(const FullMap::Mark& m)
	{
		PokeMinimap(m);
		EventExt::RaiseFullMapMarkRemove(m.Id);
		if (auto* pMark = FindMark(m.Author, m.Id))
			g_Marks.erase(g_Marks.begin() + (pMark - g_Marks.data()));
	}

	void SelectViewTool()
	{
		s_NoTool = true;
		s_Eraser = false;
		s_Pending = false;
		s_FreehandActive = false;
		s_FreehandId = 0;
		s_PointBuf.clear();
		s_Typing = false;
		s_TextBuf.clear();
		if (s_NativeTyping)
			MessageListClass::Instance.RemoveEdit();
		s_NativeTyping = false;
	}

	void ResetInteractionState()
	{
		SelectViewTool();
		s_DragMark = nullptr;
		s_DragMarkEnd2 = false;
		s_LastClickTick = 0;
		s_LastClickPos = { -1000, -1000 };
		s_PressDown = false;
		s_PressMoved = false;
		s_DragShape = false;
		s_ViewDrag = false;
		s_MapPanDrag = false;
		s_PanOffset = { 0, 0 };
		s_LastZoomTick = 0;
		s_ZoomUseCenter = false;
		s_LastMouse = { -1000, -1000 };
	}

	// ============================================================================
	// 雷达位图与坐标
	// ============================================================================

	Surface* GetRadarImage(int& w, int& h)
	{
		auto& radar = RadarClass::Instance;
		auto* pImage = reinterpret_cast<Surface*>(radar.unknown_121C);
		if (!pImage)
			return nullptr;

		// 尺寸直接取自表面自身（Radar+0x14C4/0x14C8 的语义并不可靠，
		// 实测存在雷达可用但这两个字段非尺寸的情况）
		const RectangleStruct rc = pImage->GetRect();
		w = rc.Width;
		h = rc.Height;
		if (w <= 0 || h <= 0)
		{
			w = pImage->Width;
			h = pImage->Height;
		}
		if (w <= 0 || h <= 0 || w > 2048 || h > 2048)
		{
			Debug::Log("[FullMap] radar surf %p but bad dims %dx%d (fields 14C4=%u 14C8=%u)\n",
				static_cast<void*>(pImage), w, h,
				radar.unknown_14C4, radar.unknown_14C8);
			return nullptr;
		}
		return pImage;
	}

	// 侧栏标记始终走原版的雷达变换；高清画布采用独立的战术层坐标。
	Point2D CellToRadarPx(CellStruct cell)
	{
		CoordStruct coords { cell.X * 256 + 128, cell.Y * 256 + 128, 0 };
		Point2D result {};
		RadarClass::Instance.GetCrdOnRadar(&result, &coords, false);
		return result;
	}

	bool TerrainActive()
	{
		return !FullMapSettings::UseNativeRadar && FullMapTerrain::IsReady();
	}

	Point2D CellToSourcePx(CellStruct cell)
	{
		if (TerrainActive())
		{
			int px = 0, py = 0;
			FullMapTerrain::CellToCanvas(cell.X, cell.Y, px, py);
			return Point2D { px, py };
		}
		return CellToRadarPx(cell);
	}

	// Map source is selected by the local preference; exactly one source is active.
	struct MapSource
	{
		const unsigned short* Pixels = nullptr;
		int W = 0;
		int H = 0;
		int Pitch = 0;    // WORD 数
		bool Owned = false;   // true = 画布（本模块自有内存，不需要拷贝缓存）
	};

	// 高清地形画布：首次打开或地图变化时按帧预算增量构建
	constexpr int TerrainBudgetCells = 900;
	constexpr int FogBudgetCells = 4000;
	constexpr int FogBudgetRows = 96;

	void PumpTerrain()
	{
		if (FullMapSettings::UseNativeRadar) return;
		if (!FullMapTerrain::IsReady() && !FullMapTerrain::IsBuilding())
		{
			if (!FullMapTerrain::Build())
				return;
		}
		if (FullMapTerrain::IsBuilding())
			FullMapTerrain::BuildIncremental(TerrainBudgetCells);

		FullMapTerrain::RefreshFog(FogBudgetCells);
		FullMapTerrain::PumpCompose(FogBudgetRows);
	}

	MapSource AcquireMapSource()
	{
		FullMapPerf::Scope timing(FullMapPerf::Stage::Acquire);
		MapSource src {};

		// Custom and native-radar modes are mutually exclusive.
		if (!FullMapSettings::UseNativeRadar)
		{
			if (!TerrainActive()) return src;
			src.Pixels = FullMapTerrain::Pixels();
			src.W = FullMapTerrain::CanvasW;
			src.H = FullMapTerrain::CanvasH;
			src.Pitch = FullMapTerrain::CanvasW;
			src.Owned = true;
			return src;
		}

		// Native radar mode: reuse the actual game surface, including native visibility.
		int w = 0, h = 0;
		Surface* pRadar = GetRadarImage(w, h);
		if (pRadar && pRadar->GetBytesPerPixel() == 2)
		{
			if (void* pBase = pRadar->Lock(0, 0))
			{
				src.Pixels = static_cast<const unsigned short*>(pBase);
				src.W = w;
				src.H = h;
				src.Pitch = pRadar->GetPitch() / 2;
			}
			else
			{
				Debug::Log("[FullMap] radar surface Lock failed (%dx%d)\n", w, h);
			}
		}
		return src;
	}

	// 格 → 放大图屏幕坐标
	Point2D CellToScreen(CellStruct cell)
	{
		const auto& L = g_Layout;
		const Point2D px = CellToSourcePx(cell);
		return Point2D {
			L.MapRect.X + (px.X - L.SourceX) * L.MapRect.Width / std::max(1, L.BmpW),
			L.MapRect.Y + (px.Y - L.SourceY) * L.MapRect.Height / std::max(1, L.BmpH)
		};
	}

	bool ScreenToCell(Point2D pt, CellStruct& out)
	{
		const auto& L = g_Layout;
		if (pt.X < L.PlayableClip.X || pt.Y < L.PlayableClip.Y
			|| pt.X >= L.PlayableClip.X + L.PlayableClip.Width
			|| pt.Y >= L.PlayableClip.Y + L.PlayableClip.Height) return false;
		if (pt.X < L.MapRect.X || pt.X >= L.MapRect.X + L.MapRect.Width
			|| pt.Y < L.MapRect.Y || pt.Y >= L.MapRect.Y + L.MapRect.Height)
			return false;

		Point2D bmp {
			(pt.X - L.MapRect.X) * L.BmpW / std::max(1, L.MapRect.Width),
			(pt.Y - L.MapRect.Y) * L.BmpH / std::max(1, L.MapRect.Height)
		};
		bmp.X += L.SourceX;
		bmp.Y += L.SourceY;

		if (TerrainActive())
		{
			short cx = 0, cy = 0;
			if (!FullMapTerrain::CanvasToCell(bmp.X, bmp.Y, cx, cy))
				return false;
			out = CellStruct { cx, cy };
			return true;
		}

		out = RadarMark::ToCell(bmp);
		return true;
	}

	// 高清画布直接使用真实战术相机；侧栏的小框有低分辨率取整和边界钳制，
	// 不能按小地图尺寸放大成当前视野。0x6D8B30 使用同一个战术视口尺寸。
	RectangleStruct GetViewRectInSource()
	{
		if (TerrainActive())
		{
			if (const auto* tactical = TacticalClass::Instance)
			{
				DEFINE_NONSTATIC_REFERENCE(RectangleStruct, tacticalBounds, 0xB0CE28);
				const RectangleStruct source { tactical->TacticalPos.X, tactical->TacticalPos.Y,
					tacticalBounds.Width, tacticalBounds.Height };
				RectangleStruct canvas {};
				FullMapTerrain::TacticalRectToCanvas(source, canvas);
				return canvas;
			}
			return {};
		}
		auto& radar = RadarClass::Instance;
		RectangleStruct ret = radar.unknown_rect_14DC;
		ret.X -= radar.unknown_rect_149C.X;
		ret.Y -= radar.unknown_rect_149C.Y;
		return ret;
	}

	// ---- 视野区裁剪：任何绘制不得越出 ViewBounds（否则污染侧栏/任务栏）----

	bool IsEmpty(RectangleStruct r) { return r.Width <= 0 || r.Height <= 0; }

	RectangleStruct ClipRect(RectangleStruct r)
	{
		const auto& v = g_ToolbarDrawing || !FullMap::IsOpen
			? DSurface::ViewBounds : g_Layout.PlayableClip;
		const int x0 = std::max(r.X, v.X);
		const int y0 = std::max(r.Y, v.Y);
		const int x1 = std::min(r.X + r.Width, v.X + v.Width);
		const int y1 = std::min(r.Y + r.Height, v.Y + v.Height);
		if (x1 <= x0 || y1 <= y0)
			return { 0, 0, 0, 0 };
		return { x0, y0, x1 - x0, y1 - y0 };
	}

	// Liang-Barsky 直线裁剪
	bool ClipLine(Point2D& a, Point2D& b)
	{
		const auto& v = g_ToolbarDrawing || !FullMap::IsOpen
			? DSurface::ViewBounds : g_Layout.PlayableClip;
		if (IsEmpty(v)) return false;
		const double xmin = v.X, xmax = v.X + v.Width - 1.0;
		const double ymin = v.Y, ymax = v.Y + v.Height - 1.0;
		double t0 = 0.0, t1 = 1.0;
		const double dx = static_cast<double>(b.X) - a.X, dy = static_cast<double>(b.Y) - a.Y;
		const double edges[4][4] = {
			{ -dx, a.X - xmin, 0, 0 }, { dx, xmax - a.X, 0, 0 },
			{ -dy, a.Y - ymin, 0, 0 }, { dy, ymax - a.Y, 0, 0 } };
		for (const auto& e : edges)
		{
			const double p = e[0], q = e[1];
			if (p == 0.0)
			{
				if (q < 0.0)
					return false;
			}
			else
			{
				const double t = q / p;
				if (p < 0.0)
				{
					if (t > t1) return false;
					if (t > t0) t0 = t;
				}
				else
				{
					if (t < t0) return false;
					if (t < t1) t1 = t;
				}
			}
		}
		// 注意必须基于原始端点计算：先把 a 改写再用 a 求 b 会把线段平移拉长
		const double ax = a.X, ay = a.Y;
		a = { static_cast<int>(std::clamp(std::round(ax + t0 * dx), xmin, xmax)),
			static_cast<int>(std::clamp(std::round(ay + t0 * dy), ymin, ymax)) };
		b = { static_cast<int>(std::clamp(std::round(ax + t1 * dx), xmin, xmax)),
			static_cast<int>(std::clamp(std::round(ay + t1 * dy), ymin, ymax)) };
		return true;
	}

	// 单像素线（自动裁剪到视野区）。贴边线段可能只剩一个交点，
	// 此时绘制裁剪后的交点，避免使用原端点或中点而漏掉边界像素。
	void DrawLine1(Point2D a, Point2D b, int colorInt)
	{
		if (!ClipLine(a, b))
			return;

		if (a.X == b.X && a.Y == b.Y)
		{
			// Use the clipped intersection, not the offscreen original midpoint.
			RectangleStruct px { a.X, a.Y, 1, 1 };
			px = ClipRect(px);
			if (!IsEmpty(px))
				DSurface::Composite->FillRect(&px, colorInt);
			return;
		}

		DSurface::Composite->DrawLine(&a, &b, colorInt);
	}

	// ============================================================================
	// 绘制原语
	// ============================================================================

	void DrawLine2(Point2D a, Point2D b, int colorInt)
	{
		// 3px 粗线：再补两像素偏移，细线在大地图上太容易看不清
		DrawLine1(a, b, colorInt);
		DrawLine1({ a.X + 1, a.Y }, { b.X + 1, b.Y }, colorInt);
		DrawLine1({ a.X + 2, a.Y }, { b.X + 2, b.Y }, colorInt);
	}

	// Clip the actual edges, not the bounding box. Intersecting the box first
	// invents borders at the viewport edge and loses offscreen geometry.
	void DrawRectangleOutline(RectangleStruct rc, int colorInt)
	{
		if (IsEmpty(rc)) return;
		const int right = rc.X + rc.Width - 1;
		const int bottom = rc.Y + rc.Height - 1;
		DrawLine1({ rc.X, rc.Y }, { right, rc.Y }, colorInt);
		DrawLine1({ right, rc.Y }, { right, bottom }, colorInt);
		DrawLine1({ right, bottom }, { rc.X, bottom }, colorInt);
		DrawLine1({ rc.X, bottom }, { rc.X, rc.Y }, colorInt);
	}

	void DrawEllipse(RectangleStruct rc, int colorInt)
	{
		constexpr int Seg = 24;
		Point2D pts[Seg];
		const double cx = rc.X + rc.Width / 2.0;
		const double cy = rc.Y + rc.Height / 2.0;
		const double rx = std::max(1.0, rc.Width / 2.0);
		const double ry = std::max(1.0, rc.Height / 2.0);
		for (int i = 0; i < Seg; ++i)
		{
			const double t = 2.0 * Pi * i / Seg;
			pts[i] = Point2D { static_cast<int>(cx + rx * std::cos(t)), static_cast<int>(cy + ry * std::sin(t)) };
		}
		for (int i = 0; i < Seg; ++i)
			DrawLine2(pts[i], pts[(i + 1) % Seg], colorInt);
	}

	// 实心三角：尖头朝上（顶点在上、底边在下），锚点是顶点。
	void DrawFilledTriangle(Point2D top, int halfW, int height, int colorInt)
	{
		for (int i = 0; i < height; ++i)
		{
			// 从顶点向下逐行加宽
			const int w = std::max(1, halfW * 2 * (i + 1) / height);
			RectangleStruct strip { top.X - w / 2, top.Y + i, w, 1 };
			strip = ClipRect(strip);
			if (!IsEmpty(strip))
				DSurface::Composite->FillRect(&strip, colorInt);
		}
	}

	// 圆形柔光：由内向外逐层降低不透明度，避免方形边缘
	void DrawGlowDisc(Point2D c, int radius, ColorStruct color)
	{
		auto* pSurf = DSurface::Composite;
		if (!pSurf || radius <= 0)
			return;

		constexpr int Layers = 5;
		for (int i = 0; i < Layers; ++i)
		{
			const int r = radius - radius * i / Layers;
			if (r <= 0)
				continue;
			const int alpha = 14 + i * 4;   // 外圈更淡
			RectangleStruct rc { c.X - r, c.Y - r, r * 2, r * 2 };
			rc = ClipRect(rc);
			if (IsEmpty(rc))
				continue;
			pSurf->FillRectTrans(&rc, &color, alpha);
		}
	}

	// 半透明椭圆描边：Surface 没有 alpha 线接口，这里自己按 alpha 混合写点。
	// 只用于徽章的扩散环（每帧几个环，开销可控）。
	void DrawEllipseFade(Point2D c, int rx, int ry, int colorInt, int opacity)
	{
		if (rx <= 0 || ry <= 0 || opacity <= 0)
			return;
		if (opacity > 100)
			opacity = 100;

		auto* pSurf = DSurface::Composite;
		if (!pSurf || pSurf->GetBytesPerPixel() != 2)
		{
			// 非 16 位：退回不透明细线
			RectangleStruct rc { c.X - rx, c.Y - ry, rx * 2, ry * 2 };
			DrawEllipse(rc, colorInt);
			return;
		}

		void* pBase = pSurf->Lock(0, 0);
		if (!pBase)
			return;

		unsigned short* pWords = static_cast<unsigned short*>(pBase);
		const int pitch = pSurf->GetPitch() / 2;
		const auto& vb = DSurface::ViewBounds;

		// 目标色拆解：colorInt 是游戏色深的 16 位值，必须按 Drawing 的位移还原，
		// 直接当成 RGB888 取高字节会得到错误颜色（之前所有环都变成绿色就是这里）
		BYTE sr = 0, sg = 0, sb = 0;
		Drawing::Int_To_RGB(colorInt, sr, sg, sb);

		constexpr int Seg = 96;
		auto plot = [&](int px, int py)
		{
			if (px < vb.X || px >= vb.X + vb.Width || py < vb.Y || py >= vb.Y + vb.Height)
				return;
			unsigned short& dst = pWords[py * pitch + px];
			BYTE dr = 0, dg = 0, db = 0;
			Drawing::Int_To_RGB(dst, dr, dg, db);
			const int r = (sr * opacity + dr * (100 - opacity)) / 100;
			const int g = (sg * opacity + dg * (100 - opacity)) / 100;
			const int b = (sb * opacity + db * (100 - opacity)) / 100;
			dst = static_cast<unsigned short>(Drawing::RGB_To_Int(r, g, b));
		};

		for (int i = 0; i < Seg; ++i)
		{
			const double t = 2.0 * Pi * i / Seg;
			const int px = c.X + static_cast<int>(rx * std::cos(t));
			const int py = c.Y + static_cast<int>(ry * std::sin(t));
			plot(px, py);
			// 加倍描边厚度，小环也看得见
			plot(px + 1, py);
		}

		pSurf->Unlock();
		(void)sr; (void)sg; (void)sb;
	}

	// 任意三角形填充（扫描线+重心符号判定，图标尺度下开销可忽略）
	void DrawFilledTri3(Point2D p0, Point2D p1, Point2D p2, int colorInt)
	{
		const int x0 = std::min({ p0.X, p1.X, p2.X });
		const int x1 = std::max({ p0.X, p1.X, p2.X });
		const int y0 = std::min({ p0.Y, p1.Y, p2.Y });
		const int y1 = std::max({ p0.Y, p1.Y, p2.Y });

		auto edge = [](Point2D a, Point2D b, int px, int py)
		{
			return static_cast<long long>(b.X - a.X) * (py - a.Y)
				- static_cast<long long>(b.Y - a.Y) * (px - a.X);
		};
		const long long e0 = edge(p0, p1, p2.X, p2.Y);

		// 扫描线：直接算每行的跨度，避免逐像素 FillRect（箭头/图标每帧都要画）
		for (int y = y0; y <= y1; ++y)
		{
			int lo = INT_MAX, hi = INT_MIN;
			for (int x = x0; x <= x1; ++x)
			{
				const long long a0 = edge(p1, p2, x, y);
				const long long a1 = edge(p2, p0, x, y);
				const long long a2 = edge(p0, p1, x, y);
				if ((a0 >= 0 && a1 >= 0 && a2 >= 0) || (a0 <= 0 && a1 <= 0 && a2 <= 0))
				{
					if (x < lo) lo = x;
					if (x > hi) hi = x;
				}
			}
			if (hi >= lo)
			{
				RectangleStruct strip { lo, y, hi - lo + 1, 1 };
				strip = ClipRect(strip);
				if (!IsEmpty(strip))
					DSurface::Composite->FillRect(&strip, colorInt);
			}
		}
		(void)e0;
	}

	// 实心箭头：箭尖三角 + 2px 杆（比两条分叉线醒目得多）
	void DrawArrowHead(Point2D tip, Point2D from, int size, int colorInt)
	{
		const double ang = std::atan2(static_cast<double>(tip.Y - from.Y), static_cast<double>(tip.X - from.X));
		constexpr double HalfSpread = 0.42;
		const Point2D p1 {
			tip.X - static_cast<int>(size * std::cos(ang + HalfSpread)),
			tip.Y - static_cast<int>(size * std::sin(ang + HalfSpread)) };
		const Point2D p2 {
			tip.X - static_cast<int>(size * std::cos(ang - HalfSpread)),
			tip.Y - static_cast<int>(size * std::sin(ang - HalfSpread)) };
		DrawFilledTri3(tip, p1, p2, colorInt);
	}

	void PrintText(const wchar_t* text, Point2D pos, int colorInt)
	{
		if (!text || !*text)
			return;
		RectangleStruct bounds = g_ToolbarDrawing || !FullMap::IsOpen
			? DSurface::ViewBounds : g_Layout.PlayableClip;
		if (IsEmpty(bounds)) return;
		pos.X -= bounds.X; pos.Y -= bounds.Y;
		DSurface::Composite->DrawText(text, &bounds, &pos, colorInt, 0,
			TextPrintType::Metal12 | TextPrintType::BrightColor);
	}

	// 带衬底的文字（地图标注用，保证复杂底色上可读）
	void PrintTextWithBacking(const wchar_t* text, Point2D pos, int colorInt)
	{
		if (!text || !*text)
			return;
		RectangleStruct dims = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 2, 0);
		if (dims.Width <= 0) dims.Width = 6 * static_cast<int>(wcslen(text));
		RectangleStruct back { pos.X - 2, pos.Y - 1, dims.Width + 4, std::max(12, dims.Height) };
		back = ClipRect(back);
		if (!IsEmpty(back))
		{
			ColorStruct black { 0, 0, 0 };
			DSurface::Composite->FillRectTrans(&back, &black, 45);
		}
		PrintText(text, pos, colorInt);
	}

	// 实心圆盘（屏幕空间固定尺寸，不随地图缩放膨胀；逐行裁剪到视野区）
	void DrawDisc(Point2D c, int r, int colorInt)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			const int w = static_cast<int>(std::sqrt(static_cast<double>(r) * r - dy * dy));
			RectangleStruct strip { c.X - w, c.Y + dy, w * 2 + 1, 1 };
			strip = ClipRect(strip);
			if (!IsEmpty(strip))
				DSurface::Composite->FillRect(&strip, colorInt);
		}
	}

	// ============================================================================
	// 标记绘制
	// 现代约定（参考 SC2 ping / AoE flare 的社区反馈）：
	//   * 身份永远用作者所属方颜色（圆盘）——多人协同时区分"谁标的"是第一信息
	//   * 色板选择的颜色只用于线条/图形（表达意图）
	//   * 本人的标记白描边，盟友黑描边，一眼可辨
	//   * 固定屏幕尺寸 + 新标记短脉冲，不随地图缩放变大
	// ============================================================================

	void DrawMark(const FullMap::Mark& m, bool hovered = false)
	{
		auto* pSurf = DSurface::Composite;
		const auto& L = g_Layout;
		if (!pSurf || L.BmpW <= 0)
			return;

		// 主色 = 色板选择（自动时为作者所属方颜色），圆盘与线条都用它；
		// 本人标记白描边 + 黑外圈保持统一风格与归属可辨
		const ColorStruct intent = ResolveColor(m);
		const int intentInt = Drawing::RGB_To_Int(intent);
		const int white = Drawing::RGB_To_Int(ColorStruct { 255, 255, 255 });

		auto* pPlayer = HouseClass::CurrentPlayer;
		const bool own = pPlayer && m.Author == pPlayer->ArrayIndex;

		const Point2D a = CellToScreen(m.Cell);

		// 悬停：圆形柔光（用户明确要求不要方形），中心亮、外缘淡出
		if (hovered)
		{
			const int r = IsBadgeKind(m.Kind) ? 19 : 14;
			DrawGlowDisc(a, r, ColorStruct { 255, 240, 160 });
			if (KindHasEndPoint(m.Kind))
				DrawGlowDisc(CellToScreen(m.Cell2), 10, ColorStruct { 255, 240, 160 });
		}

		// 新标记脉冲：标记色扩散圆环（徽章不播这一次性脉冲，另有常驻同心圆）
		const int age = Unsorted::CurrentFrame - m.PlacedFrame;
		if (!IsBadgeKind(m.Kind) && age >= 0 && age < PulseFrames)
		{
			const int r = 8 + age * 20 / PulseFrames;
			RectangleStruct ring { a.X - r, a.Y - r, r * 2, r * 2 };
			DrawEllipse(ring, intentInt);
		}

		// 统一风格：黑描边圆盘 + 标记颜色 + 白色符号；
		// 本人的标记最外再套一圈白环——与单位像素点、触发器标记一眼区分
		auto drawBase = [&](Point2D c)
		{
			if (own)
				DrawDisc(c, MarkRadius + 2, white);
			DrawDisc(c, MarkRadius, 0);
			DrawDisc(c, MarkRadius - 1, intentInt);
		};

		// 线段类标记的两端小圆点
		auto drawEnd = [&](Point2D c)
		{
			if (own)
				DrawDisc(c, EndRadius + 2, white);
			DrawDisc(c, EndRadius, 0);
			DrawDisc(c, std::max(1, EndRadius - 1), intentInt);
		};

		switch (m.Kind)
		{
		case FullMapMarkKind::Cross:
		{
			drawBase(a);
			const int g = 4;
			DrawLine1({ a.X - g, a.Y - g }, { a.X + g, a.Y + g }, white);
			DrawLine1({ a.X - g, a.Y + g }, { a.X + g, a.Y - g }, white);
			break;
		}
		case FullMapMarkKind::Triangle:
		{
			drawBase(a);
			// 尖头朝下：从 a.Y-8 加宽到 a.Y 收尖
			DrawFilledTriangle({ a.X, a.Y - 7 }, 5, 9, white);
			break;
		}
		case FullMapMarkKind::Crosshair:
		{
			drawBase(a);
			DrawLine1({ a.X - 5, a.Y }, { a.X + 5, a.Y }, white);
			DrawLine1({ a.X, a.Y - 5 }, { a.X, a.Y + 5 }, white);
			break;
		}
		case FullMapMarkKind::Line:
		{
			const Point2D b = CellToScreen(m.Cell2);
			DrawLine1(a, b, intentInt);
			drawEnd(a);
			drawEnd(b);
			break;
		}
		case FullMapMarkKind::Arrow:
		{
			const Point2D b = CellToScreen(m.Cell2);
			DrawLine2(a, b, intentInt);
			DrawArrowHead(b, a, 18, intentInt);
			drawEnd(a); // 箭尖处不画圆点，保证箭头形状清晰
			break;
		}
		case FullMapMarkKind::Rect:
		{
			const Point2D b = CellToScreen(m.Cell2);
			RectangleStruct rc {
				std::min(a.X, b.X), std::min(a.Y, b.Y),
				std::max(1, std::abs(a.X - b.X)), std::max(1, std::abs(a.Y - b.Y)) };
			DrawRectangleOutline(rc, intentInt);
			drawEnd(a);
			drawEnd(b);
			break;
		}
		case FullMapMarkKind::Ellipse:
		{
			const Point2D b = CellToScreen(m.Cell2);
			RectangleStruct rc {
				std::min(a.X, b.X), std::min(a.Y, b.Y),
				std::max(1, std::abs(a.X - b.X)), std::max(1, std::abs(a.Y - b.Y)) };
			DrawEllipse(rc, intentInt);
			drawEnd(a);
			drawEnd(b);
			break;
		}
		case FullMapMarkKind::Freehand:
		{
			for (int i = 1; i < m.PointCount; ++i)
			{
				DrawLine1(
					CellToScreen(CellStruct { m.Points[i - 1][0], m.Points[i - 1][1] }),
					CellToScreen(CellStruct { m.Points[i][0], m.Points[i][1] }),
					intentInt);
			}
			drawEnd(a);
			break;
		}
		case FullMapMarkKind::Text:
		{
			if (own)
				DrawDisc(a, 7, white);
			DrawDisc(a, 5, 0);
			DrawDisc(a, 4, intentInt);
			PrintTextWithBacking(m.Text, { a.X + 8, a.Y - 6 }, white);
			break;
		}
		case FullMapMarkKind::Distance:
		{
			const Point2D b = CellToScreen(m.Cell2);
			DrawLine1(a, b, intentInt);
			DrawLine1({ a.X, a.Y - 4 }, { a.X, a.Y + 4 }, intentInt);
			DrawLine1({ b.X, b.Y - 4 }, { b.X, b.Y + 4 }, intentInt);
			drawEnd(a);
			drawEnd(b);

			const int dx = m.Cell2.X - m.Cell.X;
			const int dy = m.Cell2.Y - m.Cell.Y;
			const int dist = static_cast<int>(std::sqrt(dx * dx + dy * dy) + 0.5);
			wchar_t buf[32];
			swprintf_s(buf, StringTable::TryFetchString("SF:FULLMAP_DISTANCE_FMT", L"%d格"), dist);
			PrintTextWithBacking(buf, { (a.X + b.X) / 2 + 6, (a.Y + b.Y) / 2 - 6 }, white);
			break;
		}
		default:
			if (IsBadgeKind(m.Kind))
			{
				// 全屏地图徽章：黑描边 + 深色盘 + 含义色彩环 + 白图标（C 方案）
				// 深色底保证白图标在任何地形上都有对比，彩色环表达含义
				constexpr int BR = 13;   // 盘半径放大，给图标留出空间
				const ColorStruct meaning = BadgeMeaningColor(m.Kind);
				const int meaningInt = Drawing::RGB_To_Int(meaning);
				const int darkInt = Drawing::RGB_To_Int(ColorStruct { 24, 25, 30 });

				// 扩散环：多环相位错开，半径增长时同步渐隐（波状）
				const int frame = Unsorted::CurrentFrame;
				for (int i = 0; i < BadgeRingCount; ++i)
				{
					const int phase = BadgeRingPeriod * i / BadgeRingCount;
					const int rr = BadgeRingRadius(frame, phase);
					const int alpha = BadgeRingOpacity(rr);
					if (alpha <= 2)
						continue;
					DrawEllipseFade(a, BR + 4 + rr, BR + 4 + rr, meaningInt, alpha);
				}

				// 结构：白外圈（本人）= 黑描边 + 含义色实心圆 + 深色内圆 + 白图标
				// 实心圆相减形成干净的彩色厚边，比描边椭圆在小尺寸下更整齐
				if (own)
					DrawDisc(a, BR + 3, white);
				DrawDisc(a, BR + 2, 0);
				DrawDisc(a, BR, meaningInt);      // 含义色彩边
				DrawDisc(a, BR - 3, darkInt);     // 深色盘面
				DrawBadgeGlyph(m.Kind, a, BR - 6, white);

				// 玩家身份：盘右上角小色点
				const ColorStruct idColor = GetHouse(m.Author)
					? GetHouse(m.Author)->Color : ColorStruct { 255, 255, 255 };
				Point2D pd { a.X + BR - 3, a.Y - BR + 3 };
				DrawDisc(pd, 3, 0);
				DrawDisc(pd, 2, Drawing::RGB_To_Int(idColor));

				const wchar_t* label = BadgeLabel(m.Kind);
				RectangleStruct dims = Drawing::GetTextDimensions(label, { 0, 0 }, 0, 2, 0);
				PrintTextWithBacking(label, { a.X - dims.Width / 2, a.Y + BR + 3 }, white);
			}
			break;
		}
	}

	// ============================================================================
	// 工具栏与状态条
	// ============================================================================

	const wchar_t* ToolKindName(FullMapMarkKind kind)
	{
		switch (kind)
		{
		case FullMapMarkKind::Cross: return StringTable::TryFetchString("SF:FULLMAP_KIND_CROSS", L"标记 X");
		case FullMapMarkKind::Triangle: return StringTable::TryFetchString("SF:FULLMAP_KIND_TRI", L"三角");
		case FullMapMarkKind::Crosshair: return StringTable::TryFetchString("SF:FULLMAP_KIND_AIM", L"准星");
		case FullMapMarkKind::Line: return StringTable::TryFetchString("SF:FULLMAP_KIND_LINE", L"直线");
		case FullMapMarkKind::Arrow: return StringTable::TryFetchString("SF:FULLMAP_KIND_ARROW", L"箭头");
		case FullMapMarkKind::Rect: return StringTable::TryFetchString("SF:FULLMAP_KIND_RECT", L"矩形");
		case FullMapMarkKind::Ellipse: return StringTable::TryFetchString("SF:FULLMAP_KIND_ELLIPSE", L"椭圆");
		case FullMapMarkKind::Freehand: return StringTable::TryFetchString("SF:FULLMAP_KIND_FREEHAND", L"手绘");
		case FullMapMarkKind::Text: return StringTable::TryFetchString("SF:FULLMAP_KIND_TEXT", L"文字");
		case FullMapMarkKind::Distance: return StringTable::TryFetchString("SF:FULLMAP_KIND_DISTANCE", L"测距");
		default: return StringTable::TryFetchString("SF:FULLMAP_KIND_UNKNOWN", L"?");
		}
	}

	// 工具图标 = 标记实际样式的缩微预览（所见即所得）
	void DrawIconPrimitive(int id, RectangleStruct rc, int colorInt)
	{
		const int cx = rc.X + rc.Width / 2;
		const int cy = rc.Y + rc.Height / 2;
		const int dark = Drawing::RGB_To_Int(ColorStruct { 5, 10, 16 });
		const int accent = colorInt;
		const int bright = Drawing::RGB_To_Int(ColorStruct { 226, 246, 240 });

		auto stroke = [&](Point2D a, Point2D b, int color = -1)
		{
			if (color < 0) color = accent;
			DrawLine1({ a.X - 1, a.Y }, { b.X - 1, b.Y }, dark);
			DrawLine1(a, b, color);
		};
		auto node = [&](Point2D p, int radius = 2)
		{
			DrawDisc(p, radius + 1, dark);
			DrawDisc(p, radius, accent);
		};
		auto cornerBox = [&](const RectangleStruct& box)
		{
			const int n = 4;
			stroke({ box.X, box.Y }, { box.X + n, box.Y });
			stroke({ box.X, box.Y }, { box.X, box.Y + n });
			stroke({ box.X + box.Width, box.Y }, { box.X + box.Width - n, box.Y });
			stroke({ box.X + box.Width, box.Y }, { box.X + box.Width, box.Y + n });
			stroke({ box.X, box.Y + box.Height }, { box.X + n, box.Y + box.Height });
			stroke({ box.X, box.Y + box.Height }, { box.X, box.Y + box.Height - n });
			stroke({ box.X + box.Width, box.Y + box.Height }, { box.X + box.Width - n, box.Y + box.Height });
			stroke({ box.X + box.Width, box.Y + box.Height }, { box.X + box.Width, box.Y + box.Height - n });
		};

		switch (static_cast<FullMapMarkKind>(id))
		{
		case FullMapMarkKind::Cross:
			stroke({ cx - 6, cy - 6 }, { cx + 6, cy + 6 }, bright);
			stroke({ cx - 6, cy + 6 }, { cx + 6, cy - 6 }, bright);
			node({ cx, cy }, 2);
			break;
		case FullMapMarkKind::Triangle:
			stroke({ cx, cy - 8 }, { cx + 8, cy + 6 });
			stroke({ cx + 8, cy + 6 }, { cx - 8, cy + 6 });
			stroke({ cx - 8, cy + 6 }, { cx, cy - 8 });
			node({ cx, cy - 1 }, 1);
			break;
		case FullMapMarkKind::Crosshair:
			DrawEllipse({ cx - 7, cy - 7, 14, 14 }, accent);
			stroke({ cx - 10, cy }, { cx - 3, cy });
			stroke({ cx + 3, cy }, { cx + 10, cy });
			stroke({ cx, cy - 10 }, { cx, cy - 3 });
			stroke({ cx, cy + 3 }, { cx, cy + 10 });
			node({ cx, cy }, 1);
			break;
		case FullMapMarkKind::Line:
			node({ cx - 9, cy + 5 });
			node({ cx + 9, cy - 5 });
			stroke({ cx - 7, cy + 4 }, { cx + 7, cy - 4 });
			break;
		case FullMapMarkKind::Arrow:
			stroke({ cx - 10, cy + 6 }, { cx + 7, cy - 5 });
			stroke({ cx + 7, cy - 5 }, { cx + 2, cy - 5 });
			stroke({ cx + 7, cy - 5 }, { cx + 5, cy });
			node({ cx - 10, cy + 6 }, 1);
			break;
		case FullMapMarkKind::Rect:
			cornerBox({ cx - 9, cy - 6, 18, 12 });
			node({ cx - 9, cy - 6 }, 1);
			node({ cx + 9, cy + 6 }, 1);
			break;
		case FullMapMarkKind::Ellipse:
			DrawEllipse({ cx - 10, cy - 6, 20, 12 }, accent);
			node({ cx - 10, cy }, 1);
			node({ cx + 10, cy }, 1);
			break;
		case FullMapMarkKind::Freehand:
			node({ cx - 10, cy + 5 }, 1);
			stroke({ cx - 10, cy + 5 }, { cx - 3, cy - 4 });
			stroke({ cx - 3, cy - 4 }, { cx + 3, cy + 3 });
			stroke({ cx + 3, cy + 3 }, { cx + 10, cy - 6 });
			node({ cx + 10, cy - 6 }, 1);
			break;
		case FullMapMarkKind::Text:
			cornerBox({ cx - 9, cy - 7, 18, 14 });
			PrintText(L"A", { cx - 3, cy - 6 }, bright);
			break;
		case FullMapMarkKind::Distance:
			stroke({ cx - 10, cy + 4 }, { cx + 10, cy - 4 });
			stroke({ cx - 10, cy }, { cx - 10, cy + 8 });
			stroke({ cx + 10, cy - 8 }, { cx + 10, cy });
			PrintText(L"12", { cx - 6, cy - 11 }, bright);
			break;
		default:
			break;
		}
	}
	void DrawToolbarButton(int id, RectangleStruct rc, bool selected, bool hover, Point2D mouse)
	{
		auto* pSurf = DSurface::Composite;
		// 统一成战术 HUD：深蓝灰底、细金属边、青绿色选中态。
		// 所有按钮保持同一层级，图标负责表达工具含义。
		ColorStruct bg { 13, 18, 25 };
		if (hover && !selected)
			bg = { 25, 42, 49 };
		if (selected)
			bg = { 27, 62, 65 };
		pSurf->FillRect(&rc, Drawing::RGB_To_Int(bg));

		const int border = Drawing::RGB_To_Int(selected ? ColorStruct { 245, 196, 86 }
			: hover ? ColorStruct { 112, 170, 174 } : ColorStruct { 58, 78, 88 });
		pSurf->DrawRect(&rc, static_cast<DWORD>(border));
		RectangleStruct topLine { rc.X + 2, rc.Y + 1, std::max(1, rc.Width - 4), 1 };
		pSurf->FillRect(&topLine, Drawing::RGB_To_Int(
			selected ? ColorStruct { 100, 190, 184 } : ColorStruct { 32, 49, 58 }));
		RectangleStruct bottomLine { rc.X + 2, rc.Y + rc.Height - 2, std::max(1, rc.Width - 4), 1 };
		pSurf->FillRect(&bottomLine, Drawing::RGB_To_Int(ColorStruct { 8, 11, 16 }));
		if (selected)
		{
			// 左侧短灯条比整块高亮更克制，也更容易在长工具栏中定位当前工具。
			RectangleStruct accent { rc.X + 2, rc.Y + 4, 2, std::max(1, rc.Height - 8) };
			pSurf->FillRect(&accent, Drawing::RGB_To_Int(ColorStruct { 245, 196, 86 }));
			RectangleStruct inner { rc.X + 1, rc.Y + 1, rc.Width - 2, rc.Height - 2 };
			pSurf->DrawRect(&inner, Drawing::RGB_To_Int(ColorStruct { 75, 142, 145 }));
		}

		const int icon = Drawing::RGB_To_Int(ColorStruct { 218, 236, 232 });

		if (id == BtnZoomIn || id == BtnZoomOut)
		{
			const int cx = rc.X + rc.Width / 2 - 2, cy = rc.Y + rc.Height / 2 - 2;
			for (int i = 0; i < 20; ++i)
			{
				const double a = 2 * Pi * i / 20, b = 2 * Pi * (i + 1) / 20;
				DrawLine1({ cx + static_cast<int>(std::lround(6 * std::cos(a))), cy + static_cast<int>(std::lround(6 * std::sin(a))) },
					{ cx + static_cast<int>(std::lround(6 * std::cos(b))), cy + static_cast<int>(std::lround(6 * std::sin(b))) }, icon);
			}
			DrawLine1({ cx + 4, cy + 5 }, { cx + 9, cy + 10 }, icon);
			DrawLine1({ cx - 3, cy }, { cx + 3, cy }, icon);
			if (id == BtnZoomIn) DrawLine1({ cx, cy - 3 }, { cx, cy + 3 }, icon);
		}
		else if (id == BtnEraser)
		{
			// 清晰的双色橡皮：深色外轮廓、浅色主体和黄色胶头。
			const int cx = rc.X + rc.Width / 2, cy = rc.Y + rc.Height / 2;
			const int dark = Drawing::RGB_To_Int(ColorStruct { 5, 10, 16 });
			const int body = Drawing::RGB_To_Int(ColorStruct { 218, 236, 232 });
			const int rubber = Drawing::RGB_To_Int(ColorStruct { 245, 196, 86 });
			const Point2D outline[] {
				{ cx - 10, cy + 4 }, { cx - 2, cy - 7 },
				{ cx + 10, cy + 1 }, { cx + 2, cy + 9 } };
			const Point2D inner[] {
				{ cx - 8, cy + 3 }, { cx - 2, cy - 5 },
				{ cx + 8, cy + 1 }, { cx + 2, cy + 7 } };
			const Point2D rubberEnd[] {
				{ cx - 8, cy + 3 }, { cx - 2, cy - 5 },
				{ cx + 1, cy - 3 }, { cx - 5, cy + 5 } };
			FillPolygon(outline, 4, dark);
			FillPolygon(inner, 4, body);
			FillPolygon(rubberEnd, 4, rubber);
			DrawLine1({ cx - 2, cy - 5 }, { cx + 1, cy - 3 }, dark);
			DrawLine1({ cx - 5, cy + 5 }, { cx + 2, cy + 7 }, dark);
		}
		else if (id == BtnNoTool)
		{
			// 无工具：细环与斜线。
			const int cx = rc.X + rc.Width / 2;
			const int cy = rc.Y + rc.Height / 2;
			RectangleStruct ring { cx - 8, cy - 8, 16, 16 };
			DrawEllipse(ring, icon);
			DrawLine1({ cx - 6, cy + 6 }, { cx + 6, cy - 6 }, icon);
		}
		else if (id == BtnBadgeCat)
		{
			// 已移除开关，保留分支防止旧 id 误入
		}
		else if (id >= BtnBadgeBase && id < BtnBadgeBase + 6)
		{
			// 徽章按钮：深色底 + 白色字形 + 底部含义色条。
			// 小格子里"颜色圆盘+白字形"会糊成一团，改为色条承载含义、字形承载识别。
			const auto kind = static_cast<FullMapMarkKind>(static_cast<int>(FullMapMarkKind::Attack) + (id - BtnBadgeBase));
			const ColorStruct meaning = BadgeMeaningColor(kind);
			const int meaningInt = Drawing::RGB_To_Int(meaning);

			// 字形主体居中偏上，给底部色条留位
			const Point2D c { rc.X + rc.Width / 2, rc.Y + rc.Height / 2 - 2 };
			DrawBadgeGlyph(kind, c, 5, icon);

			// 底部含义色条（2px）
			RectangleStruct bar { rc.X + 3, rc.Y + rc.Height - 4, rc.Width - 6, 2 };
			pSurf->FillRect(&bar, meaningInt);
		}
		else if (id == BtnColorBase)
		{
			// "默认"（所属方色）：小色块 + 文字，整体居中；放不下时缩短为"默认"
			auto* pPlayer = HouseClass::CurrentPlayer;
			ColorStruct c = pPlayer ? pPlayer->Color : ColorStruct { 255, 255, 255 };
			const wchar_t* text = StringTable::TryFetchString("SF:FULLMAP_COLORAUTO", L"所属方色");
			RectangleStruct dims = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 2, 0);
			const int sw = 9;
			if (sw + 4 + dims.Width > rc.Width - 2)
			{
				text = StringTable::TryFetchString("SF:FULLMAP_COLORAUTO_S", L"默认");
				dims = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 2, 0);
			}
			const int total = sw + 4 + dims.Width;
			const int sx = rc.X + std::max(2, (rc.Width - total) / 2);
			const int sy = rc.Y + std::max(2, (rc.Height - sw) / 2);
			RectangleStruct swatch { sx, sy, sw, sw };
			swatch = ClipRect(swatch);
			if (!IsEmpty(swatch))
			{
				pSurf->FillRect(&swatch, Drawing::RGB_To_Int(c));
				pSurf->DrawRect(&swatch, static_cast<DWORD>(border));
			}
			PrintText(text, { sx + sw + 4, rc.Y + std::max(2, (rc.Height - 12) / 2) },
				Drawing::RGB_To_Int(ColorStruct { 235, 235, 235 }));
		}
		else if (id >= BtnColorBase + 1 && id < BtnColorBase + PaletteCount)
		{
			RectangleStruct sw { rc.X + 1, rc.Y + 1, std::max(1, rc.Width - 2), std::max(1, rc.Height - 2) };
			pSurf->FillRect(&sw, Drawing::RGB_To_Int(Palette[id - BtnColorBase]));
		}
		else if (id == BtnClearOwn)
		{
			const wchar_t* text = StringTable::TryFetchString("SF:FULLMAP_CLEAR", L"清除");
			RectangleStruct dims = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 2, 0);
			PrintText(text, { rc.X + std::max(2, (rc.Width - dims.Width) / 2),
				rc.Y + std::max(2, (rc.Height - 12) / 2) }, icon);
		}
		else if (id == BtnClose)
		{
			ColorStruct red { 255, 80, 80 };
			pSurf->FillRect(&rc, Drawing::RGB_To_Int(ColorStruct { 120, 30, 30 }));
			pSurf->DrawRect(&rc, static_cast<DWORD>(Drawing::RGB_To_Int(red)));
			const wchar_t* text = StringTable::TryFetchString("SF:FULLMAP_CLOSE", L"关闭");
			RectangleStruct dims = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 2, 0);
			PrintText(text, { rc.X + std::max(2, (rc.Width - dims.Width) / 2),
				rc.Y + std::max(2, (rc.Height - 12) / 2) },
				Drawing::RGB_To_Int(ColorStruct { 255, 255, 255 }));
		}
		else
		{
			DrawIconPrimitive(id, rc, icon);
		}
	}

	const wchar_t* TooltipFor(int id)
	{
		switch (id)
		{
		case BtnZoomIn: return StringTable::TryFetchString("SF:FULLMAP_TT_ZOOMIN", L"放大");
		case BtnZoomOut: return StringTable::TryFetchString("SF:FULLMAP_TT_ZOOMOUT", L"缩小");
		case BtnEraser: return StringTable::TryFetchString("SF:FULLMAP_TT_ERASER", L"删除标记");
		case BtnClearOwn: return StringTable::TryFetchString("SF:FULLMAP_TT_CLEAR", L"清除标记");
		case BtnClose: return StringTable::TryFetchString("SF:FULLMAP_TT_CLOSE", L"关闭地图");
		case BtnBadgeCat: return L"";
		case BtnNoTool: return StringTable::TryFetchString("SF:FULLMAP_TT_NONE_PAN", L"查看");
		default: break;
		}
		if (id >= BtnBadgeBase && id < BtnBadgeBase + 6)
		{
			const auto kind = static_cast<FullMapMarkKind>(static_cast<int>(FullMapMarkKind::Attack) + (id - BtnBadgeBase));
			switch (kind)
			{
			case FullMapMarkKind::Attack: return StringTable::TryFetchString("SF:FULLMAP_TT_B_ATK", L"进攻");
			case FullMapMarkKind::Defend: return StringTable::TryFetchString("SF:FULLMAP_TT_B_DEF", L"防守");
			case FullMapMarkKind::Ore: return StringTable::TryFetchString("SF:FULLMAP_TT_B_ORE", L"矿石");
			case FullMapMarkKind::Tech: return StringTable::TryFetchString("SF:FULLMAP_TT_B_TECH", L"科技");
			case FullMapMarkKind::Rally: return StringTable::TryFetchString("SF:FULLMAP_TT_B_RALLY", L"集结");
			case FullMapMarkKind::Danger: return StringTable::TryFetchString("SF:FULLMAP_TT_B_DANGER", L"危险");
			default: return L"";
			}
		}
		if (id == BtnColorBase)
			return StringTable::TryFetchString("SF:FULLMAP_TT_COLORAUTO", L"自动颜色");
		if (id > BtnColorBase && id < BtnColorBase + PaletteCount)
			return StringTable::TryFetchString("SF:FULLMAP_TT_COLOR", L"固定颜色");

		switch (static_cast<FullMapMarkKind>(id))
		{
		case FullMapMarkKind::Cross: return StringTable::TryFetchString("SF:FULLMAP_TT_CROSS", L"X 标记");
		case FullMapMarkKind::Triangle: return StringTable::TryFetchString("SF:FULLMAP_TT_TRI", L"三角标记");
		case FullMapMarkKind::Crosshair: return StringTable::TryFetchString("SF:FULLMAP_TT_AIM", L"准星");
		case FullMapMarkKind::Line: return StringTable::TryFetchString("SF:FULLMAP_TT_LINE", L"直线");
		case FullMapMarkKind::Arrow: return StringTable::TryFetchString("SF:FULLMAP_TT_ARROW", L"箭头");
		case FullMapMarkKind::Rect: return StringTable::TryFetchString("SF:FULLMAP_TT_RECT", L"矩形");
		case FullMapMarkKind::Ellipse: return StringTable::TryFetchString("SF:FULLMAP_TT_ELLIPSE", L"椭圆");
		case FullMapMarkKind::Freehand: return StringTable::TryFetchString("SF:FULLMAP_TT_FREEHAND", L"手绘");
		case FullMapMarkKind::Text: return StringTable::TryFetchString("SF:FULLMAP_TT_TEXT", L"文字");
		case FullMapMarkKind::Distance: return StringTable::TryFetchString("SF:FULLMAP_TT_DIST", L"测距");
		default: return L"";
		}
	}

	void DrawTooltip(int id, RectangleStruct btnRect)
	{
		const wchar_t* text = TooltipFor(id);
		if (!text || !*text)
			return;

		RectangleStruct dims = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 2, 0);
		if (dims.Width <= 0)
			dims.Width = 40;

		const int w = dims.Width + 10;
		const int h = 18;
		int x = g_Layout.Toolbar.X - w - 8;
		if (x < g_Layout.View.X + 4)
			x = g_Layout.View.X + 4;
		const int y = std::max(g_Layout.View.Y + 4,
			std::min(btnRect.Y + btnRect.Height / 2 - h / 2, g_Layout.View.Y + g_Layout.View.Height - h - 4));

		RectangleStruct rc { x, y, w, h };
		ColorStruct black { 0, 0, 0 };
		DSurface::Composite->FillRectTrans(&rc, &black, 25);
		DSurface::Composite->DrawRect(&rc, Drawing::RGB_To_Int(ColorStruct { 200, 200, 200 }));
		PrintText(text, { x + 5, y + 3 }, Drawing::RGB_To_Int(ColorStruct { 255, 255, 255 }));
	}

	void RenderToolbar(Point2D mouse)
	{
		auto* pSurf = DSurface::Composite;
		auto& L = g_Layout;
		L.Buttons.clear();

		ColorStruct bg { 10, 10, 10 };
		pSurf->FillRect(&L.Toolbar, Drawing::RGB_To_Int(bg));
		pSurf->DrawRect(&L.Toolbar, Drawing::RGB_To_Int(ColorStruct { 120, 120, 120 }));

		int x = L.Toolbar.X + (ToolbarWidth - ToolBtnW) / 2;
		int y = L.Toolbar.Y + 30;
		int hoverId = -1;
		RectangleStruct hoverRect {};

		// ---- 底部固定区：关闭 / 清除 / 徽章 2×3（常驻右下） ----
		int by = L.Toolbar.Y + L.Toolbar.Height - 27; // 关闭
		auto pinnedButton = [&](int id, RectangleStruct rc)
		{
			const bool hover = mouse.X >= rc.X && mouse.X < rc.X + rc.Width
				&& mouse.Y >= rc.Y && mouse.Y < rc.Y + rc.Height;
			if (hover) { hoverId = id; hoverRect = rc; }
			DrawToolbarButton(id, rc, false, hover, mouse);
			L.Buttons.push_back({ id, rc });
		};
		pinnedButton(BtnZoomIn, { L.Toolbar.X + 3, L.Toolbar.Y + 3, 23, 24 });
		pinnedButton(BtnZoomOut, { L.Toolbar.X + 28, L.Toolbar.Y + 3, 23, 24 });
		{
			RectangleStruct rcClose { x, by, ToolBtnW, 24 };
			pinnedButton(BtnClose, rcClose);
			by -= 27;
			RectangleStruct rcClear { x, by, ToolBtnW, 24 };
			pinnedButton(BtnClearOwn, rcClear);
			by -= 30;

			// 徽章 2×3，含义色缩略
			for (int i = 0; i < BadgeCount; ++i)
			{
				const int col = i % 2;
				const int row = i / 2;
				RectangleStruct rc {
					L.Toolbar.X + 3 + col * 25, by - row * 27, 23, 25 };
				const bool hover = mouse.X >= rc.X && mouse.X < rc.X + rc.Width
					&& mouse.Y >= rc.Y && mouse.Y < rc.Y + rc.Height;
				if (hover) { hoverId = BtnBadgeBase + i; hoverRect = rc; }
				const auto kind = static_cast<FullMapMarkKind>(static_cast<int>(FullMapMarkKind::Attack) + i);
				DrawToolbarButton(BtnBadgeBase + i, rc, !s_Eraser && !s_NoTool && s_ToolKind == kind, hover, mouse);
				L.Buttons.push_back({ BtnBadgeBase + i, rc });
			}
			by -= 2 * 27 + 4; // 顶部可滚动区的下边界
		}

		// ---- 顶部滚动区：工具 + 橡皮 + 颜色 ----
		const int zoneTop = L.Toolbar.Y + 30;
		const int zoneBottom = by;
		const int zoneH = zoneBottom - zoneTop;

		// 先量内容高度
		int contentH = 0;
		for (int k = 0; k < static_cast<int>(FullMapMarkKind::Attack); ++k)
		{
			if (k == static_cast<int>(FullMapMarkKind::Crosshair))
				continue;
			contentH += 24 + 3;
		}
		contentH += 24 + 3;      // 橡皮
		contentH += 24 + 3;      // 无工具
		contentH += 4 + 23 + 19 + 24; // 色板区

		s_ToolbarScrollMax = std::max(0, contentH - zoneH);
		if (s_ToolbarScrollMax == 0)
			s_ToolbarScroll = 0.0f;
		s_ToolbarScroll = std::clamp(s_ToolbarScroll, 0.0f, static_cast<float>(s_ToolbarScrollMax));

		auto addButton = [&](int id, int w, int h)
		{
			RectangleStruct rc { x, y, w, h };
			rc.Y -= static_cast<int>(s_ToolbarScroll);
			const bool inZone = rc.Y >= zoneTop && rc.Y + h <= zoneBottom;
			if (inZone)
			{
				const bool hover = mouse.X >= rc.X && mouse.X < rc.X + rc.Width
					&& mouse.Y >= rc.Y && mouse.Y < rc.Y + rc.Height;
				if (hover) { hoverId = id; hoverRect = rc; }
				bool selected = false;
				if (id == BtnNoTool)
					selected = s_NoTool;
				else if (id < static_cast<int>(FullMapMarkKind::COUNT))
					selected = !s_Eraser && !s_NoTool && s_ToolKind == static_cast<FullMapMarkKind>(id);
				else if (id == BtnEraser)
					selected = s_Eraser;
				DrawToolbarButton(id, rc, selected, hover, mouse);
				L.Buttons.push_back({ id, rc });
			}
			y += h + 3;
		};

		// 无工具（仅查看/移动视野）+ 9 个标注工具 + 橡皮
		addButton(BtnNoTool, ToolBtnW, 24);
		for (int k = 0; k < static_cast<int>(FullMapMarkKind::Attack); ++k)
		{
			if (k == static_cast<int>(FullMapMarkKind::Crosshair))
				continue;
			addButton(BtnToolBase + k, ToolBtnW, 24);
		}
		addButton(BtnEraser, ToolBtnW, 24);

		// 颜色：首行"默认"（所属方色）按钮，下方 4×2 固定色板
		y += 4;
		{
			RectangleStruct rcAuto { L.Toolbar.X + 3, y - static_cast<int>(s_ToolbarScroll), ToolbarWidth - 6, 20 };
			if (rcAuto.Y >= zoneTop && rcAuto.Y + 20 <= zoneBottom)
			{
				const bool hoverAuto = mouse.X >= rcAuto.X && mouse.X < rcAuto.X + rcAuto.Width
					&& mouse.Y >= rcAuto.Y && mouse.Y < rcAuto.Y + rcAuto.Height;
				if (hoverAuto) { hoverId = BtnColorBase; hoverRect = rcAuto; }
				DrawToolbarButton(BtnColorBase, rcAuto, s_ColorIdx == 0, hoverAuto, mouse);
				L.Buttons.push_back({ BtnColorBase, rcAuto });
			}
			y += 23;

			for (int idx = 1; idx < PaletteCount; ++idx)
			{
				const int slot = idx - 1;
				const int col = slot % 4;
				const int row = slot / 4;
				RectangleStruct rc { L.Toolbar.X + 3 + col * 12,
					y + row * 12 - static_cast<int>(s_ToolbarScroll), 10, 10 };
				if (rc.Y >= zoneTop && rc.Y + 10 <= zoneBottom)
				{
					const bool hover = mouse.X >= rc.X && mouse.X < rc.X + rc.Width
						&& mouse.Y >= rc.Y && mouse.Y < rc.Y + rc.Height;
					if (hover) { hoverId = BtnColorBase + idx; hoverRect = rc; }
					DrawToolbarButton(BtnColorBase + idx, rc, s_ColorIdx == idx, hover, mouse);
					L.Buttons.push_back({ BtnColorBase + idx, rc });
				}
			}
			y += 2 * 12;
		}

		// 滚动指示
		if (s_ToolbarScrollMax > 0)
		{
			const int trackX = L.Toolbar.X + ToolbarWidth - 4;
			RectangleStruct track { trackX, zoneTop, 2, std::max(4, zoneH) };
			track = ClipRect(track);
			if (!IsEmpty(track))
			{
				pSurf->FillRect(&track, Drawing::RGB_To_Int(ColorStruct { 60, 60, 60 }));
				const int thumbH = std::max(6, zoneH * zoneH / std::max(1, contentH));
				const int thumbY = zoneTop + (zoneH - thumbH)
					* static_cast<int>(s_ToolbarScroll) / std::max(1, s_ToolbarScrollMax);
				RectangleStruct thumb { trackX, thumbY, 2, thumbH };
				thumb = ClipRect(thumb);
				if (!IsEmpty(thumb))
					pSurf->FillRect(&thumb, Drawing::RGB_To_Int(ColorStruct { 180, 180, 180 }));
			}
		}

		// 悬停文字提示
		if (hoverId >= 0)
			DrawTooltip(hoverId, hoverRect);
	}

	void RenderStatus(Point2D mouse)
	{
		auto* pSurf = DSurface::Composite;
		auto& L = g_Layout;
		ColorStruct bg { 10, 10, 10 };
		pSurf->FillRect(&L.Status, Drawing::RGB_To_Int(bg));
		pSurf->DrawRect(&L.Status, Drawing::RGB_To_Int(ColorStruct { 120, 120, 120 }));

		const int white = Drawing::RGB_To_Int(ColorStruct { 235, 235, 235 });
		const int grey = Drawing::RGB_To_Int(ColorStruct { 160, 160, 160 });

		int x = L.Status.X + 6;
		const int y = L.Status.Y + 4;

		// 当前工具
		PrintText(s_Eraser ? StringTable::TryFetchString("SF:FULLMAP_ERASER", L"橡皮擦")
			: s_NoTool ? StringTable::TryFetchString("SF:FULLMAP_NONE", L"查看")
			: ToolKindName(s_ToolKind),
			{ x, y }, white);
		x += 76;

		// 悬停格坐标
		CellStruct cell {};
		if (ScreenToCell(mouse, cell))
		{
			wchar_t buf[48];
			swprintf_s(buf, L"(%d, %d)", cell.X, cell.Y);
			PrintText(buf, { x, y }, grey);
		}
		x += 90;

		// 提示 / 输入缓冲
		if (s_NativeTyping)
		{
			const wchar_t* hint2 = StringTable::TryFetchString("SF:FULLMAP_HINT_TYPING",
				L"输入文字中… 回车确认 / Esc 取消（点击取消）");
			PrintTextWithBacking(hint2, { x, y }, white);
		}
		else if (s_Typing)
		{
			std::wstring text = s_TextBuf + L"_";
			PrintTextWithBacking(text.c_str(), { x, y }, white);
		}
		else if (!s_NoTool)
		{
			const wchar_t* hint = StringTable::TryFetchString("SF:FULLMAP_HINT_TOOL",
				s_Eraser ? L"左键删除；右键返回查看" : L"左键绘制；右键返回查看");
			PrintText(hint, { x, y }, grey);
		}
		else
		{
			const wchar_t* hint = StringTable::TryFetchString("SF:FULLMAP_HINT_PAN",
				L"左键拖动地图/视野框；中键平移；双击跳转；Ctrl+滚轮缩放");
			PrintText(hint, { x, y }, grey);
		}
	}

	RectangleStruct ViewBoxScreen()
	{
		const auto& L = g_Layout;
		const auto vb = GetViewRectInSource();
		return {
			L.MapRect.X + (vb.X - L.SourceX) * L.MapRect.Width / std::max(1, L.BmpW),
			L.MapRect.Y + (vb.Y - L.SourceY) * L.MapRect.Height / std::max(1, L.BmpH),
			std::max(2, vb.Width * L.MapRect.Width / std::max(1, L.BmpW)),
			std::max(2, vb.Height * L.MapRect.Height / std::max(1, L.BmpH))
		};
	}

	void DrawCurrentViewBox(Point2D mouse)
	{
		if (g_Layout.BmpW <= 0 || g_Layout.BmpH <= 0) return;
		const auto rc = ViewBoxScreen();
		// At enlarged zoom the interior gesture pans the map. Keep the camera
		// outline visible without washing out terrain under an inactive handle.
		const auto& clip = g_Layout.PlayableClip;
		const bool hoverBox = s_NoTool && !s_Eraser && !s_Typing && !s_NativeTyping
			&& s_Zoom <= 1.001f && s_ZoomTarget <= 1.001f
			&& FullMapGeometry::MapDragAt(
				{ static_cast<double>(mouse.X), static_cast<double>(mouse.Y) },
				{ clip.X, clip.Y, clip.Width, clip.Height },
				{ rc.X, rc.Y, rc.Width, rc.Height }, false) == FullMapGeometry::DragMode::MoveCamera;
		if (hoverBox)
		{
			auto fill = ClipRect(rc);
			ColorStruct white { 255, 255, 255 };
			if (!IsEmpty(fill)) DSurface::Composite->FillRectTrans(&fill, &white, 45);
		}
		const int white = Drawing::RGB_To_Int(ColorStruct { 255, 255, 255 });
		const int right = rc.X + rc.Width - 1, bottom = rc.Y + rc.Height - 1;
		DrawLine2({ rc.X, rc.Y }, { right, rc.Y }, white);
		DrawLine2({ right, rc.Y }, { right, bottom }, white);
		DrawLine2({ right, bottom }, { rc.X, bottom }, white);
		DrawLine2({ rc.X, bottom }, { rc.X, rc.Y }, white);
	}

	void MaskOutsidePlayableArea()
	{
		auto area = g_Layout.MapArea;
		const auto clip = g_Layout.PlayableClip;
		if (IsEmpty(area)) return;
		if (IsEmpty(clip))
		{
			DSurface::Composite->FillRect(&area, 0);
			return;
		}
		// Only four screen rectangles; no ownership mask and no cell coordinate
		// inference. Full terrain data remains cached and reappears on expansion.
		RectangleStruct strips[] = {
			{ area.X, area.Y, area.Width, clip.Y - area.Y },
			{ area.X, clip.Y + clip.Height, area.Width, area.Y + area.Height - clip.Y - clip.Height },
			{ area.X, clip.Y, clip.X - area.X, clip.Height },
			{ clip.X + clip.Width, clip.Y, area.X + area.Width - clip.X - clip.Width, clip.Height }
		};
		for (auto& strip : strips)
			if (!IsEmpty(strip)) DSurface::Composite->FillRect(&strip, 0);
	}

	// ============================================================================
	// 主绘制
	// ============================================================================

	void ComputeLayout(int srcW, int srcH, int sourceX = 0, int sourceY = 0)
	{
		FullMapPerf::Scope timing(FullMapPerf::Stage::Layout);
		auto& L = g_Layout;
		const auto& view = DSurface::ViewBounds;
		const auto previousView = L.View;
		const auto previous = L.MapRect;
		const bool reset = L.SourceX != sourceX || L.SourceY != sourceY
			|| L.BmpW != srcW || L.BmpH != srcH
			|| previousView.Width != view.Width || previousView.Height != view.Height;
		L.View = view;
		L.SourceX = sourceX;
		L.SourceY = sourceY;
		L.Toolbar = { view.X + view.Width - ToolbarWidth - Margin, view.Y + Margin,
			ToolbarWidth, std::max(0, view.Height - 2 * Margin) };
		L.MapArea = { view.X + Margin, view.Y + Margin,
			std::max(1, view.Width - ToolbarWidth - 3 * Margin),
			std::max(1, view.Height - StatusHeight - 2 * Margin) };
		L.Status = { L.MapArea.X, view.Y + view.Height - StatusHeight - Margin,
			L.MapArea.Width, StatusHeight };
		L.BmpW = std::max(1, srcW);
		L.BmpH = std::max(1, srcH);

		// Reserve the right toolbar and bottom status bar before fitting the map.
		// The map may zoom/pan inside this area, but never underneath UI controls.
		const double fitScale = std::min(L.MapArea.Width / static_cast<double>(L.BmpW),
			L.MapArea.Height / static_cast<double>(L.BmpH));
		const int fw = std::max(1, static_cast<int>(L.BmpW * fitScale));
		const int fh = std::max(1, static_cast<int>(L.BmpH * fitScale));
		L.FitRect = { L.MapArea.X + (L.MapArea.Width - fw) / 2,
			L.MapArea.Y + (L.MapArea.Height - fh) / 2, fw, fh };
		const double zoom = std::min<double>(s_Zoom, 8192.0 / std::max(fw, fh));
		const int width = std::max(1, static_cast<int>(fw * zoom));
		const int height = std::max(1, static_cast<int>(fh * zoom));

		if (reset || previous.Width <= 0 || previous.Height <= 0 || s_Zoom <= 1.001f)
		{
			L.MapRect = L.FitRect;
		}
		else if (width != previous.Width || height != previous.Height)
		{
			Point2D anchor { L.MapArea.X + L.MapArea.Width / 2, L.MapArea.Y + L.MapArea.Height / 2 };
			if (const auto* mouse = WWMouseClass::Instance; !s_ZoomUseCenter && mouse)
			{
				if (mouse->XY1.X >= view.X && mouse->XY1.X < view.X + view.Width
					&& mouse->XY1.Y >= view.Y && mouse->XY1.Y < view.Y + view.Height)
					anchor = mouse->XY1;
			}
			const auto scaled = FullMapGeometry::ScaleAround(
				{ previous.X, previous.Y, previous.Width, previous.Height }, width, height,
				{ static_cast<double>(anchor.X), static_cast<double>(anchor.Y) });
			L.MapRect = { scaled.X, scaled.Y, scaled.Width, scaled.Height };
		}
		else
		{
			L.MapRect = previous;
		}
		L.MapRect.X += s_PanOffset.X;
		L.MapRect.Y += s_PanOffset.Y;
		// Pan input is a delta. Re-applying its accumulated value every frame
		// makes the map keep drifting after the mouse has stopped.
		s_PanOffset = {};
		L.MapRect.X = L.MapRect.Width >= L.MapArea.Width
			? std::clamp(L.MapRect.X, L.MapArea.X + L.MapArea.Width - L.MapRect.Width, L.MapArea.X)
			: L.MapArea.X + (L.MapArea.Width - L.MapRect.Width) / 2;
		L.MapRect.Y = L.MapRect.Height >= L.MapArea.Height
			? std::clamp(L.MapRect.Y, L.MapArea.Y + L.MapArea.Height - L.MapRect.Height, L.MapArea.Y)
			: L.MapArea.Y + (L.MapArea.Height - L.MapRect.Height) / 2;
		const auto allowed = TerrainActive() ? FullMapTerrain::PlayableBounds()
			: RectangleStruct { 0, 0, L.BmpW, L.BmpH };
		const auto clip = FullMapGeometry::MapDisplayClip(
			{ L.MapArea.X, L.MapArea.Y, L.MapArea.Width, L.MapArea.Height },
			{ L.MapRect.X, L.MapRect.Y, L.MapRect.Width, L.MapRect.Height },
			{ L.SourceX, L.SourceY, L.BmpW, L.BmpH },
			{ allowed.X, allowed.Y, allowed.Width, allowed.Height });
		L.PlayableClip = { clip.X, clip.Y, clip.Width, clip.Height };
	}

	// ---- 底图缓存与 1:1 画布 ----
	int g_LastRadarW = 0, g_LastRadarH = 0;

	// 1:1 全图画布：底图按整张地图原尺寸铺进离屏缓冲，
	// 缩放/平移只从画布取样，避免每次滚轮都重采样源图（卡顿与闪烁的来源）。
	std::vector<unsigned short> g_FullCanvas;
	int g_FullW = 0, g_FullH = 0;
	// 横向采样列映射表（缩放期间避免逐像素整数除法）
	std::vector<int> g_ColMap;
	int g_ColMapWidth = 0, g_ColMapSrcW = 0;

	// ---- 1:1 全图画布 ----
	// 思路（按用户建议）：底图永远按"整张地图 1:1 像素"铺进离屏画布，
	// 缩放/平移只影响"从画布上取哪一块、以多大倍率贴到屏幕"。
	// 这样滚轮缩放不再触发任何源图重采样，只有画布尺寸变化时才重建。
	constexpr int FullCanvasMaxW = 2048;
	constexpr int FullCanvasMaxH = 2048;

	// 保证画布按 src 的 1:1 尺寸就绪（尺寸不变则完全不重做）
	void EnsureFullCanvas(int srcW, int srcH, int srcPitch, const unsigned short* pSrc)
	{
		if (!pSrc || srcW <= 0 || srcH <= 0)
			return;
		if (srcW > FullCanvasMaxW || srcH > FullCanvasMaxH)
			return;

		const size_t need = static_cast<size_t>(srcW) * srcH;
		g_FullW = srcW;
		g_FullH = srcH;
		g_FullCanvas.resize(need);

		// 1:1 拷贝（源 pitch 可能大于宽度）
		for (int y = 0; y < srcH; ++y)
		{
			memcpy(g_FullCanvas.data() + static_cast<size_t>(y) * srcW,
				pSrc + static_cast<ptrdiff_t>(y) * srcPitch,
				sizeof(unsigned short) * srcW);
		}
	}

	// 贴图：从 1:1 画布按倍率取样到 dst 矩形。
	// 关键优化：整行连续时用 memcpy；需要横向缩放时也只在行内做一次步进采样，
	// 且只 Lock/Unlock 一次合成表面（原先每帧重建缓存 + 两次 Lock 是卡顿主因）。
	void BlitScaledWords(const unsigned short* pSrc, int srcW, int srcH, int srcPitch, RectangleStruct dst)
	{
		DSurface* pDst = DSurface::Composite;
		if (!pSrc || !pDst || srcW <= 0 || srcH <= 0 || dst.Width <= 0 || dst.Height <= 0)
			return;
		if (pDst->GetBytesPerPixel() != 2)
			return;

		// 高清地形画布已经是"整张地图"的离屏缓冲，直接按缩放取样，
		// 不再复制进 g_FullCanvas（省一次整图拷贝，也避免双份内存）。
		const bool useCanvas = (pSrc == FullMapTerrain::Pixels());
		if (!useCanvas)
		{
			EnsureFullCanvas(srcW, srcH, srcPitch, pSrc);
			if (g_FullCanvas.empty())
				return;
		}

		const int cropX = useCanvas ? g_Layout.SourceX : 0;
		const int cropY = useCanvas ? g_Layout.SourceY : 0;
		const int baseW = useCanvas ? g_Layout.BmpW : g_FullW;
		const int baseH = useCanvas ? g_Layout.BmpH : g_FullH;
		const int basePitch = useCanvas ? srcPitch : g_FullW;
		const unsigned short* pBasePixels = useCanvas
			? pSrc + static_cast<ptrdiff_t>(cropY) * srcPitch + cropX
			: g_FullCanvas.data();


		const auto& vb = DSurface::ViewBounds;
		const auto& mapArea = g_Layout.MapArea;
		const int x0 = std::max({ dst.X, vb.X, 0 });
		const int y0 = std::max({ dst.Y, vb.Y, 0 });
		const int x1 = std::min({ dst.X + dst.Width, vb.X + vb.Width, pDst->Width,
			mapArea.Width > 0 ? mapArea.X + mapArea.Width : pDst->Width });
		const int y1 = std::min({ dst.Y + dst.Height, vb.Y + vb.Height, pDst->Height,
			mapArea.Height > 0 ? mapArea.Y + mapArea.Height : pDst->Height });
		const int clippedX0 = std::max(x0, mapArea.Width > 0 ? mapArea.X : 0);
		const int clippedY0 = std::max(y0, mapArea.Height > 0 ? mapArea.Y : 0);

		const int count = x1 - clippedX0;
		if (count <= 0 || y1 <= clippedY0) return;

		if (useCanvas && FullMapSettings::GPUScaling)
		{
			FullMapOpenGL::Result scaled;
			if (FullMapOpenGL::Scale({ pSrc, srcW, srcH, srcPitch, FullMapTerrain::Revision() },
				{ cropX, cropY, baseW, baseH }, { dst.X, dst.Y, dst.Width, dst.Height },
				{ clippedX0, clippedY0, count, y1 - clippedY0 }, scaled))
			{
				FullMapPerf::Scope timing(FullMapPerf::Stage::GLCopy);
				FullMapPerf::Scope lockTiming(FullMapPerf::Stage::GLLock);
				auto* target = static_cast<uint16_t*>(pDst->Lock(0, 0));
				lockTiming.Finish();
				if (target)
				{
					const int pitch = pDst->GetPitch() / 2;
					for (int row = 0; row < scaled.Height; ++row)
						memcpy(target + static_cast<ptrdiff_t>(clippedY0 + row) * pitch + clippedX0,
							scaled.Pixels + static_cast<ptrdiff_t>(row) * scaled.Pitch, static_cast<size_t>(count) * 2);
					pDst->Unlock();
					return;
				}
			}
		}
		// Any unsupported driver/GL error leaves the original CPU path intact.
		FullMapPerf::Scope timing(FullMapPerf::Stage::ScaleCPU);
		FullMapPerf::Scope lockTiming(FullMapPerf::Stage::CPULock);
		void* pDstBase = pDst->Lock(0, 0);
		lockTiming.Finish();
		if (!pDstBase) return;
		const int dstPitch = pDst->GetPitch() / 2;
		auto* pDstWords = static_cast<unsigned short*>(pDstBase);

		FullMapPerf::Scope pixelsTiming(FullMapPerf::Stage::CPUPixels);
		// 1:1 且无缩放时走整行 memcpy（最快路径）
		const bool oneToOne = (dst.Width == baseW && dst.Height == baseH);
		for (int dy = clippedY0; dy < y1; ++dy)
		{
			const int relY = dy - dst.Y;
			int sy = oneToOne ? relY : relY * baseH / dst.Height;
			sy = std::clamp(sy, 0, baseH - 1);
			const unsigned short* pSrcRow = pBasePixels + static_cast<size_t>(sy) * basePitch;
			unsigned short* pDstRow = pDstWords + static_cast<ptrdiff_t>(dy) * dstPitch + clippedX0;

			if (oneToOne)
			{
				memcpy(pDstRow, pSrcRow + (clippedX0 - dst.X), sizeof(unsigned short) * count);
			}
			else
			{
				// 横向采样用查表：列映射只依赖 (x0-dst.X, dst.Width, g_FullW)，
				// 每帧算一次表，内层循环只做一次数组读取，避免百万次整数除法。
				if (g_ColMapWidth != dst.Width || g_ColMapSrcW != baseW)
				{
					g_ColMapWidth = dst.Width;
					g_ColMapSrcW = baseW;
					g_ColMap.resize(dst.Width);
					for (int i = 0; i < dst.Width; ++i)
						g_ColMap[i] = std::clamp(i * baseW / dst.Width, 0, baseW - 1);
				}
				const int* pColMap = g_ColMap.data() + (clippedX0 - dst.X);
				for (int i = 0; i < count; ++i)
					pDstRow[i] = pSrcRow[pColMap[i]];
			}
		}

		pixelsTiming.Finish();
		FullMapPerf::Scope unlockTiming(FullMapPerf::Stage::CPUUnlock);
		pDst->Unlock();
	}

	void PrepareLayout()
	{
		if (TerrainActive())
		{
			ComputeLayout(FullMapTerrain::CanvasW, FullMapTerrain::CanvasH, 0, 0);
			return;
		}
		int w = 0, h = 0;
		GetRadarImage(w, h);
		if (w > 0 && h > 0) { g_LastRadarW = w; g_LastRadarH = h; }
		ComputeLayout(std::max(1, g_LastRadarW), std::max(1, g_LastRadarH), 0, 0);
	}

	void Render()
	{
		auto* pSurf = DSurface::Composite;
		if (!pSurf)
			return;

		auto& L = g_Layout;
		Point2D mouse { -1000, -1000 };
		if (auto* pMouse = WWMouseClass::Instance)
			mouse = pMouse->XY1;

		// 1. 背板
		RectangleStruct backdrop { L.View.X, L.View.Y, L.View.Width, L.View.Height };
		pSurf->FillRect(&backdrop, 0);

		// 每帧刷新底图：画布只在尺寸变化时重建，贴图按当前缩放从画布取样，
		// 因此缩放/平移过程中不会触发源图重采样，也不会因缓存翻新而闪屏。
		MapSource src {};
		Surface* pLockedRadar = nullptr;

		src = AcquireMapSource();
		// The terrain canvas owns its pixels and does not lock the radar surface.
		// Only the fallback source needs the matching Unlock below.
		pLockedRadar = (!src.Owned && src.Pixels)
			? reinterpret_cast<Surface*>(RadarClass::Instance.unknown_121C) : nullptr;

		if (src.Pixels && src.W > 0 && src.H > 0)
		{
			BlitScaledWords(src.Pixels, src.W, src.H, src.Pitch, L.MapRect);
		}
		else if (FullMapSettings::UseNativeRadar && !g_FullCanvas.empty())
		{
			// 雷达短暂不可用（切换视图等）时沿用画布内容，避免整屏黑一下
			BlitScaledWords(g_FullCanvas.data(), g_FullW, g_FullH, g_FullW, L.MapRect);
		}

		if (!src.Pixels && !TerrainActive())
		{
			const bool building = !FullMapSettings::UseNativeRadar && FullMapTerrain::IsBuilding();
			const wchar_t* line1 = building
				? StringTable::TryFetchString("SF:FULLMAP_LOADING", L"正在绘制地形地图…")
				: StringTable::TryFetchString("SF:FULLMAP_TERRAIN_UNAVAILABLE", L"地形数据暂不可用");
			const wchar_t* line2 = building
				? StringTable::TryFetchString("SF:FULLMAP_LOADING_HINT", L"首次打开或地图变化时正在生成高清地形画布")
				: StringTable::TryFetchString("SF:FULLMAP_NO_HINT", L"请重新打开战术地图");
			const int yellow = Drawing::RGB_To_Int(ColorStruct { 255, 210, 90 });
			const int grey = Drawing::RGB_To_Int(ColorStruct { 180, 180, 180 });
			RectangleStruct d1 = Drawing::GetTextDimensions(line1, { 0, 0 }, 0, 2, 0);
			RectangleStruct d2 = Drawing::GetTextDimensions(line2, { 0, 0 }, 0, 2, 0);
			const int cx = L.View.X + L.View.Width / 2;
			const int cy = L.View.Y + L.View.Height / 2;
			PrintText(line1, { cx - d1.Width / 2, cy - 30 }, yellow);
			PrintText(line2, { cx - d2.Width / 2, cy - 6 }, grey);
		}

		if (TerrainActive())
		{
			const RectangleStruct clip = L.PlayableClip;
			FullMapObjects::Draw({ L.MapRect, clip, L.SourceX, L.SourceY, L.BmpW, L.BmpH,
				s_Typing ? Point2D { -1000, -1000 } : mouse, s_Zoom });
		}

		if (TerrainActive())
		{
			FullMapPerf::Scope radarAnimationTiming(FullMapPerf::Stage::RadarAnimations);
			RadarMark::DrawFullMap({ pSurf, L.MapRect, L.PlayableClip,
				L.SourceX, L.SourceY, L.BmpW, L.BmpH, s_Zoom });
		}

		// 4. 标记（仅本人与当前盟友可见；中途结盟/解盟实时生效）
		//    悬停命中时给一层高亮，橡皮擦模式尤其需要"指着谁"的反馈
		FullMapPerf::Scope annotationsTiming(FullMapPerf::Stage::Annotations);
		const FullMap::Mark* pHover = HitTestMark(mouse, s_Eraser, !s_Eraser);
		for (const auto& m : g_Marks)
		{
			if (FullMap::IsMarkVisible(m))
				DrawMark(m, pHover && pHover->Id == m.Id && pHover->Author == m.Author);
		}

		// 5. 两点工具的橡皮筋预览
		if ((s_Pending || s_DragShape) && !s_Typing)
		{
			CellStruct cell {};
			if (ScreenToCell(mouse, cell))
			{
				const int grey = Drawing::RGB_To_Int(ColorStruct { 200, 200, 200 });
				const Point2D a = CellToScreen(s_PendingCell);
				const Point2D b = CellToScreen(cell);
				switch (s_ToolKind)
				{
				case FullMapMarkKind::Line:
				case FullMapMarkKind::Arrow:
				case FullMapMarkKind::Distance:
					DrawLine2(a, b, grey);
					break;
				case FullMapMarkKind::Rect:
				{
					RectangleStruct rc { std::min(a.X, b.X), std::min(a.Y, b.Y),
						std::max(1, std::abs(a.X - b.X)), std::max(1, std::abs(a.Y - b.Y)) };
					DrawRectangleOutline(rc, grey);
					break;
				}
				case FullMapMarkKind::Ellipse:
				{
					RectangleStruct rc { std::min(a.X, b.X), std::min(a.Y, b.Y),
						std::max(1, std::abs(a.X - b.X)), std::max(1, std::abs(a.Y - b.Y)) };
					DrawEllipse(rc, grey);
					break;
				}
				default:
					break;
				}
			}
		}

		annotationsTiming.Finish();
		if (src.Pixels)
		{
			FullMapPerf::Scope maskTiming(FullMapPerf::Stage::Mask);
			MaskOutsidePlayableArea();
			maskTiming.Finish();
			FullMapPerf::Scope boxTiming(FullMapPerf::Stage::CameraBox);
			DrawCurrentViewBox(mouse);
		}

		// 6. 工具栏 / 状态条
		FullMapPerf::Scope toolbarTiming(FullMapPerf::Stage::Toolbar);
		g_ToolbarDrawing = true;
		RenderToolbar(mouse);
		RenderStatus(mouse);
		g_ToolbarDrawing = false;

		// 7. 侧栏已有原版小地图，这里不再额外绘制缩略图

		// 雷达位图的 Lock 保持到帧尾
		if (pLockedRadar)
			pLockedRadar->Unlock();
	}

	// ============================================================================
	// 交互
	// ============================================================================

	void UpdateMouseEdges()
	{
		const int frame = Unsorted::CurrentFrame;
		const bool sample = s_MouseFrame != frame;
		s_MouseFrame = frame;

		const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

		if (sample)
		{
			s_LeftJustPressed = left && !s_LeftDown;
			s_RightJustPressed = right && !s_RightDown;
			s_LeftDown = left;
			s_RightDown = right;
		}
		else
		{
			// 同一逻辑帧内的后续渲染帧不再重复触发
			s_LeftJustPressed = false;
			s_RightJustPressed = false;
		}
	}

	int HitTestToolbar(Point2D mouse)
	{
		for (const auto& btn : g_Layout.Buttons)
		{
			if (mouse.X >= btn.Rect.X && mouse.X < btn.Rect.X + btn.Rect.Width
				&& mouse.Y >= btn.Rect.Y && mouse.Y < btn.Rect.Y + btn.Rect.Height)
				return btn.Id;
		}
		return -1;
	}

	// Screen-pixel distance remains usable at any zoom, including segments
	// whose anchors are both offscreen. Cast before subtracting to avoid overflow.
	double SegmentDistanceSquared(Point2D mouse, Point2D a, Point2D b)
	{
		const double dx = static_cast<double>(b.X) - a.X;
		const double dy = static_cast<double>(b.Y) - a.Y;
		const double mx = static_cast<double>(mouse.X) - a.X;
		const double my = static_cast<double>(mouse.Y) - a.Y;
		const double length = dx * dx + dy * dy;
		const double t = length > 0.0 ? std::clamp((mx * dx + my * dy) / length, 0.0, 1.0) : 0.0;
		const double x = mx - t * dx, y = my - t * dy;
		return x * x + y * y;
	}

	// Hover/erase follows the actual outline; dragging still grabs anchors.
	// Reverse traversal makes the last drawn mark win equal-distance overlaps.
	const FullMap::Mark* HitTestMark(Point2D mouse, bool onlyOwn, bool anchorsOnly)
	{
		const auto& clip = g_Layout.PlayableClip;
		if (FullMap::IsOpen && (mouse.X < clip.X || mouse.Y < clip.Y
			|| mouse.X >= clip.X + clip.Width || mouse.Y >= clip.Y + clip.Height)) return nullptr;
		const int me = static_cast<int8_t>(PlayerIndex());
		const FullMap::Mark* best = nullptr;
		double bestDistance = 36.0 * 36.0;
		for (auto it = g_Marks.rbegin(); it != g_Marks.rend(); ++it)
		{
			const auto& mark = *it;
			if (onlyOwn ? mark.Author != me : !FullMap::IsMarkVisible(mark)) continue;
			const auto a = CellToScreen(mark.Cell);
			const auto b = CellToScreen(mark.Cell2);
			const auto consider = [&](double distance, double radius)
			{
				if (distance < radius * radius && distance < bestDistance)
				{
					bestDistance = distance;
					best = &mark;
				}
			};
			const auto point = [&](Point2D p, double radius)
			{
				consider(SegmentDistanceSquared(mouse, p, p), radius);
			};
			const auto edge = [&](Point2D from, Point2D to)
			{
				consider(SegmentDistanceSquared(mouse, from, to), 10.0);
			};
			point(a, IsBadgeKind(mark.Kind) ? 36.0 : 24.0);
			if (KindHasEndPoint(mark.Kind)) point(b, 24.0);
			if (mark.Kind == FullMapMarkKind::Freehand && mark.PointCount > 0)
			{
				const int mid = mark.PointCount / 2;
				point(CellToScreen({ mark.Points[mid][0], mark.Points[mid][1] }), 24.0);
			}
			if (IsBadgeKind(mark.Kind)) point({ a.X, a.Y + 20 }, 36.0);
			if (anchorsOnly) continue;

			switch (mark.Kind)
			{
			case FullMapMarkKind::Line:
			case FullMapMarkKind::Arrow:
			case FullMapMarkKind::Distance:
				edge(a, b);
				break;
			case FullMapMarkKind::Rect:
			case FullMapMarkKind::Ellipse:
			{
				const int left = std::min(a.X, b.X), top = std::min(a.Y, b.Y);
				const int width = std::max(1, std::abs(a.X - b.X));
				const int height = std::max(1, std::abs(a.Y - b.Y));
				if (mark.Kind == FullMapMarkKind::Rect)
				{
					const int right = left + width - 1, bottom = top + height - 1;
					edge({ left, top }, { right, top });
					edge({ right, top }, { right, bottom });
					edge({ right, bottom }, { left, bottom });
					edge({ left, bottom }, { left, top });
				}
				else
				{
					// Match DrawEllipse's 24 segments, including its integer rounding.
					const double cx = left + width / 2.0, cy = top + height / 2.0;
					const double rx = std::max(1.0, width / 2.0), ry = std::max(1.0, height / 2.0);
					Point2D first { static_cast<int>(cx + rx), static_cast<int>(cy) }, previous = first;
					for (int i = 1; i < 24; ++i)
					{
						const double angle = 2.0 * Pi * i / 24;
						const Point2D next { static_cast<int>(cx + rx * std::cos(angle)), static_cast<int>(cy + ry * std::sin(angle)) };
						edge(previous, next);
						previous = next;
					}
					edge(previous, first);
				}
				break;
			}
			case FullMapMarkKind::Freehand:
				for (int i = 1; i < mark.PointCount; ++i)
					edge(CellToScreen({ mark.Points[i - 1][0], mark.Points[i - 1][1] }),
						CellToScreen({ mark.Points[i][0], mark.Points[i][1] }));
				break;
			case FullMapMarkKind::Text:
			{
				const auto size = Drawing::GetTextDimensions(mark.Text, { 0, 0 }, 0, 2, 0);
				const int width = size.Width > 0 ? size.Width : 6 * static_cast<int>(wcslen(mark.Text));
				if (width > 0 && mouse.X >= a.X + 6 && mouse.X < a.X + 10 + width
					&& mouse.Y >= a.Y - 7 && mouse.Y < a.Y - 7 + std::max(12, size.Height))
					consider(0.0, 1.0);
				break;
			}
			default:
				break;
			}
		}
		return best;
	}

	void EraseNearestOwn(Point2D mouse)
	{
		if (const auto* pBest = HitTestMark(mouse, true))
		{
			s_DragMark = nullptr;
			CommitRemove(*pBest);
		}
	}

	void DoClearOwn()
	{
		const int8_t me = static_cast<int8_t>(PlayerIndex());
		EventExt::RaiseFullMapMarkClearOwn();

		std::vector<FullMap::Mark> keep;
		keep.reserve(g_Marks.size());
		for (const auto& m : g_Marks)
		{
			if (m.Author != me)
				keep.push_back(m);
			else
				PokeMinimap(m);
		}
		g_Marks.swap(keep);
	}

	void FlushFreehandPoints()
	{
		if (s_PointBuf.empty())
			return;

		int16_t pts[FullMap::MaxPointsPerEvent][2] {};
		const int n = std::min<int>(FullMap::MaxPointsPerEvent, static_cast<int>(s_PointBuf.size()));
		for (int i = 0; i < n; ++i)
		{
			pts[i][0] = s_PointBuf[i].first;
			pts[i][1] = s_PointBuf[i].second;
		}
		EventExt::RaiseFullMapMarkAppend(s_FreehandId, &pts[0][0], static_cast<uint8_t>(n));
		s_PointBuf.erase(s_PointBuf.begin(), s_PointBuf.begin() + n);
	}

	void AddFreehandPoint(CellStruct cell)
	{
		auto* pMark = FindMark(static_cast<int8_t>(PlayerIndex()), s_FreehandId);
		if (pMark && pMark->PointCount < FullMap::MaxPoints)
		{
			pMark->Points[pMark->PointCount][0] = cell.X;
			pMark->Points[pMark->PointCount][1] = cell.Y;
			++pMark->PointCount;
		}
		s_PointBuf.emplace_back(cell.X, cell.Y);
		if (static_cast<int>(s_PointBuf.size()) >= FullMap::MaxPointsPerEvent)
			FlushFreehandPoints();
	}

	void StartFreehand(CellStruct cell)
	{
		FullMap::Mark m {};
		m.Id = AllocId();
		m.Kind = FullMapMarkKind::Freehand;
		m.ColorIdx = s_ColorIdx;
		m.Author = static_cast<int8_t>(PlayerIndex());
		m.PlacedFrame = Unsorted::CurrentFrame;
		m.Cell = cell;
		CommitAdd(m);

		s_FreehandActive = true;
		s_FreehandId = m.Id;
		s_FreehandLastCell = cell;
		AddFreehandPoint(cell);
	}

	void HandleTypingKey(UINT vk)
	{
		// 事件内联区 22 wchar，留 1 结尾位 → 有效文字上限 21，与同步端一致
		constexpr size_t cap = FullMap::MaxPointsPerEvent - 1;

		if (vk == VK_BACK)
		{
			if (!s_TextBuf.empty())
				s_TextBuf.pop_back();
			return;
		}
		if (vk == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000))
		{
			// Ctrl+V：从剪贴板粘贴文字（游戏内无法挂 IME，中文用"别处输入→复制→粘贴"）
			if (OpenClipboard(nullptr))
			{
				if (HANDLE hData = GetClipboardData(CF_UNICODETEXT))
				{
					if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(hData)))
					{
						for (const wchar_t* p = text; *p && s_TextBuf.size() < cap; ++p)
						{
							if (*p != L'\r' && *p != L'\n' && *p != L'\t')
								s_TextBuf += *p;
						}
						GlobalUnlock(hData);
					}
				}
				CloseClipboard();
			}
			return;
		}
		if (vk == VK_RETURN)
		{
			if (!s_TextBuf.empty())
			{
				FullMap::Mark m {};
				m.Id = AllocId();
				m.Kind = FullMapMarkKind::Text;
				m.ColorIdx = s_ColorIdx;
				m.Author = static_cast<int8_t>(PlayerIndex());
				m.PlacedFrame = Unsorted::CurrentFrame;
				m.Cell = s_TypingAnchor;
				m.TextLen = static_cast<uint8_t>(std::min(cap, s_TextBuf.size()));
				wcsncpy_s(m.Text, s_TextBuf.c_str(), m.TextLen);
				CommitAdd(m);
			}
			s_Typing = false;
			s_TextBuf.clear();
			return;
		}
		if (vk == VK_ESCAPE)
		{
			s_Typing = false;
			s_TextBuf.clear();
			return;
		}

		wchar_t ch = 0;
		const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

		if (vk == VK_SPACE)
			ch = L' ';
		else if (vk >= '0' && vk <= '9')
			ch = static_cast<wchar_t>(L'0' + (vk - '0'));
		else if (vk >= 'A' && vk <= 'Z')
			ch = shift ? static_cast<wchar_t>(vk) : static_cast<wchar_t>(vk - 'A' + L'a');
		else if (vk == VK_OEM_MINUS)
			ch = shift ? L'_' : L'-';
		else if (vk == VK_OEM_PERIOD)
			ch = L'.';
		else if (vk == VK_OEM_COMMA)
			ch = L',';
		else if (vk == VK_OEM_1)
			ch = L':';
		else if (vk == VK_OEM_2)
			ch = L'/';
		else if (vk == VK_OEM_7)
			ch = L'\'';

		if (ch && s_TextBuf.size() < cap)
			s_TextBuf += ch;
	}

	void PollTypingKeys()
	{
		static const UINT keys[] = {
			VK_BACK, VK_RETURN, VK_ESCAPE, VK_SPACE, VK_SHIFT,
			'0','1','2','3','4','5','6','7','8','9',
			'A','B','C','D','E','F','G','H','I','J','K','L','M',
			'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
			VK_OEM_MINUS, VK_OEM_PERIOD, VK_OEM_COMMA, VK_OEM_1, VK_OEM_2, VK_OEM_7,
		};

		for (UINT vk : keys)
		{
			const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
			if (down && !s_KeyWasDown[vk])
				HandleTypingKey(vk);
			s_KeyWasDown[vk] = down;
		}
	}

	void JumpToCell(CellStruct cell)
	{
		// SetTacticalPosition（0x6D6070）同时写入当前量与目标量并刷新可见格：
		// 立即落位，无 FocusOn 的平滑滚动
		if (auto* pCell = MapClass::Instance.TryGetCellAt(cell))
		{
			CoordStruct dest { cell.X * 256 + 128, cell.Y * 256 + 128,
				static_cast<int>(pCell->Level) * Unsorted::LevelHeight };
			if (auto* pTactical = TacticalClass::Instance)
				pTactical->SetTacticalPosition(&dest);
		}
	}

	// 白色视野框在当前屏幕上的矩形（与 Render 里的换算保持一致）
	RectangleStruct GetViewBoxOnScreen()
	{
		return ViewBoxScreen();
	}

	bool PointInRect(Point2D pt, const RectangleStruct& rc)
	{
		return pt.X >= rc.X && pt.X < rc.X + rc.Width
			&& pt.Y >= rc.Y && pt.Y < rc.Y + rc.Height;
	}

	// 两点工具落标（拖动绘制与两击兜底共用）
	void PlaceTwoPointMark(CellStruct a, CellStruct b)
	{
		FullMap::Mark m {};
		m.Id = AllocId();
		m.Kind = s_ToolKind;
		m.ColorIdx = s_ColorIdx;
		m.Author = static_cast<int8_t>(PlayerIndex());
		m.PlacedFrame = Unsorted::CurrentFrame;
		m.Cell = a;
		m.Cell2 = b;
		CommitAdd(m);
	}

	void HandleToolbarClick(int id)
	{
		if (id == BtnZoomIn || id == BtnZoomOut)
		{
			s_ZoomUseCenter = true;
			const double factor = id == BtnZoomIn ? FullMapSettings::ZoomStep : 1.0 / FullMapSettings::ZoomStep;
			s_ZoomTarget = static_cast<float>(std::clamp(s_ZoomTarget * factor, 1.0, FullMapSettings::MaxZoom));
			return;
		}

		if (id == BtnNoTool)
		{
			SelectViewTool();
			return;
		}

		if (id >= 0 && id < static_cast<int>(FullMapMarkKind::COUNT))
		{
			s_ToolKind = static_cast<FullMapMarkKind>(id);
			s_Eraser = false;
			s_NoTool = false;
			s_Pending = false;
		}
		else if (id == BtnEraser)
		{
			s_Eraser = !s_Eraser;
			s_NoTool = false;
			s_Pending = false;
		}
		else if (id >= BtnColorBase && id < BtnColorBase + PaletteCount)
		{
			s_ColorIdx = static_cast<uint8_t>(id - BtnColorBase);
		}
		else if (id == BtnClearOwn)
		{
			DoClearOwn();
		}
		else if (id == BtnClose)
		{
			FullMap::ForceClose();
		}
		else if (id == BtnBadgeCat)
		{
			// 已移除开关（徽章常驻底部），空分支
		}
		else if (id >= BtnBadgeBase && id < BtnBadgeBase + 6)
		{
			s_ToolKind = static_cast<FullMapMarkKind>(static_cast<int>(FullMapMarkKind::Attack) + (id - BtnBadgeBase));
			s_Eraser = false;
			s_NoTool = false;
			s_Pending = false;
		}
	}

	// 纯点击（按下-抬起且未拖动）落在地图区域
	void HandleMapClick(Point2D mouse)
	{
		if (s_Typing)
		{
			s_Typing = false;
			s_TextBuf.clear();
			return;
		}

		// Erasing is an edit action, not a camera double-click or a terrain pick.
		// A mark can cover cliff/margin pixels that do not map to a terrain cell.
		if (!s_NoTool && s_Eraser)
		{
			s_LastClickTick = 0;
			s_LastClickPos = { -1000, -1000 };
			EraseNearestOwn(mouse);
			return;
		}

		CellStruct cell {};
		if (!ScreenToCell(mouse, cell))
			return;

		// 双击：跳转视角并关闭
		const DWORD now = timeGetTime();
		const int dx = mouse.X - s_LastClickPos.X;
		const int dy = mouse.Y - s_LastClickPos.Y;
		if (now - s_LastClickTick < DoubleClickMs && dx * dx + dy * dy < DoubleClickDist * DoubleClickDist)
		{
			s_LastClickTick = 0;
			s_LastClickPos = { -1000, -1000 };
			ResetInteractionState();
			JumpToCell(cell);
			FullMap::ForceClose();
			return;
		}
		s_LastClickTick = now;
		s_LastClickPos = mouse;

		if (s_NoTool)
			return;

		switch (s_ToolKind)
		{
		case FullMapMarkKind::Cross:
		case FullMapMarkKind::Triangle:
		case FullMapMarkKind::Crosshair:
		case FullMapMarkKind::Attack:
		case FullMapMarkKind::Defend:
		case FullMapMarkKind::Ore:
		case FullMapMarkKind::Tech:
		case FullMapMarkKind::Rally:
		case FullMapMarkKind::Danger:
		{
			FullMap::Mark m {};
			m.Id = AllocId();
			m.Kind = s_ToolKind;
			m.ColorIdx = s_ColorIdx;
			m.Author = static_cast<int8_t>(PlayerIndex());
			m.PlacedFrame = Unsorted::CurrentFrame;
			m.Cell = cell;
			CommitAdd(m);
			break;
		}
		case FullMapMarkKind::Line:
		case FullMapMarkKind::Arrow:
		case FullMapMarkKind::Rect:
		case FullMapMarkKind::Ellipse:
		case FullMapMarkKind::Distance:
		{
			if (!s_Pending)
			{
				s_Pending = true;
				s_PendingCell = cell;
			}
			else
			{
				PlaceTwoPointMark(s_PendingCell, cell);
				s_Pending = false;
			}
			break;
		}
		case FullMapMarkKind::Text:
			s_TypingAnchor = cell;
			s_TextBuf.clear();
			// 原生输入：与联机 Enter 聊天同一套（AddEdit 创建编辑行，IME 中文可用）。
			// 注意 SetEditFocus 只把焦点转给已存在的编辑行，不会创建。
			if (auto* pList = &MessageListClass::Instance)
			{
				wchar_t emptyText[1] { L'\0' };
				pList->AddEdit(0, static_cast<TextPrintType>(0x4046), emptyText, 0, 640);
				s_NativeTyping = pList->IsEdit;
				Debug::Log("[FullMap] native edit start IsEdit=%d\n", pList->IsEdit ? 1 : 0);
			}
			s_Typing = !s_NativeTyping;
			break;
		case FullMapMarkKind::Freehand:
			StartFreehand(cell);
			break;
		default:
			break;
		}
	}

	// 拖动端点：只改被抓住的那一端，另一端保持不动（矩形/椭圆/直线/箭头因此变形）
	void SetMarkAnchor(FullMap::Mark& m, CellStruct cell, bool bMoveCell2)
	{
		if (bMoveCell2)
			m.Cell2 = cell;
		else
			m.Cell = cell;
	}

	// 点类标记（X/三角/徽章/文字/手绘）：整体平移
	void TranslateMarkTo(FullMap::Mark& m, CellStruct target, int dxCells, int dyCells)
	{
		m.Cell.X = static_cast<int16_t>(m.Cell.X + dxCells);
		m.Cell.Y = static_cast<int16_t>(m.Cell.Y + dyCells);
		m.Cell2.X = static_cast<int16_t>(m.Cell2.X + dxCells);
		m.Cell2.Y = static_cast<int16_t>(m.Cell2.Y + dyCells);
		for (int i = 0; i < m.PointCount; ++i)
		{
			m.Points[i][0] = static_cast<int16_t>(m.Points[i][0] + dxCells);
			m.Points[i][1] = static_cast<int16_t>(m.Points[i][1] + dyCells);
		}
		(void)target;
	}

	void HandleRightClick(Point2D mouse)
	{
		if (HitTestToolbar(mouse) >= 0)
			return;
		if (!s_NoTool)
		{
			SelectViewTool();
			return;
		}
		EraseNearestOwn(mouse);
	}

	// 滚轮输入由 PeekMessageA 拦截转发（见 Hooks.cpp 的 InstallInputHooks）：
	// 游戏自己的消息泵会先于渲染期轮询消费掉 WM_MOUSEWHEEL，这里不再自己 peek。
	void PollWheelZoom()
	{
	}

	// IME 组合窗与候选窗定位到标签锚点附近（贴近常见输入体验）
	void UpdateImePosition()
	{
		const auto* pMouse = WWMouseClass::Instance;
		if (!pMouse || !pMouse->hWnd)
			return;

		HIMC hImc = ImmGetContext(pMouse->hWnd);
		if (!hImc)
			return;

		const Point2D pos = CellToScreen(s_TypingAnchor);
		COMPOSITIONFORM cf {};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos = { pos.X + 10, pos.Y - 24 };
		ImmSetCompositionWindow(hImc, &cf);

		CANDIDATEFORM cand {};
		cand.dwIndex = 0;
		cand.dwStyle = CFS_CANDIDATEPOS;
		cand.ptCurrentPos = { pos.X + 10, pos.Y + 6 };
		ImmSetCandidateWindow(hImc, &cand);

		ImmReleaseContext(pMouse->hWnd, hImc);
	}

	void PollInput()
	{
		FullMapPerf::Scope timing(FullMapPerf::Stage::Input);
		PollMinimapClose();
		if (!FullMap::IsOpen) return;
		UpdateMouseEdges();
		PollWheelZoom();

		if (s_Typing)
			PollTypingKeys();

		Point2D mouse { -1000, -1000 };
		if (auto* pMouse = WWMouseClass::Instance)
			mouse = pMouse->XY1;

		const bool leftHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool midHeld = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
		const bool haveLast = s_LastMouse.X > -500;

		// 原生输入进行中：等待回车/Esc 结束编辑，结束后取缓冲区内容落标记
		if (s_NativeTyping)
		{
			auto* pList = &MessageListClass::Instance;
			if (!pList->IsEdit)
			{
				s_NativeTyping = false;
				const wchar_t* buf = pList->GetEditBuffer();
				if (buf && *buf)
				{
					constexpr size_t textCap = FullMap::MaxPointsPerEvent - 1; // 与同步上限一致
					FullMap::Mark m {};
					m.Id = AllocId();
					m.Kind = FullMapMarkKind::Text;
					m.ColorIdx = s_ColorIdx;
					m.Author = static_cast<int8_t>(PlayerIndex());
					m.PlacedFrame = Unsorted::CurrentFrame;
					m.Cell = s_TypingAnchor;
					const size_t len = std::min(textCap, wcslen(buf));
					m.TextLen = static_cast<uint8_t>(len);
					wcsncpy_s(m.Text, buf, len);
					CommitAdd(m);
				}
			}
			else if (s_LeftJustPressed)
			{
				// 输入中点击任意处：取消
				pList->RemoveEdit();
				s_NativeTyping = false;
			}
			else
			{
				UpdateImePosition();
			}

			s_LastMouse = mouse;
			return;
		}

		// 卡顿看门狗：统计"滚轮到达→本帧消费"的延迟与帧间隔峰值
		const DWORD nowTick = timeGetTime();
		if (s_WheelArriveTick)
		{
			const DWORD age = nowTick - s_WheelArriveTick;
			if (age > s_MaxWheelAge)
				s_MaxWheelAge = age;
			s_WheelArriveTick = 0;
		}
		if (s_PrevRenderTick)
		{
			const DWORD gap = nowTick - s_PrevRenderTick;
			if (gap > s_MaxDrawGap)
				s_MaxDrawGap = gap;
		}
		s_PrevRenderTick = nowTick;

		if (!s_PerfWindowStart)
			s_PerfWindowStart = nowTick;
		++s_PerfFrames;
		if (nowTick - s_PerfWindowStart >= 2000)
		{
			Debug::Log("[FullMap] perf: frames=%d wheelMaxAge=%ums drawGapMax=%ums zoom=%.2f\n",
				s_PerfFrames, static_cast<unsigned>(s_MaxWheelAge),
				static_cast<unsigned>(s_MaxDrawGap), s_Zoom);
			s_PerfWindowStart = nowTick;
			s_PerfFrames = 0;
			s_MaxWheelAge = 0;
			s_MaxDrawGap = 0;
		}

		// 滚轮诊断（渲染侧输出：消息泵里只累计，绝不做文件 IO）
		if (s_WheelDiagCount > 0 && nowTick - s_WheelDiagLastLog >= 100)
		{
			Debug::Log("[FullMap] wheel diag: n=%d sum=%d lastAge=%ums drawDt=%ums zoom=%.3f target=%.3f frame=%d\n",
				s_WheelDiagCount, static_cast<int>(s_WheelDiagDelta),
				s_WheelDiagTick ? static_cast<unsigned>(nowTick - s_WheelDiagTick) : 0u,
				static_cast<unsigned>(g_PrevDrawDelta),
				s_Zoom, s_ZoomTarget, Unsorted::CurrentFrame);
			s_WheelDiagCount = 0;
			s_WheelDiagDelta = 0;
			s_WheelDiagLastLog = nowTick;
		}

		// 缩放插值：按真实毫秒插值（不是按帧），帧率抖动时不改变手感；
		// 时间常数 45ms → 60fps 下等价于原来的每帧 0.5
		const float zoomDt = s_LastZoomTick ? static_cast<float>(nowTick - s_LastZoomTick) : 16.0f;
		s_LastZoomTick = nowTick;
		if (std::abs(s_ZoomTarget - s_Zoom) > 0.0015f)
		{
			const float blend = 1.0f - std::exp(-std::clamp(zoomDt, 1.0f, 100.0f) / 45.0f);
			s_Zoom += (s_ZoomTarget - s_Zoom) * blend;
			if (std::abs(s_ZoomTarget - s_Zoom) < 0.01f)
				s_Zoom = s_ZoomTarget;
		}

		// 左键按下：工具栏立即响应；文字输入取消；手绘起笔；其余进入"点击/拖拽判别"
		if (s_LeftJustPressed)
		{
			s_MapPanDrag = false;
			const int btn = HitTestToolbar(mouse);
			if (btn >= 0)
			{
				HandleToolbarClick(btn);
			}
			else if (s_Typing)
			{
				s_Typing = false;
				s_TextBuf.clear();
			}
			else if (!s_NoTool && s_Eraser)
			{
				// Consume the press before mark dragging/camera gestures can steal
				// the click. Mouse-up must not submit a second deletion.
				s_DragMark = nullptr;
				s_PressDown = s_PressMoved = s_DragShape = s_ViewDrag = false;
				HandleMapClick(mouse);
			}
			else if (!s_NoTool && !s_Eraser && s_ToolKind == FullMapMarkKind::Freehand)
			{
				CellStruct cell {};
				if (ScreenToCell(mouse, cell))
					StartFreehand(cell);
			}
			else if (!s_NoTool && !s_Eraser && KindHasEndPoint(s_ToolKind) && !s_Pending)
			{
				// 现代 RTS：两点工具按下即取锚点，拖动实时预览，松开落标
				CellStruct cell {};
				if (ScreenToCell(mouse, cell))
				{
					s_Pending = true;
					s_PendingCell = cell;
					s_DragShape = true;
					s_PressDown = true;
					s_PressPos = mouse;
					s_PressMoved = false;
				}
			}
			else if (FullMap::BeginDragMarkAt(mouse))
			{
				// 抓住本人已有标记拖动：优先级高于任何工具（徽章/点标记整体平移，
				// 线/矩形/椭圆/箭头只移动被抓住的那一端）
				s_PressDown = true;
				s_PressPos = mouse;
				s_PressMoved = false;
				s_DragShape = false;
				s_ViewDrag = false;
			}
			else if (PointInRect(mouse, g_Layout.PlayableClip))
			{
				s_PressDown = true;
				s_PressPos = mouse;
				s_PressMoved = false;
				s_DragShape = false;
				const auto box = GetViewBoxOnScreen();
				const auto& clip = g_Layout.PlayableClip;
				const auto drag = FullMapGeometry::MapDragAt(
					{ static_cast<double>(mouse.X), static_cast<double>(mouse.Y) },
					{ clip.X, clip.Y, clip.Width, clip.Height },
					{ box.X, box.Y, box.Width, box.Height }, s_NoTool && FullMapSettings::LeftDragPan && s_Zoom > 1.001f);
				s_MapPanDrag = drag == FullMapGeometry::DragMode::PanMap;
				s_ViewDrag = drag == FullMapGeometry::DragMode::MoveCamera;
				s_ViewDragOffset = { box.X + box.Width / 2 - mouse.X, box.Y + box.Height / 2 - mouse.Y };
			}
		}

		// 无工具模式下，放大后左键拖动平移地图；白框边缘保留相机拖动。
		// 中键在地图区域平移，绘图工具继续使用各自的左键手势。
		const bool dragByLeft = s_PressDown && leftHeld;
		const bool dragByMiddle = midHeld && PointInRect(mouse, g_Layout.MapArea);
		if (haveLast && (dragByLeft || dragByMiddle))
		{
			const bool wasMoved = s_PressMoved;
			if (dragByLeft && std::abs(mouse.X - s_PressPos.X) + std::abs(mouse.Y - s_PressPos.Y) > PanDragDist)
				s_PressMoved = true;

			if (dragByMiddle || (s_MapPanDrag && s_PressMoved))
			{
				const auto from = !dragByMiddle && !wasMoved ? s_PressPos : s_LastMouse;
				s_PanOffset.X += mouse.X - from.X;
				s_PanOffset.Y += mouse.Y - from.Y;
				// A completed pan must not count as the first click of a double click.
				s_LastClickTick = 0;
				s_LastClickPos = { -1000, -1000 };
			}
			else if (s_DragMark)
			{
				FullMap::UpdateDragMarkTo(mouse);
			}
			else if (s_PressMoved && !s_DragShape && s_ViewDrag)
			{
				CellStruct cell {};
				if (ScreenToCell({ mouse.X + s_ViewDragOffset.X, mouse.Y + s_ViewDragOffset.Y }, cell))
					JumpToCell(cell);
			}
		}

		// 左键抬起：拖动绘制 → 落标；未拖动 → 两击兜底/点击；否则走点击
		if (s_PressDown && !leftHeld)
		{
			if (s_DragShape)
			{
				if (s_PressMoved)
				{
					CellStruct end {};
					if (ScreenToCell(mouse, end))
					{
						PlaceTwoPointMark(s_PendingCell, end);
						s_Pending = false;
					}
					else
					{
						// 拖出地图：放弃本次绘制
						s_Pending = false;
					}
				}
				// 未拖动：保留 s_Pending，下一次单击即完成（精确两点模式兜底）
				s_DragShape = false;
			}
			else if (s_DragMark)
			{
				if (s_PressMoved)
					FullMap::EndDragMark();
				else
					s_DragMark = nullptr;   // 没移动：当作普通点击
			}
			else if (!s_PressMoved && FullMap::IsOpen)
			{
				HandleMapClick(mouse);
			}
			s_PressDown = false;
			s_ViewDrag = false;
			s_MapPanDrag = false;
		}

		if (s_RightJustPressed && FullMap::IsOpen)
			HandleRightClick(mouse);

		// 手绘持续采点
		if (s_FreehandActive)
		{
			if (!leftHeld)
			{
				FlushFreehandPoints();
				s_FreehandActive = false;
				s_FreehandId = 0;
			}
			else
			{
				CellStruct cell {};
				if (ScreenToCell(mouse, cell) && (cell.X != s_FreehandLastCell.X || cell.Y != s_FreehandLastCell.Y))
				{
					s_FreehandLastCell = cell;
					AddFreehandPoint(cell);
				}
			}
		}

		s_LastMouse = mouse;
	}
}

// ============================================================================
// 公开接口
// ============================================================================

bool FullMap::IsOpen = false;
bool FullMap::Enabled = true;
int FullMap::MarkLimit = FullMap::MaxMarks;

bool FullMap::ShouldBlockGameInput()
{
	if (!IsOpen)
		return false;
	if (!HouseClass::CurrentPlayer)
		return false;
	// 渲染停滞后（切菜单/结算屏）不再拦截
	return timeGetTime() - g_LastDrawTick < 1000;
}

void FullMap::Toggle()
{
	if (!HouseClass::CurrentPlayer)
		return;

	if (IsOpen)
	{
		ForceClose();
	}
	else if (Enabled)
	{
		InstallInputHooks();

		// 关闭 FullMap 不销毁静态 TMP 画布；重开时复用它。
		// 只有切图、地形图块或地图几何发生变化才重新构建。
		FullMapOpenGL::Reset();
		FullMapPerf::Reset();
		if (!FullMapSettings::UseNativeRadar)
		{
			if (!FullMapTerrain::IsCompatibleWithCurrentMap())
				FullMapTerrain::Invalidate();
			else
				FullMapTerrain::RefreshForOpen();
		}
		FullMapObjects::Invalidate();
		g_Layout = {};

		IsOpen = true;
		g_LastDrawTick = timeGetTime();
		s_MinimapDoubleClick.Reset((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		s_Zoom = 1.0f;
		s_ZoomTarget = 1.0f;
		ResetInteractionState();

		int w = 0, h = 0;
		Surface* pImg = GetRadarImage(w, h);
		const auto& vr = RadarClass::Instance.unknown_rect_14DC;
		const auto& rr = RadarClass::Instance.unknown_rect_149C;
		Debug::Log("[FullMap] open: radar surf=%p %dx%d, factor=%.3f, avail=%d, marks=%zu,"
			" view=(%d,%d %dx%d) radarRect=(%d,%d %dx%d) fields(14C4=%u 14C8=%u)\n",
			static_cast<void*>(pImg), w, h,
			RadarClass::Instance.RadarSizeFactor,
			RadarClass::Instance.IsAvailableNow ? 1 : 0,
			g_Marks.size(),
			vr.X, vr.Y, vr.Width, vr.Height,
			rr.X, rr.Y, rr.Width, rr.Height,
			RadarClass::Instance.unknown_14C4, RadarClass::Instance.unknown_14C8);
		if (FullMapSettings::PerformanceLogging)
		{
			SYSTEM_INFO system {}; GetSystemInfo(&system);
			DWORD_PTR processAffinity = 0, systemAffinity = 0;
			GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity);
			Debug::Log("[FullMap] profile start: interval=%dms slowThreshold=%dms gpuRequested=%d logicalCPUs=%lu processAffinity=0x%llX systemAffinity=0x%llX composite=%dx%d bpp=%d videoRam=%d\n",
				FullMapSettings::PerformanceInterval, FullMapSettings::SlowFrameThreshold,
				FullMapSettings::GPUScaling, system.dwNumberOfProcessors,
				static_cast<unsigned long long>(processAffinity), static_cast<unsigned long long>(systemAffinity),
				DSurface::Composite ? DSurface::Composite->Width : 0,
				DSurface::Composite ? DSurface::Composite->Height : 0,
				DSurface::Composite ? DSurface::Composite->GetBytesPerPixel() : 0,
				DSurface::Composite ? DSurface::Composite->IsInVideoRam : false);
		}
	}
}

void FullMap::ForceClose()
{
	FullMapOpenGL::Reset();
	FullMapPerf::Report(true);
	if (FullMapSettings::PerformanceLogging) Debug::FlushLogFile();
	FullMapPerf::Reset();
	IsOpen = false;
	s_MinimapDoubleClick.Reset((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
	s_Zoom = 1.0f;
	s_ZoomTarget = 1.0f;
	ResetInteractionState();
}

void FullMap::OnDraw()
{
	if (!IsOpen)
		return;

	if (!HouseClass::CurrentPlayer || !DSurface::Composite)
	{
		ForceClose();
		return;
	}

	FullMapPerf::BeginFrame();
	FullMapPerf::Scope frameTiming(FullMapPerf::Stage::Frame);
	{
		const DWORD now = timeGetTime();
		g_PrevDrawDelta = g_LastDrawTick ? now - g_LastDrawTick : 0;
		g_LastDrawTick = now;
	}

	// 打开期间持续清掉框选橡皮筋（点击已被拦截，这里只擦视觉）
	if (auto* pTactical = TacticalClass::Instance)
		pTactical->Band = LTRBStruct {};

	// 额外保险：清零游戏按键镜像表里的鼠标键条目（sub_53EC90 轮询路径）
	// 与主循环攒下的鼠标事件位；主拦截在 IsKeyPressed(0x54F5C0) 钩子。
	if (*reinterpret_cast<DWORD*>(KeyMirrorFlagAddr) != 0)
	{
		auto* keyMirror = reinterpret_cast<short*>(KeyMirrorAddr);
		keyMirror[VK_LBUTTON] = 0;
		keyMirror[VK_RBUTTON] = 0;
		keyMirror[VK_MBUTTON] = 0;
		keyMirror[VK_XBUTTON1] = 0;
		keyMirror[VK_XBUTTON2] = 0;
	}
	*reinterpret_cast<DWORD*>(MouseButtonFlagsAddr) = 0;

	const DWORD t0 = timeGetTime();
	PumpTerrain();
	PrepareLayout();
	PollInput();
	if (!IsOpen) return;
	PrepareLayout();
	const DWORD t1 = timeGetTime();
	Render();
	// FullMap owns the cursor while blocking native hover/scroll updates.
	// SetCursor also replaces the saved cursor used by RestoreCursor, so an
	// edge-scroll arrow cannot persist when the mouse returns to the map.
	if (IsOpen) MouseClass::Instance.SetCursor(MouseCursorType::Default, false);
	const DWORD t2 = timeGetTime();
	frameTiming.Finish();
	const bool slowProfile = FullMapPerf::SlowFrame();
	const bool summaryProfile = FullMapPerf::Report();
	if (slowProfile || summaryProfile)
	{
		const auto& L = g_Layout;
		Debug::Log("[FullMap] profile scene: logicFrame=%d canvas=%dx%d destination=%dx%d panel=%dx%d clip=%dx%d zoom=%.3f target=%.3f marks=%zu ready=%d building=%d revision=%llu source=%s gpuRequested=%d gpuActive=%d\n",
			Unsorted::CurrentFrame, L.BmpW, L.BmpH, L.MapRect.Width, L.MapRect.Height,
			L.MapArea.Width, L.MapArea.Height, L.PlayableClip.Width, L.PlayableClip.Height,
			s_Zoom, s_ZoomTarget, g_Marks.size(), FullMapTerrain::IsReady(), FullMapTerrain::IsBuilding(),
			FullMapTerrain::Revision(), FullMapSettings::UseNativeRadar ? "native-radar" : "terrain",
			FullMapSettings::GPUScaling, FullMapOpenGL::Active());
		Debug::FlushLogFile();
	}

	// 慢帧诊断：只记录真正卡顿的帧，避免诊断本身成为负担
	const DWORD pollMs = t1 - t0;
	const DWORD renderMs = t2 - t1;
	const DWORD totalMs = t2 - t0;
	static DWORD s_LastSlowLog = 0;
	if ((totalMs >= 40 || g_PrevDrawDelta >= 80) && timeGetTime() - s_LastSlowLog >= 500)
	{
		s_LastSlowLog = timeGetTime();
		Debug::Log("[FullMap] slow: total=%ums poll=%ums render=%ums drawGap=%ums zoom=%.3f marks=%zu\n",
			static_cast<unsigned>(totalMs), static_cast<unsigned>(pollMs),
			static_cast<unsigned>(renderMs), static_cast<unsigned>(g_PrevDrawDelta),
			s_Zoom, g_Marks.size());
	}
}

// 战术视野内绘制标记（世界层钩子 0x6D4684 调用；
// RA2MD.ini [Scaffold] FullMapMarksInWorld 控制，默认开启）
void FullMap::DrawWorldMarks()
{
	if (IsOpen || g_Marks.empty() || !Scaffold::Config::FullMapMarksInWorld)
		return;

	auto* pTac = TacticalClass::Instance;
	if (!pTac || !DSurface::Composite)
		return;

	auto* pPlayer = HouseClass::CurrentPlayer;
	const int white = Drawing::RGB_To_Int(ColorStruct { 255, 255, 255 });

	// 战术视野内的标记：比全屏图小一号但清晰可辨（线加粗 1px）
	constexpr int R = 6;
	constexpr int ER = 3;

	for (const auto& m : g_Marks)
	{
		if (!IsMarkVisible(m))
			continue;

		const bool own = pPlayer && m.Author == pPlayer->ArrayIndex;
		const int intentInt = Drawing::RGB_To_Int(ResolveColor(m));

		auto drawEndW = [&](Point2D c)
		{
			if (own)
				DrawDisc(c, ER + 2, white);
			DrawDisc(c, ER, 0);
			DrawDisc(c, std::max(1, ER - 1), intentInt);
		};
		auto drawBaseW = [&](Point2D c)
		{
			if (own)
				DrawDisc(c, R + 2, white);
			DrawDisc(c, R, 0);
			DrawDisc(c, R - 1, intentInt);
		};

		const CoordStruct ca { m.Cell.X * 256 + 128, m.Cell.Y * 256 + 128, 0 };
		const auto [pa, visa] = pTac->CoordsToClient(ca);

		if (KindHasEndPoint(m.Kind))
		{
			const CoordStruct cb { m.Cell2.X * 256 + 128, m.Cell2.Y * 256 + 128, 0 };
			const Point2D pb = pTac->CoordsToClient(cb).first;
			// CoordsToClient's bool only tests a margin around each endpoint
			// (-360/-180 in YRpp), not whether the shape crosses the viewport.
			// Long lines/boxes/ellipses must reach the segment clipping below,
			// even when BOTH anchors are far outside the current camera.

			switch (m.Kind)
			{
			case FullMapMarkKind::Rect:
			{
				RectangleStruct rc {
					std::min(pa.X, pb.X), std::min(pa.Y, pb.Y),
					std::max(1, std::abs(pa.X - pb.X)), std::max(1, std::abs(pa.Y - pb.Y)) };
				DrawRectangleOutline(rc, intentInt);
				break;
			}
			case FullMapMarkKind::Ellipse:
			{
				RectangleStruct rc {
					std::min(pa.X, pb.X), std::min(pa.Y, pb.Y),
					std::max(1, std::abs(pa.X - pb.X)), std::max(1, std::abs(pa.Y - pb.Y)) };
				DrawEllipse(rc, intentInt);
				break;
			}
			default:
				DrawLine2(pa, pb, intentInt);
				if (m.Kind == FullMapMarkKind::Arrow)
					DrawArrowHead(pb, pa, 16, intentInt);
				if (m.Kind == FullMapMarkKind::Distance)
				{
					const int dx = m.Cell2.X - m.Cell.X;
					const int dy = m.Cell2.Y - m.Cell.Y;
					const int dist = static_cast<int>(std::sqrt(dx * dx + dy * dy) + 0.5);
					wchar_t buf[32];
					swprintf_s(buf, StringTable::TryFetchString("SF:FULLMAP_DISTANCE_FMT", L"%d格"), dist);
					PrintTextWithBacking(buf, { (pa.X + pb.X) / 2 + 6, (pa.Y + pb.Y) / 2 - 6 }, white);
				}
				break;
			}
			drawEndW(pa);
			drawEndW(pb);
		}
		else if (m.Kind == FullMapMarkKind::Freehand)
		{
			for (int i = 1; i < m.PointCount; ++i)
			{
				const CoordStruct c1 { m.Points[i - 1][0] * 256 + 128, m.Points[i - 1][1] * 256 + 128, 0 };
				const CoordStruct c2 { m.Points[i][0] * 256 + 128, m.Points[i][1] * 256 + 128, 0 };
				DrawLine2(pTac->CoordsToClient(c1).first, pTac->CoordsToClient(c2).first, intentInt);
			}
		}
		else if (visa)
		{
			switch (m.Kind)
			{
			case FullMapMarkKind::Cross:
				drawBaseW(pa);
				DrawLine2({ pa.X - 4, pa.Y - 4 }, { pa.X + 4, pa.Y + 4 }, white);
				DrawLine2({ pa.X - 4, pa.Y + 4 }, { pa.X + 4, pa.Y - 4 }, white);
				break;
			case FullMapMarkKind::Triangle:
				drawBaseW(pa);
				DrawFilledTriangle({ pa.X, pa.Y - 6 }, 4, 8, white);
				break;
			case FullMapMarkKind::Crosshair:
				drawBaseW(pa);
				DrawLine2({ pa.X - 5, pa.Y }, { pa.X + 5, pa.Y }, white);
				DrawLine2({ pa.X, pa.Y - 5 }, { pa.X, pa.Y + 5 }, white);
				break;
			case FullMapMarkKind::Text:
				if (own)
					DrawDisc(pa, 7, white);
				DrawDisc(pa, 5, 0);
				DrawDisc(pa, 4, intentInt);
				PrintTextWithBacking(m.Text, { pa.X + 8, pa.Y - 6 }, white);
				break;
			default:
				if (IsBadgeKind(m.Kind))
				{
					// 世界层徽章：放大版（R=13），含义色 + 常驻圆环（不播放放置脉冲）
					constexpr int WR = 13;
					const ColorStruct meaning = BadgeMeaningColor(m.Kind);
					const int meaningInt = Drawing::RGB_To_Int(meaning);
					const int darkInt = Drawing::RGB_To_Int(ColorStruct { 22, 23, 28 });

					// 扩散环：与世界层尺寸匹配，同样渐隐
					for (int i = 0; i < BadgeRingCount; ++i)
					{
						const int phase = BadgeRingPeriod * i / BadgeRingCount;
						const int rr = BadgeRingRadius(Unsorted::CurrentFrame, phase) * 2 / 3;
						const int alpha = BadgeRingOpacity(rr);
						if (alpha <= 2)
							continue;
						DrawEllipseFade(pa, WR + 3 + rr, WR + 3 + rr, meaningInt, alpha);
					}

					if (own)
						DrawDisc(pa, WR + 3, white);
					DrawDisc(pa, WR + 2, 0);
					DrawDisc(pa, WR, meaningInt);
					DrawDisc(pa, WR - 3, darkInt);
					DrawBadgeGlyph(m.Kind, pa, WR - 6, white);
					const ColorStruct idColor = GetHouse(m.Author)
						? GetHouse(m.Author)->Color : ColorStruct { 255, 255, 255 };
					Point2D pd { pa.X + WR - 2, pa.Y - WR + 2 };
					DrawDisc(pd, 2, 0);
					DrawDisc(pd, 1, Drawing::RGB_To_Int(idColor));
					const wchar_t* label = BadgeLabel(m.Kind);
					RectangleStruct dims = Drawing::GetTextDimensions(label, { 0, 0 }, 0, 2, 0);
					PrintTextWithBacking(label, { pa.X - dims.Width / 2, pa.Y + WR + 2 }, white);
				}
				break;
			}
		}
	}
}

bool FullMap::IsLocalAuthor(int8_t author)
{
	return author >= 0 && author == PlayerIndex();
}

bool FullMap::IsMarkVisible(const Mark& m)
{
	if (m.Author < 0)
		return false;
	const int me = PlayerIndex();
	if (m.Author == me)
		return true;

	auto* pPlayer = HouseClass::CurrentPlayer;
	auto* pAuthor = GetHouse(m.Author);
	return pPlayer && pAuthor && pPlayer->IsAlliedWith(pAuthor);
}

bool FullMap::IsTypingActive()
{
	return s_NativeTyping || s_Typing || MessageListClass::Instance.IsEdit;
}

// 世界层右键删除：命中本人的标记（锚点/终点 12px、徽章 16px 内，后放的优先）则移除。
// 在窗口过程里同步调用，命中即吞掉该次右键，不影响普通右键取消操作。
bool FullMap::TryEraseMarkAtScreen(Point2D pos)
{
	if (IsOpen)
		return false;

	auto* pTac = TacticalClass::Instance;
	if (!pTac)
		return false;

	const int me = PlayerIndex();

	FullMap::Mark* pBest = nullptr;
	int bestDist = 16 * 16;

	for (auto& m : g_Marks)
	{
		if (m.Author != me)
			continue;

		const int hitR = IsBadgeKind(m.Kind) ? 16 : 12;
		const CoordStruct ca { m.Cell.X * 256 + 128, m.Cell.Y * 256 + 128, 0 };
		const Point2D pa = pTac->CoordsToClient(ca).first;

		const Point2D cand[2] = { pa, pTac->CoordsToClient(
			{ m.Cell2.X * 256 + 128, m.Cell2.Y * 256 + 128, 0 }).first };

		for (const Point2D& c : cand)
		{
			const int dx = c.X - pos.X;
			const int dy = c.Y - pos.Y;
			const int dist = dx * dx + dy * dy;
			if (dist < hitR * hitR && dist <= bestDist)
			{
				bestDist = dist;
				pBest = &m;
			}
		}
	}

	if (pBest)
	{
		CommitRemove(*pBest);
		return true;
	}
	return false;
}

bool FullMap::HandleNativeMinimapClick(unsigned char eventFlags)
{
	// Once open, the native radar input is blocked; closing is processed in
	// PollInput using the same bounds and gesture. Never count one click twice.
	if (IsOpen || !Enabled || !HouseClass::CurrentPlayer || !InputManagerClass::Instance)
		return false;
	const bool leftDown = (eventFlags & static_cast<unsigned char>(MouseEvent::LeftDown)) != 0;
	const bool leftUp = (eventFlags & static_cast<unsigned char>(MouseEvent::LeftUp)) != 0;
	if (!leftDown && !leftUp) return false;

	const auto* inputWords = reinterpret_cast<const int*>(InputManagerClass::Instance);
	const auto point = NativeRadarPoint({ inputWords[0], inputWords[1] });
	if (!TrackMinimapClick(leftDown, point)) return false;

	Debug::Log("[FullMap] minimap toggle source=native-radar stateBefore=0 stateAfter=1 radar=(%d,%d)\n",
		point.X, point.Y);
	Toggle();
	return IsOpen;
}

bool FullMap::IsMapToggleKey(DWORD key)
{
	// 放行规则：
	//   1. ESC —— 任何时候都要能关地图；
	//   2. 玩家在"选项→键盘"里给 Toggle_full_map 绑定的那个键。
	// 后者通过 CommandClass::Hotkeys（按键→命令 的索引）反查，
	// 这样无论玩家绑什么键都能关掉地图，也不会误放行其它热键。
	const DWORD vk = key & 0xFF;
	if (vk == VK_ESCAPE)
		return true;

	auto& hotkeys = CommandClass::Hotkeys;
	if (hotkeys.Count() > 0)
	{
		// 索引是"按键 → 命令"，遍历找名字是 Toggle_full_map 的条目
		for (auto& node : hotkeys)
		{
			CommandClass* pCommand = node.Data;
			if (!pCommand)
				continue;
			const char* pName = pCommand->GetName();
			if (pName && strcmp(pName, "Toggle_full_map") == 0)
			{
				// 热键索引的 key 可能带修饰位，只比较低字节
				if (static_cast<DWORD>(node.ID & 0xFF) == vk)
					return true;
			}
		}
	}

	return false;
}

bool FullMap::BeginDragMarkAt(Point2D screenPos)
{
	if (s_Eraser) return false;
	const auto* p = HitTestMark(screenPos, true, true);
	if (!p)
		return false;

	s_DragMark = FindMark(p->Author, p->Id);
	if (!s_DragMark)
		return false;

	// 两点类标记：按下的位置更靠近哪一端，就只拖那一端（另一端保持不动）
	s_DragMarkEnd2 = false;
	if (KindHasEndPoint(s_DragMark->Kind))
	{
		const Point2D a = CellToScreen(s_DragMark->Cell);
		const Point2D b = CellToScreen(s_DragMark->Cell2);
		const double da = SegmentDistanceSquared(screenPos, a, a);
		const double db = SegmentDistanceSquared(screenPos, b, b);
		s_DragMarkEnd2 = db < da;
	}

	// 记录抓取点相对锚点的偏移，避免按下瞬间标记"瞬移"到光标下
	s_DragMarkGrab = s_DragMarkEnd2 ? s_DragMark->Cell2 : s_DragMark->Cell;
	s_DragMarkStart = screenPos;
	return true;
}

void FullMap::UpdateDragMarkTo(Point2D screenPos)
{
	if (!s_DragMark)
		return;

	CellStruct cell {};
	if (!ScreenToCell(screenPos, cell))
		return;

	PokeMinimap(*s_DragMark);

	if (KindHasEndPoint(s_DragMark->Kind))
	{
		// 只移动被抓住的那一端：矩形/椭圆/直线/箭头随之变形
		SetMarkAnchor(*s_DragMark, cell, s_DragMarkEnd2);
	}
	else
	{
		// 点类标记整体平移（保持与光标相对位置，不瞬移）
		TranslateMarkTo(*s_DragMark, cell, cell.X - s_DragMarkGrab.X, cell.Y - s_DragMarkGrab.Y);
		s_DragMarkGrab = cell;
	}

	PokeMinimap(*s_DragMark);
}

void FullMap::EndDragMark()
{
	if (!s_DragMark)
		return;

	// 位置已在本机改好，广播一次"删除+新增"让盟友同步到新位置
	const FullMap::Mark moved = *s_DragMark;
	const uint16_t oldId = moved.Id;

	PokeMinimap(moved);
	EventExt::RaiseFullMapMarkRemove(oldId);

	// 复用同一 Id，避免盟友端出现两个标记
	EventExt::RaiseFullMapMarkAdd(moved);

	s_DragMark = nullptr;
}

void FullMap::NotifyWheelDelta(short delta, bool ctrlHeld)
{
	if (!IsOpen || delta == 0)
		return;

	const bool ctrl = ctrlHeld;

	// 非 Ctrl：滚轮用于工具栏滚动（工具区溢出时）
	if (!ctrl)
	{
		if (s_ToolbarScrollMax > 0)
		{
			Point2D mouse { -1000, -1000 };
			if (auto* pMouse = WWMouseClass::Instance)
				mouse = pMouse->XY1;
			const auto& tb = g_Layout.Toolbar;
			if (mouse.X >= tb.X && mouse.X < tb.X + tb.Width
				&& mouse.Y >= tb.Y && mouse.Y < tb.Y + tb.Height)
			{
				s_ToolbarScroll = std::clamp(s_ToolbarScroll - delta * 0.25f,
					0.0f, static_cast<float>(s_ToolbarScrollMax));
			}
		}
		return;
	}

	// 消息泵内只累计诊断数据（同步文件 IO 会卡消息泵），日志统一在渲染侧输出
	++s_WheelDiagCount;
	s_WheelDiagDelta = static_cast<short>(s_WheelDiagDelta + delta);
	s_WheelDiagTick = timeGetTime();
	if (!s_WheelArriveTick)
		s_WheelArriveTick = s_WheelDiagTick;

	s_ZoomUseCenter = false;
	s_ZoomTarget *= static_cast<float>(std::exp(delta * (std::log(FullMapSettings::ZoomStep) / 120.0)));
	s_ZoomTarget = std::clamp(s_ZoomTarget, 1.0f, static_cast<float>(FullMapSettings::MaxZoom));
}

void FullMap::ApplyAdd(const Mark& mark)
{
	if (mark.Kind >= FullMapMarkKind::COUNT || mark.Author < 0)
		return;
	if (mark.PointCount > MaxPoints || mark.TextLen >= MaxTextChars)
		return;

	Mark m = mark;
	m.PlacedFrame = Unsorted::CurrentFrame;
	m.Text[MaxTextChars - 1] = 0;
	InsertToList(m);
	PokeMinimap(m);
}

void FullMap::ApplyAppend(int8_t author, uint16_t id, const int16_t* points, uint8_t count)
{
	auto* pMark = FindMark(author, id);
	if (!pMark)
		return;

	for (int i = 0; i < count && pMark->PointCount < MaxPoints; ++i)
	{
		pMark->Points[pMark->PointCount][0] = points[i * 2];
		pMark->Points[pMark->PointCount][1] = points[i * 2 + 1];
		++pMark->PointCount;
	}
}

void FullMap::ApplyRemove(int8_t author, uint16_t id)
{
	if (auto* pMark = FindMark(author, id))
	{
		PokeMinimap(*pMark);
		g_Marks.erase(g_Marks.begin() + (pMark - g_Marks.data()));
	}
}

void FullMap::ApplyClearOwn(int8_t author)
{
	std::vector<Mark> keep;
	keep.reserve(g_Marks.size());
	for (const auto& m : g_Marks)
	{
		if (m.Author != author)
			keep.push_back(m);
		else
			PokeMinimap(m);
	}
	g_Marks.swap(keep);
}

bool FullMap::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	Stm.Save(SaveVersion);
	Stm.Save(g_NextId);

	const int count = static_cast<int>(g_Marks.size());
	if (count > MaxMarks)
		return false;
	Stm.Save(count);
	for (const auto& m : g_Marks)
		Stm.Save(m); // 纯 POD，整条字节流

	return Stm.Success();
}

bool FullMap::LoadGlobals(ScaffoldStreamReader& Stm)
{
	IsOpen = false;
	ResetInteractionState();
	g_Marks.clear();

	uint32_t version = 0;
	if (!Stm.Load(version) || version != SaveVersion)
		return false;

	uint16_t nextId = 0;
	int count = 0;
	if (!Stm.Load(nextId) || !Stm.Load(count))
		return false;
	if (count < 0 || count > MaxMarks)
		return false;

	g_Marks.resize(count);
	for (auto& m : g_Marks)
	{
		if (!Stm.Load(m))
			return false;
		if (m.Kind >= FullMapMarkKind::COUNT || m.PointCount > MaxPoints || m.TextLen >= MaxTextChars)
			return false;
		m.Text[MaxTextChars - 1] = 0;
	}

	g_NextId = std::max<uint16_t>(nextId, 1);
	return Stm.Success();
}

bool FullMap::CompleteLoad()
{
	return true;
}

// 在原版侧栏小地图上画标记（RadarClass::Render 里位图→侧栏 blit 之后调用）。
// 直接画在侧栏表面而非雷达位图上：既出现在小地图，又不会被放大进战术地图。
// 点标记画色点；直线/箭头/测距画连线，矩形/椭圆画轮廓，手绘画折线——所见即所得。
// 颜色 = 作者所属方颜色，黑描边小方块与触发器标记 / 单位点区分。
void FullMap::DrawMinimapMarks()
{
	if (g_Marks.empty())
		return;

	auto* pSurf = DSurface::Sidebar;
	if (!pSurf)
		return;

	auto& radar = RadarClass::Instance;
	const auto& box = radar.unknown_rect_149C; // 侧栏上雷达框的位置与大小

	auto toSidebar = [&](CellStruct cell, Point2D& out)
	{
		const Point2D p = CellToRadarPx(cell);
		out = { box.X + p.X, box.Y + p.Y };
		return p.X >= 0 && p.X < box.Width && p.Y >= 0 && p.Y < box.Height;
	};

	auto dot = [&](Point2D p, int cInt, bool isBadge, int badgeColorInt)
	{
		if (isBadge)
		{
			// 徽章：含义色方块（黑边）+ 常驻扩散椭圆环（贴合雷达菱形比例）
			const int rr = 3 + BadgeRingRadius(Unsorted::CurrentFrame, 0) / 9;
			constexpr int Seg = 10;
			Point2D pts[Seg];
			for (int i = 0; i < Seg; ++i)
			{
				const double t = 2.0 * 3.14159265358979323846 * i / Seg;
				pts[i] = { p.X + static_cast<int>(rr * std::cos(t)),
					p.Y + static_cast<int>(rr * std::sin(t) / 2) };
			}
			for (int i = 0; i < Seg; ++i)
				pSurf->DrawLine(&pts[i], &pts[(i + 1) % Seg], badgeColorInt);

			RectangleStruct outer { p.X - 2, p.Y - 2, 4, 4 };
			pSurf->FillRect(&outer, 0);
			RectangleStruct inner { p.X - 1, p.Y - 1, 2, 2 };
			pSurf->FillRect(&inner, badgeColorInt);
		}
		else
		{
			RectangleStruct outer { p.X - 1, p.Y - 1, 3, 3 };
			pSurf->FillRect(&outer, 0);
			RectangleStruct inner { p.X, p.Y, 1, 1 };
			pSurf->FillRect(&inner, cInt);
		}
	};

	for (const auto& m : g_Marks)
	{
		if (!IsMarkVisible(m))
			continue;

		ColorStruct color { 255, 255, 255 };
		if (auto* pHouse = GetHouse(m.Author))
			color = pHouse->Color;
		const int colorInt = Drawing::RGB_To_Int(color);

		// 徽章在小地图上用含义色方块 + 扩散圆环，与普通标记的作者色点区分
		const bool badge = IsBadgeKind(m.Kind);
		const int badgeInt = badge ? Drawing::RGB_To_Int(BadgeMeaningColor(m.Kind)) : 0;

		Point2D a {}, b {};
		switch (m.Kind)
		{
		case FullMapMarkKind::Line:
		case FullMapMarkKind::Arrow:
		case FullMapMarkKind::Distance:
			if (toSidebar(m.Cell, a) && toSidebar(m.Cell2, b))
			{
				pSurf->DrawLine(&a, &b, colorInt);
				if (m.Kind == FullMapMarkKind::Arrow)
				{
					// 小地图箭头分叉（迷你）
					const double ang = std::atan2(static_cast<double>(b.Y - a.Y), static_cast<double>(b.X - a.X));
					for (const double off : { 0.5, -0.5 })
					{
						Point2D p {
							b.X - static_cast<int>(6 * std::cos(ang + off)),
							b.Y - static_cast<int>(6 * std::sin(ang + off)) };
						pSurf->DrawLine(&b, &p, colorInt);
					}
				}
			}
			dot(a, colorInt, badge, badgeInt);
			dot(b, colorInt, badge, badgeInt);
			break;
		case FullMapMarkKind::Rect:
		case FullMapMarkKind::Ellipse:
		{
			const bool okA = toSidebar(m.Cell, a);
			const bool okB = toSidebar(m.Cell2, b);
			if (okA && okB)
			{
				RectangleStruct rc {
					std::min(a.X, b.X), std::min(a.Y, b.Y),
					std::max(1, std::abs(a.X - b.X)), std::max(1, std::abs(a.Y - b.Y)) };
				if (m.Kind == FullMapMarkKind::Rect)
				{
					pSurf->DrawRect(&rc, static_cast<DWORD>(colorInt));
				}
				else
				{
					constexpr int Seg = 12;
					Point2D pts[Seg];
					const double cx = rc.X + rc.Width / 2.0;
					const double cy = rc.Y + rc.Height / 2.0;
					for (int i = 0; i < Seg; ++i)
					{
						const double t = 2.0 * 3.14159265358979323846 * i / Seg;
						pts[i] = {
							static_cast<int>(cx + rc.Width / 2.0 * std::cos(t)),
							static_cast<int>(cy + rc.Height / 2.0 * std::sin(t)) };
					}
					for (int i = 0; i < Seg; ++i)
						pSurf->DrawLine(&pts[i], &pts[(i + 1) % Seg], colorInt);
				}
			}
			dot(a, colorInt, badge, badgeInt);
			dot(b, colorInt, badge, badgeInt);
			break;
		}
		case FullMapMarkKind::Freehand:
		{
			Point2D prev {};
			bool hasPrev = false;
			for (int i = 0; i < m.PointCount; ++i)
			{
				Point2D p {};
				if (!toSidebar({ m.Points[i][0], m.Points[i][1] }, p))
				{
					hasPrev = false;
					continue;
				}
				if (hasPrev)
					pSurf->DrawLine(&prev, &p, colorInt);
				prev = p;
				hasPrev = true;
			}
			toSidebar(m.Cell, a);
			dot(a, colorInt, badge, badgeInt);
			break;
		}
		default:
			if (toSidebar(m.Cell, a))
				dot(a, colorInt, badge, badgeInt);
			break;
		}
	}
}

void FullMap::Clear()
{
	s_MinimapDoubleClick.Reset();
	FullMapOpenGL::Reset();
	FullMapPerf::Reset();
	FullMapTerrain::Invalidate();
	FullMapObjects::Invalidate();
	g_Layout = {};
	g_FullCanvas.clear();
	g_FullW = g_FullH = g_LastRadarW = g_LastRadarH = 0;
	IsOpen = false;
	s_Zoom = 1.0f;
	s_ZoomTarget = 1.0f;
	ResetInteractionState();
	g_Marks.clear();
	g_NextId = 1;
	s_LastClickTick = 0;
	s_LastClickPos = { -1000, -1000 };
}
