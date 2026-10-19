#include "RadarMark.h"
#include "RadarMarkAuto.h"
#include "RadarMarkTeamAuto.h"
#include "RadarMarkStream.h"
#include "RadarMarkNative.h"

#include <RadarClass.h>
#include <Surface.h>
#include <ConvertClass.h>
#include <FileSystem.h>
#include <TechnoClass.h>
#include <FootClass.h>
#include <TeamClass.h>
#include <AnimTypeClass.h>
#include <CellClass.h>
#include <MapClass.h>
#include <HouseClass.h>
#include <Fundamentals.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	// 原版逻辑帧率。Duration 用逻辑帧而不是真实时间，暂停/变速时表现才一致。
	constexpr int LogicFramesPerSecond = 15;

	// 脏矩形四周各放宽的像素数：CC_Draw_Shape 的实际落点可能比 GetFrameBounds 大一圈，
	// 少脏一列就会留下残边。
	constexpr int DirtyPadding = 1;

	// RadarClass::unknown_121C 是雷达显示位图（Surface*）；0x1220 是地形底图。
	Surface* GetRadarImage(RadarClass* pRadar)
	{
		return pRadar ? *reinterpret_cast<Surface**>(&pRadar->unknown_121C) : nullptr;
	}

	inline WORD ToSurface(ColorStruct const& color)
	{
		return static_cast<WORD>(color);  // ColorStruct -> RGB565
	}

	int PaletteIndexOf(ConvertClass* pPalette);

	struct Entry
	{
		enum class Anchor
		{
			Techno,     // 跟随单个对象
			TeamEach,   // 小队每个成员一个
			TeamBox,    // 小队包围盒
			TeamCenter, // 小队成员像素的中心，整队只有一个
			Cell,       // 固定格
			Coord,      // 固定世界坐标
			CellRect,   // 固定格范围
			Link,       // 两对象连线
			Pixel,      // 固定雷达像素
		};

		RadarMarkId   Id;
		int           Serial = 0;
		std::string   Tag;
		RadarArt      Art;
		int           Duration = RadarMark::Infinite;

		Anchor        Kind = Anchor::Techno;
		TechnoClass*  pTechno = nullptr;
		TechnoClass*  pLinkTo = nullptr;
		TeamClass*    pTeam = nullptr;
		CellStruct    CellA {};
		CellStruct    CellB {};
		CoordStruct   Coord {};
		Point2D       Pixel { 0, 0 };
		bool          Follow = true;

		int           PaletteIndex = -1; // ConvertClass 不参与 Swizzle，在 CompleteLoad 后解析

		// 运行时
		int           Frame = 0;      // 动画序号
		int           Timer = 0;
		int           LastUpdateFrame = -1; // 运行时基准；读档完成后按当前逻辑帧重建
		int           ShpFrame = 0;   // 实际 SHP 帧号
		bool          Expired = false;
		std::vector<Point2D>         Pixels;
		std::vector<TechnoClass*>    DrawTargets; // 原生动画绘制所需的逐成员上下文
		std::vector<RectangleStruct> LastRects;  // 上一帧画过的地方，用来擦除
		std::vector<RectangleStruct> NowRects;   // 本帧要画的地方

		template <typename T>
		bool Serialize(T& Stm, int& paletteIndex, bool registerForChange)
		{
			return Stm
				.Process(this->Kind)
				.Process(this->Id.Value)
				.Process(this->Serial)
				.Process(this->Art.Kind)
				.Process(this->Art.Size)
				.Process(this->Art.Filled)
				.Process(this->Art.Outline)
				.Process(this->Art.Color)
				.Process(this->Art.OutlineColor)
				.Process(this->Art.UseHouseColor)
				.Process(this->Art.Shape, registerForChange)
				.Process(this->Art.Anim, registerForChange)
				.Process(paletteIndex)
				.Process(this->Art.First)
				.Process(this->Art.Count)
				.Process(this->Art.Rate)
				.Process(this->Art.Remap)
				.Process(this->Art.Opaque)
				.Process(this->Art.Brightness)
				.Process(this->Art.AnchorCenter)
				.Process(this->Art.Offset)
				.Process(this->Art.Priority)
				.Process(this->Art.BlinkRate)
				.Process(this->Art.HideInShroud)
				.Process(this->Tag)
				.Process(this->Duration)
				.Process(this->Frame)
				.Process(this->Timer)
				.Process(this->Follow)
				.Process(this->pTechno, registerForChange)
				.Process(this->pLinkTo, registerForChange)
				.Process(this->pTeam, registerForChange)
				.Process(this->CellA)
				.Process(this->CellB)
				.Process(this->Coord)
				.Process(this->Pixel)
				.Success();
		}

		bool Load(ScaffoldStreamReader& Stm, bool registerForChange)
		{
			return this->Serialize(Stm, this->PaletteIndex, registerForChange)
				&& this->Kind >= Anchor::Techno && this->Kind <= Anchor::Pixel
				&& this->Art.Kind >= RadarArtKind::Rect && this->Art.Kind <= RadarArtKind::Anim
				&& this->Id.Value >= 0 && this->Serial >= 0
				&& this->Frame >= 0 && this->Timer >= 0;
		}

		bool Save(ScaffoldStreamWriter& Stm) const
		{
			int paletteIndex = PaletteIndexOf(this->Art.Palette);
			return const_cast<Entry*>(this)->Serialize(Stm, paletteIndex, true);
		}
	};

	std::vector<Entry> g_Entries;
	Surface* g_LastSurface = nullptr;
	int g_NextId = 0;
	int g_NextSerial = 0;

	// Swizzle 保存的是指针字段的地址；暂存区在 CompleteLoad 前不能搬移或释放。
	std::vector<Entry> g_LoadedEntries;
	bool g_LoadPending = false;

	bool IsInObjectArray(TechnoClass* pTarget)
	{
		auto& array = TechnoClass::Array;

		for (int i = 0; i < array.Count; ++i)
		{
			if (array[i] == pTarget)
				return true;
		}

		return false;
	}

	bool IsInTeamArray(TeamClass* pTarget)
	{
		auto& array = TeamClass::Array;

		for (int i = 0; i < array.Count; ++i)
		{
			if (array[i] == pTarget)
				return true;
		}

		return false;
	}

	// 与 0x655C50 内的迷雾判定一致：AltFlags 的 Mapped 位没置位就还在迷雾里
	bool IsInShroud(TechnoClass* pTechno)
	{
		CellClass* pCell = pTechno->GetCell();
		if (!pCell)
			return false;

		return (static_cast<DWORD>(pCell->AltFlags) & 0x8u) == 0;
	}

	bool IsInShroud(CellStruct cell)
	{
		CellClass* pCell = MapClass::Instance.TryGetCellAt(cell);
		if (!pCell)
			return false;

		return (static_cast<DWORD>(pCell->AltFlags) & 0x8u) == 0;
	}

	SHPStruct* ResolveImage(RadarArt const& art)
	{
		if (art.Kind == RadarArtKind::Shape)
			return art.Shape;

		if (art.Kind == RadarArtKind::Anim && art.Anim)
			return RadarMark::ResolveAnimImage(art.Anim);

		return nullptr;
	}

	int ResolveFirst(RadarArt const& art)
	{
		if (art.Kind == RadarArtKind::Anim && art.Anim)
			return art.Anim->Start;

		return art.First;
	}

	int ResolveFrameCount(RadarArt const& art)
	{
		if (art.Kind == RadarArtKind::Anim && art.Anim)
			return std::max(1, art.Anim->End - art.Anim->Start + 1);

		if (art.Kind == RadarArtKind::Shape)
			return std::max(1, art.Count);

		return 1;
	}

	int ResolveRate(RadarArt const& art)
	{
		if (art.Rate > 0)
			return art.Rate;

		if (art.Kind == RadarArtKind::Anim && art.Anim)
			return std::max(1, art.Anim->Rate);

		return 0;
	}

	int ResolveShpFrame(Entry const& entry)
	{
		return ResolveFirst(entry.Art) + (entry.Frame % ResolveFrameCount(entry.Art));
	}

	// 动画声明的帧范围（Start..End）可能比 SHP 实际帧数大（End 写错、或只有一帧的图），
	// 越界索引会让 GetFrameBounds / CC_Draw_Shape 读到野指针，这里按实际帧数兜回来。
	int ClampToImageFrames(SHPStruct* pImage, int frame)
	{
		int const frames = pImage ? static_cast<int>(pImage->Frames) : 0;

		if (frames <= 0)
			return 0;

		frame %= frames;

		return (frame < 0) ? frame + frames : frame;
	}

	RectangleStruct MakeArtRect(Point2D const& pixel, RadarArt const& art, int shpFrame)
	{
		int width = std::max(1, art.Size);
		int height = width;

		if (SHPStruct* pImage = ResolveImage(art))
		{
			if (pImage->Frames > 0)
			{
				RectangleStruct bounds = pImage->GetFrameBounds(ClampToImageFrames(pImage, shpFrame));

				if (bounds.Width > 0 && bounds.Height > 0)
				{
					width = bounds.Width;
					height = bounds.Height;
				}
			}
		}

		RectangleStruct rect;
		rect.X = (art.AnchorCenter ? pixel.X - width / 2 : pixel.X) + art.Offset.X;
		rect.Y = (art.AnchorCenter ? pixel.Y - height / 2 : pixel.Y) + art.Offset.Y;
		rect.Width = width;
		rect.Height = height;
		return rect;
	}

	// 把矩形逐像素塞进原版的脏点表；RefreshCrd 内部已做边界检查与同帧去重。
	// 先按雷达位图裁剪，避免超大范围标记（如全图 AddRect）空转。
	void MarkDirty(RadarClass* pRadar, RectangleStruct const& clip, RectangleStruct const& rect)
	{
		int const left = std::max(rect.X - DirtyPadding, clip.X);
		int const top = std::max(rect.Y - DirtyPadding, clip.Y);
		int const right = std::min(rect.X + rect.Width + DirtyPadding, clip.X + clip.Width);
		int const bottom = std::min(rect.Y + rect.Height + DirtyPadding, clip.Y + clip.Height);

		if (right <= left || bottom <= top)
			return;

		for (int y = top; y < bottom; ++y)
		{
			for (int x = left; x < right; ++x)
			{
				Point2D pt { x, y };
				pRadar->RefreshCrd(&pt);
			}
		}
	}

	void AdvanceTimers(Entry& entry, int currentFrame)
	{
		int const previousFrame = entry.LastUpdateFrame;
		entry.LastUpdateFrame = currentFrame;
		if (entry.Expired || previousFrame < 0 || currentFrame <= previousFrame)
			return;

		auto const elapsed = static_cast<std::int64_t>(currentFrame) - previousFrame;
		if (entry.Duration > 0)
		{
			if (elapsed >= entry.Duration)
			{
				entry.Duration = 0;
				entry.Expired = true;
				return;
			}
			entry.Duration -= static_cast<int>(elapsed);
		}

		int const rate = ResolveRate(entry.Art);
		int const count = ResolveFrameCount(entry.Art);
		if (rate > 0 && count > 1)
		{
			auto const ticks = static_cast<std::int64_t>(entry.Timer) + elapsed;
			entry.Frame = static_cast<int>((entry.Frame + ticks / rate) % count);
			entry.Timer = static_cast<int>(ticks % rate);
		}
	}

	void AdvanceAllTimers()
	{
		int const frame = Unsorted::CurrentFrame;
		for (auto& entry : g_Entries)
			AdvanceTimers(entry, frame);
	}

	// 解析本帧的锚点像素；返回 false 表示锚点失效（对象没了），标记应被清理。
	bool ResolvePixels(Entry& entry)
	{
		switch (entry.Kind)
		{
		case Entry::Anchor::Techno:
		{
			// 只有锚点真的没了才算失效
			if (!entry.pTechno || !IsInObjectArray(entry.pTechno))
				return false;

			// 进载具/机场、被黑幕罩住都只是暂时看不见，条目要留着，否则出来就再也回不来
			if (entry.pTechno->InLimbo)
				return true;

			if (entry.Art.HideInShroud && IsInShroud(entry.pTechno))
				return true;

			CoordStruct coords = entry.Follow ? entry.pTechno->GetCoords() : entry.Coord;
			entry.Pixels.push_back(RadarMark::ToPixel(coords));
			entry.DrawTargets.push_back(entry.pTechno);
			return true;
		}

		case Entry::Anchor::Link:
		{
			if (!entry.pTechno || !entry.pLinkTo)
				return false;

			if (!IsInObjectArray(entry.pTechno) || !IsInObjectArray(entry.pLinkTo))
				return false;

			entry.Pixels.push_back(RadarMark::ToPixel(entry.pTechno->GetCoords()));
			entry.Pixels.push_back(RadarMark::ToPixel(entry.pLinkTo->GetCoords()));
			return true;
		}

		case Entry::Anchor::TeamEach:
		case Entry::Anchor::TeamBox:
		{
			if (!entry.pTeam || !IsInTeamArray(entry.pTeam))
				return false;

			int guard = 0;

			for (FootClass* pFoot = entry.pTeam->FirstUnit; pFoot && guard < 512; pFoot = pFoot->NextTeamMember, ++guard)
			{
				if (!pFoot->IsAlive || pFoot->InLimbo)
					continue;

				if (entry.Art.HideInShroud && IsInShroud(pFoot))
					continue;

				entry.Pixels.push_back(RadarMark::ToPixel(pFoot->GetCoords()));
				entry.DrawTargets.push_back(pFoot);
			}

			// 成员暂时不可见时保留条目，小队失效由上面的数组检查处理。
			return true;
		}

		case Entry::Anchor::TeamCenter:
		{
			if (!entry.pTeam || !IsInTeamArray(entry.pTeam))
				return false;

			// 整队一个标记：取所有可见成员像素的平均值
			int sumX = 0;
			int sumY = 0;
			int count = 0;
			int guard = 0;

			for (FootClass* pFoot = entry.pTeam->FirstUnit; pFoot && guard < 512; pFoot = pFoot->NextTeamMember, ++guard)
			{
				if (!pFoot->IsAlive || pFoot->InLimbo)
					continue;

				if (entry.Art.HideInShroud && IsInShroud(pFoot))
					continue;

				if (count == 0)
					entry.DrawTargets.push_back(pFoot);
				Point2D const pixel = RadarMark::ToPixel(pFoot->GetCoords());
				sumX += pixel.X;
				sumY += pixel.Y;
				++count;
			}

			// 一个成员都看不见时只保留条目、不画（与 limbo/黑幕的处理一致）
			if (count > 0)
				entry.Pixels.push_back(Point2D { sumX / count, sumY / count });

			return true;
		}

		case Entry::Anchor::Cell:
			if (entry.Art.HideInShroud && IsInShroud(entry.CellA))
				return true;

			entry.Pixels.push_back(RadarMark::ToPixel(entry.CellA));
			return true;

		case Entry::Anchor::Coord:
			if (entry.Art.HideInShroud && IsInShroud(CellClass::Coord2Cell(entry.Coord)))
				return true;

			entry.Pixels.push_back(RadarMark::ToPixel(entry.Coord));
			return true;

		case Entry::Anchor::CellRect:
			entry.Pixels.push_back(RadarMark::ToPixel(entry.CellA));
			entry.Pixels.push_back(RadarMark::ToPixel(entry.CellB));
			return true;

		case Entry::Anchor::Pixel:
			entry.Pixels.push_back(entry.Pixel);
			return true;
		}

		return false;
	}

	void BuildRects(Entry& entry)
	{
		if (entry.Pixels.empty())
			return;

		bool const isBox = entry.Kind == Entry::Anchor::TeamBox
			|| entry.Kind == Entry::Anchor::CellRect
			|| entry.Kind == Entry::Anchor::Link;

		if (isBox)
		{
			int left = entry.Pixels.front().X;
			int top = entry.Pixels.front().Y;
			int right = left;
			int bottom = top;

			for (auto const& pixel : entry.Pixels)
			{
				left = std::min(left, pixel.X);
				top = std::min(top, pixel.Y);
				right = std::max(right, pixel.X);
				bottom = std::max(bottom, pixel.Y);
			}

			if (entry.Kind == Entry::Anchor::Link)
			{
				--left;
				--top;
				++right;
				++bottom;
			}

			RectangleStruct rect;
			rect.X = left + entry.Art.Offset.X;
			rect.Y = top + entry.Art.Offset.Y;
			rect.Width = right - left + 1;
			rect.Height = bottom - top + 1;
			entry.NowRects.push_back(rect);
			return;
		}

		for (auto const& pixel : entry.Pixels)
			entry.NowRects.push_back(MakeArtRect(pixel, entry.Art, entry.ShpFrame));
	}

	bool IsBlinkHidden(Entry const& entry)
	{
		if (entry.Art.BlinkRate <= 0)
			return false;

		return ((Unsorted::CurrentFrame / entry.Art.BlinkRate) & 1) != 0;
	}

	WORD ResolveColor(Entry const& entry)
	{
		if (entry.Art.UseHouseColor)
		{
			HouseClass* pOwner = entry.pTechno
				? entry.pTechno->Owner
				: (entry.pTeam ? entry.pTeam->Owner : nullptr);

			if (pOwner)
				return ToSurface(pOwner->Color);
		}

		return ToSurface(entry.Art.Color);
	}

	bool ClipRect(RectangleStruct& rect, RectangleStruct const& clip)
	{
		int const left = std::max(rect.X, clip.X);
		int const top = std::max(rect.Y, clip.Y);
		int const right = std::min(rect.X + rect.Width, clip.X + clip.Width);
		int const bottom = std::min(rect.Y + rect.Height, clip.Y + clip.Height);

		if (right <= left || bottom <= top)
			return false;

		rect.X = left;
		rect.Y = top;
		rect.Width = right - left;
		rect.Height = bottom - top;
		return true;
	}

	void DrawBox(Surface* pSurface, RectangleStruct const& clip, RectangleStruct rect, Entry const& entry, WORD color)
	{
		if (!ClipRect(rect, clip))
			return;

		if (entry.Art.Filled)
			pSurface->FillRect(&rect, color);

		if (entry.Art.Outline)
			pSurface->DrawRect(&rect, ToSurface(entry.Art.OutlineColor));
	}

	void DrawShape(Surface* pSurface, RectangleStruct const& clip, RectangleStruct const& rect, SHPStruct* pImage, Entry const& entry)
	{
		if (!pImage || pImage->Frames <= 0)
			return;

		ConvertClass* pPalette = entry.Art.Palette ? entry.Art.Palette : FileSystem::ANIM_PAL;

		if (!pPalette)
			return;

		Point2D position { rect.X, rect.Y };
		RectangleStruct bounds = clip;

		// 引擎的 RLE 位图通道把 `palette->CurrentZRemap` 当 256 字节查表用（0x4961E8 → `bl = table[pixel]`）。
		// 该字段只有引擎自己的 ZRemap 绘制流程才会赋有效值，直接调 CC_Draw_Shape 时它还是 -1，
		// 被当成指针解引用就是 0x38 崩溃。这里临时塞一张恒等表，画完还原。
		static byte s_IdentityTable[256];
		static bool s_IdentityTableReady = false;

		if (!s_IdentityTableReady)
		{
			for (int i = 0; i < 256; ++i)
				s_IdentityTable[i] = static_cast<byte>(i);

			s_IdentityTableReady = true;
		}

		DWORD const oldZRemap = pPalette->CurrentZRemap;
		pPalette->CurrentZRemap = reinterpret_cast<DWORD>(s_IdentityTable);

		CC_Draw_Shape(pSurface, pPalette, pImage, ClampToImageFrames(pImage, entry.ShpFrame), &position, &bounds,
			entry.Art.Opaque ? BlitterFlags::Plain : BlitterFlags::None,
			entry.Art.Remap < 0 ? 0 : entry.Art.Remap, 0, ZGradient::Ground, entry.Art.Brightness, 0, nullptr, 0, 0, 0);

		pPalette->CurrentZRemap = oldZRemap;
	}

	void DrawEntry(Surface* pSurface, RectangleStruct const& clip, Entry& entry)
	{
		WORD const color = ResolveColor(entry);

		if (entry.Kind == Entry::Anchor::Link)
		{
			if (entry.Pixels.size() < 2)
				return;

			Point2D from = entry.Pixels[0];
			Point2D to = entry.Pixels[1];
			int const thickness = std::max(1, entry.Art.Size);

			for (int i = 0; i < thickness; ++i)
			{
				Point2D a { from.X, from.Y + i };
				Point2D b { to.X, to.Y + i };
				pSurface->DrawLine(&a, &b, color);
			}

			return;
		}

		bool const isBox = entry.Kind == Entry::Anchor::TeamBox || entry.Kind == Entry::Anchor::CellRect;

		if (!isBox && entry.Art.Kind == RadarArtKind::Anim && entry.Art.Anim)
		{
			std::vector<RectangleStruct> painted;
			for (size_t index = 0; index < entry.Pixels.size(); ++index)
			{
				auto target = index < entry.DrawTargets.size() ? entry.DrawTargets[index] : nullptr;
				auto owner = target ? target->Owner : (entry.pTeam ? entry.pTeam->Owner : nullptr);
				CoordStruct world = target ? target->GetCoords() : entry.Coord;
				if (entry.Kind == Entry::Anchor::Cell)
					world = CellClass::Cell2Coord(entry.CellA);
				else if (entry.Kind == Entry::Anchor::Pixel)
					world = CellClass::Cell2Coord(RadarMark::ToCell(entry.Pixel));
				Point2D position = entry.Pixels[index];
				if (!entry.Art.AnchorCenter)
				{
					if (auto image = RadarMark::ResolveAnimImage(entry.Art.Anim))
						position += Point2D { image->Width / 2, image->Height / 2 };
				}
				RadarMarkNative::Draw(pSurface, clip, position, world, entry.Art.Anim,
					entry.ShpFrame, target, owner, entry.Art, painted);
			}
			// Record the actual provider-adjusted sprite/shadow rectangles for next-frame erasure.
			entry.NowRects = std::move(painted);
			return;
		}

		for (auto const& rawRect : entry.NowRects)
		{
			if (entry.Art.Kind == RadarArtKind::Rect || isBox)
			{
				DrawBox(pSurface, clip, rawRect, entry, color);
				continue;
			}

			if (SHPStruct* pImage = ResolveImage(entry.Art))
				DrawShape(pSurface, clip, rawRect, pImage, entry);
			else
				DrawBox(pSurface, clip, rawRect, entry, color);  // 图像缺失时退化成方块
		}
	}

	void RemoveExpired()
	{
		g_Entries.erase(
			std::remove_if(g_Entries.begin(), g_Entries.end(), [](Entry const& e) { return e.Expired; }),
			g_Entries.end());
	}

	RadarMarkId Insert(Entry&& entry, const char* tag)
	{
		entry.Tag = tag ? tag : "";
		entry.Duration = (entry.Duration <= 0) ? RadarMark::Infinite : entry.Duration;
		entry.LastUpdateFrame = Unsorted::CurrentFrame;

		if (!entry.Tag.empty())
		{
			for (auto& existing : g_Entries)
			{
				if (existing.Expired || existing.Tag != entry.Tag)
					continue;

				// 同 tag 原地更新，避免地图脚本反复调用时叠出一堆标记
				existing.Art = entry.Art;
				existing.Duration = entry.Duration;
				existing.Kind = entry.Kind;
				existing.pTechno = entry.pTechno;
				existing.pLinkTo = entry.pLinkTo;
				existing.pTeam = entry.pTeam;
				existing.CellA = entry.CellA;
				existing.CellB = entry.CellB;
				existing.Coord = entry.Coord;
				existing.Pixel = entry.Pixel;
				existing.Follow = entry.Follow;
				existing.Frame = 0;
				existing.Timer = 0;
				existing.LastUpdateFrame = entry.LastUpdateFrame;
				return existing.Id;
			}
		}

		entry.Id.Value = g_NextId++;
		entry.Serial = g_NextSerial++;
		g_Entries.push_back(std::move(entry));

		return g_Entries.back().Id;
	}

	// 字段布局改变后递增，避免把旧格式解释为对象指针。
	constexpr int SaveVersion = 3;

	int PaletteIndexOf(ConvertClass* pPalette)
	{
		auto& array = ConvertClass::Array;
		for (int i = 0; pPalette && i < array.Count; ++i)
		{
			if (array[i] == pPalette)
				return i;
		}
		return -1;
	}

	ConvertClass* PaletteAt(int index)
	{
		auto& array = ConvertClass::Array;
		return index >= 0 && index < array.Count ? array[index] : nullptr;
	}

}

