#pragma once

/*
	小地图命令式标记。

	纯显示层：只改雷达位图的像素，不碰对象的雷达状态，不参与联机同步。
	渲染沿用原版的"脏格增量重绘"：每帧先把标记占用的像素标脏（原版会重绘成地形/单位点），
	再把标记画上去，因此在同一帧的 blit 之前就完成了擦除 → 不会闪烁或留残影。
*/

#include <GeneralStructures.h>

class TechnoClass;
class TeamClass;
class AnimTypeClass;
class ConvertClass;
class HouseClass;
class Surface;
class ScaffoldStreamWriter;
class ScaffoldStreamReader;
struct SHPStruct;

enum class RadarArtKind : int
{
	Rect = 0,   // 纯色方块/矩形框
	Shape = 1,  // SHP
	Anim = 2,   // 临时原生 AnimClass 绘制，不参与世界动画更新
};

// 怎么画。值类型，可随意拷贝；Shape/Anim 的资源由各自的类型持有，这里不负责释放。
class RadarArt
{
public:
	RadarArtKind  Kind = RadarArtKind::Rect;

	// ---- 方块 ----
	int           Size = 5;                        // 边长（像素）
	bool          Filled = true;
	bool          Outline = true;                  // 描边：浅色地表上也看得见
	ColorStruct   Color { 255, 255, 255 };
	ColorStruct   OutlineColor { 0, 0, 0 };
	bool          UseHouseColor = true;            // true 时取 Owner->Color，忽略 Color

	// ---- Shape / Anim ----
	SHPStruct*    Shape = nullptr;
	AnimTypeClass* Anim = nullptr;
	ConvertClass* Palette = nullptr;               // 为空时用 ANIM.PAL
	int           First = 0;                       // Shape 起始帧
	int           Count = 1;                       // Shape 帧数
	int           Rate = 0;                        // 每多少逻辑帧换一帧；0 时 Anim 取动画自身的 Rate
	int           Remap = -1;                      // SHP remap 色号，-1 = 不 remap
	bool          Opaque = false;                  // true = 不做透明
	int           Brightness = 1000;

	// ---- 通用 ----
	bool          AnchorCenter = true;             // true = 以像素为中心对齐，false = 左上角对齐
	Point2D       Offset { 0, 0 };                 // 相对锚点的像素偏移
	int           Priority = 0;                    // 同像素多个标记时，大者在上
	int           BlinkRate = 0;                   // > 0 时按奇偶逻辑帧隐藏
	bool          HideInShroud = false;            // true = 锚点所在格在迷雾里就不显示

	RadarArt() = default;

	explicit RadarArt(ColorStruct color, int size = 5)
		: Size(size), Color(color), UseHouseColor(false)
	{ }

	explicit RadarArt(SHPStruct* pShape, int first = 0, int count = 1, int rate = 0, ConvertClass* pPalette = nullptr)
		: Kind(RadarArtKind::Shape), Shape(pShape), Palette(pPalette), First(first), Count(count), Rate(rate)
	{ }

	explicit RadarArt(AnimTypeClass* pAnim)
		: Kind(RadarArtKind::Anim), Anim(pAnim)
	{ }

	// 范围标记的默认样式：只画边框，不填色
	static RadarArt Range()
	{
		RadarArt art;
		art.Filled = false;
		return art;
	}
};

enum class RadarTeamMode : int
{
	EachMember = 0,   // 每个成员一个标记
	BoundingBox = 1,  // 一个框住全体成员的矩形
	Center = 2,       // 一个标记，画在全体成员像素的中心
};

struct RadarMarkId
{
	int Value = -1;

	constexpr explicit operator bool() const noexcept { return this->Value >= 0; }
};

class RadarMark
{
public:
	static constexpr int Infinite = -1;  // 逻辑帧；<= 0 一律按无限处理

	// pFollow = false 时钉在创建时的像素上，不再跟随
	static RadarMarkId Add(TechnoClass* pTechno, RadarArt const& art = RadarArt(),
		int duration = Infinite, const char* tag = nullptr, bool follow = true);

	static RadarMarkId Add(TeamClass* pTeam, RadarArt const& art = RadarArt(),
		int duration = Infinite, const char* tag = nullptr,
		RadarTeamMode mode = RadarTeamMode::EachMember);

	static RadarMarkId Add(CellStruct cell, RadarArt const& art = RadarArt(),
		int duration = Infinite, const char* tag = nullptr);

	static RadarMarkId Add(CoordStruct crd, RadarArt const& art = RadarArt(),
		int duration = Infinite, const char* tag = nullptr);

	// 格范围（含两端）。默认只画边框
	static RadarMarkId AddRect(CellStruct cellMin, CellStruct cellMax, RadarArt const& art = RadarArt::Range(),
		int duration = Infinite, const char* tag = nullptr);

	// 直接给雷达位图坐标（像素），跳过格换算
	static RadarMarkId AddPixel(Point2D pixel, RadarArt const& art = RadarArt(),
		int duration = Infinite, const char* tag = nullptr);

	// 两点连线；art.Size 兼作线宽
	static RadarMarkId AddLink(TechnoClass* pFrom, TechnoClass* pTo, ColorStruct color,
		int duration = Infinite, const char* tag = nullptr, int thickness = 1);

	static void Remove(RadarMarkId id);
	static void RemoveByTag(const char* tag);
	static void RemoveAll(TechnoClass* pTechno);  // 锚在该对象上的所有标记
	static void ClearAll();

	static void UpdateArt(RadarMarkId id, RadarArt const& art, int duration = Infinite);

	static bool Exists(RadarMarkId id);
	static int  Count();

	static int  Seconds(float seconds);           // 秒 → 逻辑帧（按 15 帧/秒）

	static int  AnimDuration(AnimTypeClass* pAnim);

	static SHPStruct* ResolveAnimImage(AnimTypeClass* pAnim);

	static Point2D ToPixel(CoordStruct crd);
	static Point2D ToPixel(CellStruct cell);

	static CellStruct ToCell(Point2D pixel);

	// ---- 存读档 ----
	// 使用项目通用序列化与 Swizzle，CompleteLoad 后恢复对象引用和剩余时长。
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
	static bool LoadGlobals(ScaffoldStreamReader& Stm);
	static bool CompleteLoad();

	static void Clear();

	static void PointerGotInvalid(void* ptr, bool removed);
};

// 仅供 Hooks.cpp 使用，业务代码不要调用。
namespace RadarMarkInternal
{
	void OnRenderBegin();
	void OnRenderPaint();

}