void RadarMarkInternal::OnRenderBegin()
{
	if (g_LoadPending || g_Entries.empty())
		return;

	AdvanceAllTimers();

	auto* pRadar = &RadarClass::Instance;
	Surface* pSurface = GetRadarImage(pRadar);

	if (!pSurface)
	{
		g_LastSurface = nullptr;
		return;
	}

	// 雷达位图重建（关/开雷达、换地图）→ 旧像素坐标作废，但锚点本身仍然有效。
	// 保留条目、只丢上帧矩形，否则常驻标记（如类型驱动）会在雷达重建后再也回不来。
	if (pSurface != g_LastSurface)
	{
		for (auto& entry : g_Entries)
			entry.LastRects.clear();

		g_LastSurface = pSurface;
	}

	RectangleStruct clip;
	pSurface->GetRect(&clip);

	bool bLostMark = false;

	for (auto& entry : g_Entries)
	{
		entry.Pixels.clear();
		entry.DrawTargets.clear();
		entry.NowRects.clear();

		if (!entry.Expired)
		{
			if (ResolvePixels(entry))
			{
				entry.ShpFrame = ResolveShpFrame(entry);
				BuildRects(entry);
			}
			else
			{
				entry.Expired = true;
			}
		}

		if (entry.Expired)
			bLostMark = true;

		// 旧位置与新位置都要重绘：旧位置擦除，新位置先露出底图再画标记
		for (auto const& rect : entry.LastRects)
			MarkDirty(pRadar, clip, rect);

		for (auto const& rect : entry.NowRects)
			MarkDirty(pRadar, clip, rect);
	}

	// 有标记消失就整张雷达图交给原版重画一次。
	// 只靠逐像素脏点擦最后一帧不可靠：脏点在某些帧会被引擎漏消费，
	// 播完一次的标记就会在小地图上留一片点位残影。
	if (bLostMark)
		MarkDirty(pRadar, clip, clip);

	RemoveExpired();
}

void RadarMarkInternal::OnRenderPaint()
{
	if (g_Entries.empty())
		return;

	auto* pRadar = &RadarClass::Instance;
	Surface* pSurface = GetRadarImage(pRadar);

	if (!pSurface)
		return;

	RectangleStruct clip;
	pSurface->GetRect(&clip);

	RadarMarkNative::FrameScope nativeFrame;
	std::vector<Entry*> order;
	order.reserve(g_Entries.size());

	for (auto& entry : g_Entries)
	{
		if (!entry.Expired && !entry.NowRects.empty())
			order.push_back(&entry);
	}

	std::sort(order.begin(), order.end(), [](Entry const* a, Entry const* b)
	{
		if (a->Art.Priority != b->Art.Priority)
			return a->Art.Priority < b->Art.Priority;

		return a->Serial < b->Serial;
	});

	for (auto* pEntry : order)
	{
		if (!IsBlinkHidden(*pEntry))
			DrawEntry(pSurface, clip, *pEntry);
	}

	for (auto& entry : g_Entries)
	{
		if (!entry.Expired)
			entry.LastRects = entry.NowRects;
	}
}

RadarMarkId RadarMark::Add(TechnoClass* pTechno, RadarArt const& art, int duration, const char* tag, bool follow)
{
	if (!pTechno)
		return { };

	Entry entry;
	entry.Kind = Entry::Anchor::Techno;
	entry.pTechno = pTechno;
	entry.Coord = pTechno->GetCoords();
	entry.Follow = follow;
	entry.Art = art;
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

RadarMarkId RadarMark::Add(TeamClass* pTeam, RadarArt const& art, int duration, const char* tag, RadarTeamMode mode)
{
	if (!pTeam)
		return { };

	Entry entry;

	switch (mode)
	{
	case RadarTeamMode::BoundingBox:
		entry.Kind = Entry::Anchor::TeamBox;
		break;
	case RadarTeamMode::Center:
		entry.Kind = Entry::Anchor::TeamCenter;
		break;
	default:
		entry.Kind = Entry::Anchor::TeamEach;
		break;
	}

	entry.pTeam = pTeam;
	entry.Art = art;
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

RadarMarkId RadarMark::Add(CellStruct cell, RadarArt const& art, int duration, const char* tag)
{
	Entry entry;
	entry.Kind = Entry::Anchor::Cell;
	entry.CellA = cell;
	entry.Art = art;
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

RadarMarkId RadarMark::Add(CoordStruct crd, RadarArt const& art, int duration, const char* tag)
{
	Entry entry;
	entry.Kind = Entry::Anchor::Coord;
	entry.Coord = crd;
	entry.Art = art;
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

RadarMarkId RadarMark::AddRect(CellStruct cellMin, CellStruct cellMax, RadarArt const& art, int duration, const char* tag)
{
	Entry entry;
	entry.Kind = Entry::Anchor::CellRect;
	entry.CellA = cellMin;
	entry.CellB = cellMax;
	entry.Art = art;
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

RadarMarkId RadarMark::AddPixel(Point2D pixel, RadarArt const& art, int duration, const char* tag)
{
	Entry entry;
	entry.Kind = Entry::Anchor::Pixel;
	entry.Pixel = pixel;
	entry.Art = art;
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

RadarMarkId RadarMark::AddLink(TechnoClass* pFrom, TechnoClass* pTo, ColorStruct color, int duration, const char* tag, int thickness)
{
	if (!pFrom || !pTo)
		return { };

	Entry entry;
	entry.Kind = Entry::Anchor::Link;
	entry.pTechno = pFrom;
	entry.pLinkTo = pTo;
	entry.Art.Color = color;
	entry.Art.UseHouseColor = false;
	entry.Art.Size = std::max(1, thickness);
	entry.Duration = duration;
	return Insert(std::move(entry), tag);
}

void RadarMark::Remove(RadarMarkId id)
{
	if (!id)
		return;

	for (auto& entry : g_Entries)
	{
		if (entry.Id.Value == id.Value)
		{
			// 实际擦除放到下一帧：先标脏再移除，避免残留像素
			entry.Expired = true;
			break;
		}
	}
}

void RadarMark::UpdateArt(RadarMarkId id, RadarArt const& art, int duration)
{
	if (!id)
		return;

	for (auto& entry : g_Entries)
	{
		if (entry.Expired || entry.Id.Value != id.Value)
			continue;

		AdvanceTimers(entry, Unsorted::CurrentFrame);
		if (entry.Expired)
			break;

		if (entry.Art.Anim != art.Anim)
		{
			entry.Frame = 0;
			entry.Timer = 0;
		}

		entry.Art = art;
		entry.Duration = (duration <= 0) ? Infinite : duration;
		entry.LastUpdateFrame = Unsorted::CurrentFrame;
		entry.ShpFrame = ResolveShpFrame(entry);
		break;
	}
}

void RadarMark::RemoveByTag(const char* tag)
{
	if (!tag)
		return;

	for (auto& entry : g_Entries)
	{
		if (entry.Tag == tag)
			entry.Expired = true;
	}
}

void RadarMark::RemoveAll(TechnoClass* pTechno)
{
	if (!pTechno)
		return;

	for (auto& entry : g_Entries)
	{
		if (entry.pTechno == pTechno || entry.pLinkTo == pTechno)
			entry.Expired = true;
	}
}

void RadarMark::ClearAll()
{
	for (auto& entry : g_Entries)
		entry.Expired = true;
}

SHPStruct* RadarMark::ResolveAnimImage(AnimTypeClass* pAnim)
{
	if (!pAnim)
		return nullptr;

	// 动画的图是按需加载的：从没在世界上播过的 AnimType（比如只拿来当小地图图标）
	// GetImage() 一路返回 null，必须主动 LoadImage 才会有 SHP。
	//
	// LoadImage 的返回值不能直接用：引擎那条路径在加载失败时会返回 -1（当成指针就是 0xFFFFFFFF，
	// 解引用就崩）。这里只把它当"触发加载"，然后重新读字段，并做一次指针范围校验。
	auto IsLoaded = [](SHPStruct* pImage)
	{
		auto const raw = reinterpret_cast<uintptr_t>(pImage);
		return raw > 0x10000u && raw < 0xFFF00000u;
	};

	if (SHPStruct* pImage = pAnim->GetImage(); IsLoaded(pImage))
		return pImage;

	pAnim->LoadImage();

	SHPStruct* pImage = pAnim->GetImage();
	return IsLoaded(pImage) ? pImage : nullptr;
}

int RadarMark::AnimDuration(AnimTypeClass* pAnim)
{
	if (!pAnim)
		return Infinite;

	// LoopCount <= 0 一律当常驻：art 里写 -1 的动画，引擎对象上读出来可能是 0
	// （要么没解析进字段，要么引擎内部把"无限"归一化成 0），真正"播一次"才会是 1。
	int const loops = pAnim->LoopCount;

	if (loops <= 0)
		return Infinite;

	int const frames = std::max(1, pAnim->End - pAnim->Start + 1);
	int const rate = std::max(1, pAnim->Rate);

	return frames * rate * loops;
}

bool RadarMark::Exists(RadarMarkId id)
{
	if (!id)
		return false;

	for (auto const& entry : g_Entries)
	{
		if (entry.Id.Value == id.Value && !entry.Expired)
			return true;
	}

	return false;
}

int RadarMark::Count()
{
	int count = 0;

	for (auto const& entry : g_Entries)
	{
		if (!entry.Expired)
			++count;
	}

	return count;
}

int RadarMark::Seconds(float seconds)
{
	int const frames = static_cast<int>(seconds * LogicFramesPerSecond + 0.5f);
	return frames > 0 ? frames : 1;
}

Point2D RadarMark::ToPixel(CoordStruct crd)
{
	Point2D ret { 0, 0 };
	RadarClass::Instance.GetCrdOnRadar(&ret, &crd, true);
	return ret;
}

Point2D RadarMark::ToPixel(CellStruct cell)
{
	CoordStruct crd { cell.X * 256 + 128, cell.Y * 256 + 128, 0 };
	return ToPixel(crd);
}

// 正变换（0x6557F0）：
//   px = ((X - Y) + offA) * factor      offA = Radar+0x1490
//   py = ((X + Y) - offB) * factor      offB = Radar+0x1498
// 注意 X/Y 单位是格（leptons/256），不要再乘 256。
CellStruct RadarMark::ToCell(Point2D pixel)
{
	auto* pRadar = &RadarClass::Instance;
	float const factor = pRadar->RadarSizeFactor;

	if (factor <= 0.0f)
		return CellStruct { 0, 0 };

	double const offsetA = static_cast<int>(pRadar->unknown_1490);
	double const offsetB = static_cast<int>(pRadar->unknown_1498);

	double const sum = pixel.Y / factor + offsetB;   // X + Y
	double const dif = pixel.X / factor - offsetA;   // X - Y

	return CellStruct {
		static_cast<short>(std::lround((sum + dif) / 2.0)),
		static_cast<short>(std::lround((sum - dif) / 2.0))
	};
}

bool RadarMark::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	if (g_LoadPending)
		return false;

	// 存档前补齐未绘制期间的逻辑帧，避免读档后凭空延长标记寿命。
	AdvanceAllTimers();
	Stm.Save(SaveVersion);
	Stm.Process(g_NextId).Process(g_NextSerial);

	int count = static_cast<int>(std::count_if(g_Entries.begin(), g_Entries.end(),
		[](Entry const& entry) { return !entry.Expired; }));
	if (count > RadarMarkStream::MaxRecords)
		return false;
	Stm.Save(count);
	for (auto& entry : g_Entries)
	{
		if (!entry.Expired)
			Stm.Process(entry);
	}

	return Stm.Success() && RadarMarkAuto::SaveGlobals(Stm) && RadarMarkTeamAuto::SaveGlobals(Stm);
}

bool RadarMark::LoadGlobals(ScaffoldStreamReader& Stm)
{
	g_LoadPending = false;
	RadarMarkAuto::Reset();
	RadarMarkTeamAuto::Reset();
	Clear();

	int version = 0;
	if (!Stm.Load(version) || version != SaveVersion)
		return false;

	Stm.Process(g_NextId).Process(g_NextSerial);
	if (!Stm.Success() || g_NextId < 0 || g_NextSerial < 0
		|| !RadarMarkStream::LoadRecords(Stm, g_LoadedEntries))
		return false;

	for (auto const& entry : g_LoadedEntries)
	{
		if (entry.Id.Value >= g_NextId || entry.Serial >= g_NextSerial)
			return false;
	}

	if (!RadarMarkAuto::LoadGlobals(Stm) || !RadarMarkTeamAuto::LoadGlobals(Stm))
		return false;

	g_LoadPending = true;
	return Stm.Success();
}

void RadarMark::Clear()
{
	g_Entries.clear();
	g_LastSurface = nullptr;

	// 若读档未完成就收到清场，保留 Swizzle 正在引用的暂存区。
	if (!g_LoadPending)
	{
		g_LoadedEntries.clear();
		g_NextId = 0;
		g_NextSerial = 0;
	}

	RadarMarkAuto::Clear();
	RadarMarkTeamAuto::Clear();
}

void RadarMark::PointerGotInvalid(void* ptr, bool removed)
{
	if (!ptr || !removed)
		return;

	for (auto& entry : g_Entries)
	{
		if (entry.pTechno == ptr || entry.pLinkTo == ptr || entry.pTeam == ptr || entry.Art.Anim == ptr)
		{
			entry.pTechno = nullptr;
			entry.pLinkTo = nullptr;
			entry.pTeam = nullptr;
			entry.Art.Anim = nullptr;
			entry.Expired = true;
		}
	}
	RadarMarkAuto::PointerGotInvalid(ptr);
	RadarMarkTeamAuto::PointerGotInvalid(ptr);
}

bool RadarMark::CompleteLoad()
{
	if (!g_LoadPending)
		return true;

	// 游戏已完成 Here_I_Am/Swizzle，现在可以搬移条目并重建以对象指针为键的记录。
	for (auto& entry : g_LoadedEntries)
	{
		entry.Art.Palette = PaletteAt(entry.PaletteIndex);
		entry.LastUpdateFrame = Unsorted::CurrentFrame;
	}
	g_Entries = std::move(g_LoadedEntries);
	g_LoadPending = false;
	g_LastSurface = nullptr;

	return RadarMarkAuto::CompleteLoad() && RadarMarkTeamAuto::CompleteLoad();
}
