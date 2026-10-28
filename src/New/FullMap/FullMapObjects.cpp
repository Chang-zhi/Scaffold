#include "FullMapObjects.h"
#include "FullMapTerrain.h"
#include "FullMapGeometry.h"
#include "FullMapIcons.h"
#include "FullMapSettings.h"
#include "FullMapPerf.h"

#include <YRPP.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <HouseClass.h>
#include <CellClass.h>
#include <MapClass.h>
#include <TiberiumClass.h>
#include <TerrainClass.h>
#include <TerrainTypeClass.h>
#include <RulesClass.h>
#include <OverlayTypeClass.h>
#include <TacticalClass.h>
#include <Surface.h>
#include <Drawing.h>
#include <Unsorted.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <queue>
#include <string>
#include <vector>

namespace
{
	using Kind = FullMapIcons::Kind;
	using Visibility = FullMapIcons::Visibility;
	using NativePoint = FullMapGeometry::Point;
	struct FoundationCell { int X, Y, Level; };
	struct Item
	{
		Kind Icon;
		NativePoint Position;
		ColorStruct Color;
		std::wstring Name, Owner;
		std::vector<FoundationCell> Foundation;
		int Health, Maximum;
		bool Selected, ShowHealth;
	};
	std::vector<Item> g_Items;
	int g_Frame = -1;
	const HouseClass* g_Player = nullptr;

	uint16_t Pixel(int r, int g, int b)
	{
		return static_cast<uint16_t>(Drawing::RGB_To_Int(r, g, b));
	}
	uint16_t Mix(uint16_t a, uint16_t b, int alpha)
	{
		const auto c = Drawing::Int_To_RGB(a), d = Drawing::Int_To_RGB(b);
		return Pixel((c.R * (255 - alpha) + d.R * alpha) / 255,
			(c.G * (255 - alpha) + d.G * alpha) / 255, (c.B * (255 - alpha) + d.B * alpha) / 255);
	}
	std::wstring NameOf(const AbstractTypeClass* type)
	{
		if (!type) return {};
		if (type->UIName && *type->UIName) return std::wstring(type->UIName, wcsnlen_s(type->UIName, 96));
		const bool hasName = *type->Name != 0;
		const char* text = hasName ? type->Name : type->ID;
		const int length = static_cast<int>(strnlen_s(text, hasName ? sizeof(type->Name) : sizeof(type->ID)));
		std::wstring result(length, L' ');
		const int count = MultiByteToWideChar(CP_ACP, 0, text, length, result.data(), length);
		result.resize(std::max(0, count));
		return result;
	}
	std::wstring LocalizeOreName(std::wstring name)
	{
		std::wstring lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c)
			{ return static_cast<wchar_t>(std::towlower(c)); });
		if (lower.find(L"tiberium tree") != std::wstring::npos
			|| lower.find(L"矿柱") != std::wstring::npos)
			return StringTable::TryFetchString("SF:FULLMAP_ORE_PILLAR", L"矿柱");
		if (lower.find(L"cruentus") != std::wstring::npos
			|| lower.find(L"gem") != std::wstring::npos
			|| lower.find(L"宝石") != std::wstring::npos)
			return StringTable::TryFetchString("SF:FULLMAP_ORE_GEMS", L"宝石矿");
		if (lower.find(L"gold") != std::wstring::npos
			|| lower.find(L"golden") != std::wstring::npos
			|| lower.find(L"黄金") != std::wstring::npos)
			return StringTable::TryFetchString("SF:FULLMAP_ORE_GOLD", L"黄金矿");
		return name;
	}

	std::wstring ResourceName(const TiberiumClass* resource)
	{
		if (!resource) return StringTable::TryFetchString("SF:FULLMAP_ORE_GENERIC", L"矿产");
		return LocalizeOreName(NameOf(resource));
	}

	const wchar_t* KindLabel(Kind kind)
	{
		switch (kind)
		{
		case Kind::Building: return StringTable::TryFetchString("SF:FULLMAP_KIND_BUILDING", L"建筑");
		case Kind::Infantry: return StringTable::TryFetchString("SF:FULLMAP_KIND_INFANTRY", L"步兵");
		case Kind::Vehicle: return StringTable::TryFetchString("SF:FULLMAP_KIND_VEHICLE", L"载具");
		case Kind::Ship: return StringTable::TryFetchString("SF:FULLMAP_KIND_SHIP", L"舰船");
		case Kind::Aircraft: return StringTable::TryFetchString("SF:FULLMAP_KIND_AIRCRAFT", L"飞行器");
		case Kind::OreSpawner: return StringTable::TryFetchString("SF:FULLMAP_ORE_PILLAR", L"矿柱");
		case Kind::TechBuilding: return StringTable::TryFetchString("SF:FULLMAP_KIND_TECH", L"科技建筑");
		default: return StringTable::TryFetchString("SF:FULLMAP_KIND_SIGNAL", L"探测信号");
		}
	}
	void Snapshot()
	{
		if (g_Frame == Unsorted::CurrentFrame && g_Player == HouseClass::CurrentPlayer) return;
		FullMapPerf::Scope timing(FullMapPerf::Stage::Snapshot);
		g_Frame = Unsorted::CurrentFrame;
		g_Player = HouseClass::CurrentPlayer;
		g_Items.clear();
		for (const auto* techno : TechnoClass::Array)
		{
			if (!techno || !techno->Owner || !techno->IsOnMap || techno->InLimbo || !techno->IsAlive || techno->Health <= 0)
				continue;
			const auto* type = techno->GetTechnoType();
			if (!type) continue;
			const auto cell = CellClass::Coord2Cell(techno->Location);
			if (!MapClass::Instance.CoordinatesLegal(cell)) continue;
			const auto* rules = RulesClass::Instance;
			const bool techBuilding = techno->WhatAmI() == AbstractType::Building && rules
				&& rules->NeutralTechBuildings.FindItemIndex(static_cast<const BuildingClass*>(techno)->Type) >= 0;
			if (techBuilding && !FullMapSettings::ShowTechBuildings) continue;
			const bool own = techno->Owner == g_Player;
			int detection = 0;
			// Use the engine's radar/sensor policy; IsVisible only means the
			// object was in the small tactical viewport and cannot be used here.
			const bool nativeRadar = own || techno->IsRadarVisible(&detection);
			// Visible civilian tech structures may omit native radar discovery.
			// Do not let this category override cloak/sensor rejection.
			const bool visibleTech = techBuilding && techno->CloakState == CloakState::Uncloaked;
			const bool radar = nativeRadar || visibleTech;
			const auto visibility = FullMapIcons::ObjectVisibility(true, type->Invisible, own,
				techno->DiscoveredByCurrentPlayer || visibleTech, own ? 255 : FullMapTerrain::VisibilityAtCell(cell.X, cell.Y), radar, detection != 0);
			if (visibility == Visibility::Hidden) continue;
			Kind icon;
			switch (techno->WhatAmI())
			{
			case AbstractType::Building: icon = Kind::Building; break;
			case AbstractType::Infantry: icon = Kind::Infantry; break;
			case AbstractType::Aircraft: icon = Kind::Aircraft; break;
			case AbstractType::Unit: icon = type->Naval ? Kind::Ship : Kind::Vehicle; break;
			default: continue;
			}
			const ObjectTypeClass* displayType = type;
			const HouseClass* displayOwner = techno->Owner;
			const bool disguised = !own && techno->IsDisguised();
			if (disguised)
			{
				displayType = techno->GetDisguise(false);
				displayOwner = techno->GetDisguiseHouse(false);
				if (!displayType || !displayOwner) continue;
				switch (displayType->WhatAmI())
				{
				case AbstractType::InfantryType: icon = Kind::Infantry; break;
				case AbstractType::UnitType: icon = Kind::Vehicle; break;
				case AbstractType::AircraftType: icon = Kind::Aircraft; break;
				case AbstractType::BuildingType: icon = Kind::Building; break;
				default:
					// A mirage appearing as terrain must not receive a vehicle
					// marker. A genuinely detected signal remains anonymous.
					if (visibility != Visibility::Signal) continue;
					break;
				}
			}
			if (techBuilding && !disguised && visibility == Visibility::Known)
				icon = Kind::TechBuilding;
			const auto position = TacticalClass::CoordsToScreen(techno->Location);
			Item item { visibility == Visibility::Signal ? Kind::Signal : icon,
				{ static_cast<double>(position.X), static_cast<double>(position.Y) }, displayOwner->Color,
				visibility == Visibility::Signal ? std::wstring(StringTable::TryFetchString("SF:FULLMAP_KIND_SIGNAL", L"探测信号")) : NameOf(displayType),
				std::wstring(displayOwner->UIName, wcsnlen_s(displayOwner->UIName, 20)), {},
				techno->Health, std::max(1, type->Strength), techno->IsSelected && !disguised,
				visibility == Visibility::Known && !disguised };
			if (techno->WhatAmI() == AbstractType::Building && visibility == Visibility::Known && !disguised)
			{
				const auto* building = static_cast<const BuildingClass*>(techno);
				const auto* foundation = building->Type->GetFoundationData(false);
				double xSum = 0.0, ySum = 0.0;
				if (foundation)
				{
					for (int i = 0; i < 512 && foundation[i].X != 0x7FFF && foundation[i].Y != 0x7FFF; ++i)
					{
						const CellStruct c { static_cast<short>(cell.X + foundation[i].X), static_cast<short>(cell.Y + foundation[i].Y) };
						const auto* terrain = MapClass::Instance.TryGetCellAt(c);
						if (!terrain || !MapClass::Instance.CoordinatesLegal(c)) continue;
						item.Foundation.push_back({ c.X, c.Y, terrain->Level });
						const auto center = FullMapGeometry::CellCenter(c.X, c.Y, terrain->Level);
						xSum += center.X; ySum += center.Y;
					}
				}
				if (!item.Foundation.empty()) item.Position = { xSum / item.Foundation.size(), ySum / item.Foundation.size() };
			}
			g_Items.push_back(std::move(item));
		}
		if (FullMapSettings::ShowOreSpawners)
		{
			for (const auto* terrain : TerrainClass::Array)
			{
				if (!terrain || !terrain->Type || !terrain->Type->SpawnsTiberium || !terrain->IsOnMap
					|| terrain->InLimbo || !terrain->IsAlive || terrain->IsCrumbling) continue;
				const auto cell = CellClass::Coord2Cell(terrain->Location);
				if (!MapClass::Instance.CoordinatesLegal(cell)
					|| FullMapTerrain::VisibilityAtCell(cell.X, cell.Y) != 255) continue;
				const auto at = TacticalClass::CoordsToScreen(terrain->Location);
				g_Items.push_back({ Kind::OreSpawner, { static_cast<double>(at.X), static_cast<double>(at.Y) },
					{ 255, 204, 66 }, std::wstring(StringTable::TryFetchString("SF:FULLMAP_ORE_PILLAR", L"矿柱")), std::wstring(StringTable::TryFetchString("SF:FULLMAP_ORE_SOURCE", L"资源再生点")), {}, 1, 1, false, false });
			}
		}
		FullMapPerf::AddWork(FullMapPerf::Work::SnapshotObjects, g_Items.size());
		// Footprints under units, aircraft over ground units. No live pointers
		// are retained in this per-frame snapshot.
		std::stable_sort(g_Items.begin(), g_Items.end(), [](const Item& a, const Item& b)
		{
			return static_cast<int>(a.Icon) < static_cast<int>(b.Icon);
		});
	}
	Point2D Screen(const FullMapObjects::View& view, NativePoint native)
	{
		int x = 0, y = 0;
		FullMapTerrain::TacticalPointToCanvas(native.X, native.Y, x, y);
		return { view.Map.X + (x - view.SourceX) * view.Map.Width / std::max(1, view.SourceW),
			view.Map.Y + (y - view.SourceY) * view.Map.Height / std::max(1, view.SourceH) };
	}
	bool Inside(Point2D p, const RectangleStruct& r)
	{
		return p.X >= r.X && p.Y >= r.Y && p.X < r.X + r.Width && p.Y < r.Y + r.Height;
	}
	void TintQuad(FullMapIcons::Canvas& canvas, const std::array<Point2D, 4>& points, uint16_t tint)
	{
		int top = INT_MAX, bottom = INT_MIN;
		for (const auto p : points) { top = std::min(top, p.Y); bottom = std::max(bottom, p.Y); }
		for (int y = std::max({ top, canvas.Clip.Y, 0 }); y < std::min({ bottom, canvas.Clip.Y + canvas.Clip.Height, canvas.Height }); ++y)
		{
			double left = 1e20, right = -1e20;
			for (int i = 0; i < 4; ++i)
			{
				const auto a = points[i], b = points[(i + 1) % 4];
				if ((y + 0.5 >= a.Y && y + 0.5 < b.Y) || (y + 0.5 >= b.Y && y + 0.5 < a.Y))
				{
					const double x = a.X + (y + 0.5 - a.Y) * (b.X - a.X) / (b.Y - a.Y);
					left = std::min(left, x); right = std::max(right, x);
				}
			}
			if (right < left) continue;
			const int begin = std::max({ static_cast<int>(std::ceil(left)), canvas.Clip.X, 0 });
			const int end = std::min({ static_cast<int>(std::ceil(right)), canvas.Clip.X + canvas.Clip.Width, canvas.Width });
			for (int x = begin; x < end; ++x)
			{
				auto& pixel = canvas.Pixels[static_cast<ptrdiff_t>(y) * canvas.Pitch + x];
				pixel = Mix(pixel, tint, 96);
			}
		}
	}
	void DrawFootprint(FullMapIcons::Canvas& canvas, const FullMapObjects::View& view, const Item& item, uint16_t color)
	{
		for (const auto& cell : item.Foundation)
		{
			const auto center = FullMapGeometry::CellCenter(cell.X, cell.Y, cell.Level);
			const std::array<Point2D, 4> corners { Screen(view, { center.X, center.Y - 15 }),
				Screen(view, { center.X + 30, center.Y }), Screen(view, { center.X, center.Y + 15 }),
				Screen(view, { center.X - 30, center.Y }) };
			TintQuad(canvas, corners, Pixel(16, 22, 30));
			const int neighbours[4][2] { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
			for (int edge = 0; edge < 4; ++edge)
			{
				const int nx = cell.X + neighbours[edge][0], ny = cell.Y + neighbours[edge][1];
				const bool adjacent = std::any_of(item.Foundation.begin(), item.Foundation.end(), [&](const FoundationCell& c)
					{ return c.X == nx && c.Y == ny; });
				if (adjacent) continue;
				const auto a = corners[edge], b = corners[(edge + 1) % 4];
				canvas.Line(a.X, a.Y, b.X, b.Y, Pixel(12, 16, 20), 3);
				canvas.Line(a.X, a.Y, b.X, b.Y, color);
			}
		}
	}
	void Text(const std::wstring& text, Point2D at, uint16_t color, RectangleStruct clip)
	{
		// Fancy_Text_Print_Wide -> 0x4A5EB0 adds the clip origin to the
		// position. Callers use screen coordinates, so convert to local here.
		at.X -= clip.X;
		at.Y -= clip.Y;
		DSurface::Composite->DrawText(text.c_str(), &clip, &at, color, 0, TextPrintType::Metal12 | TextPrintType::BrightColor);
	}
	RectangleStruct TextSize(const std::wstring& text)
	{
		auto size = Drawing::GetTextDimensions(text.c_str(), { 0, 0 }, 0, 2, 0);
		size.Width = std::max(1, size.Width); size.Height = std::max(14, size.Height);
		return size;
	}
	void Card(const FullMapObjects::View& view, const std::wstring& first, const std::wstring& second, Point2D at, uint16_t color)
	{
		const auto a = TextSize(first), b = TextSize(second);
		const int width = std::min(view.Clip.Width, std::max(a.Width, b.Width) + 16);
		const int height = std::min(view.Clip.Height, a.Height + b.Height + 13);
		RectangleStruct rect { std::clamp(at.X, view.Clip.X, view.Clip.X + view.Clip.Width - width),
			std::clamp(at.Y, view.Clip.Y, view.Clip.Y + view.Clip.Height - height), width, height };
		ColorStruct dark { 10, 16, 24 };
		DSurface::Composite->FillRectTrans(&rect, &dark, 88);
		DSurface::Composite->DrawRect(&rect, Pixel(96, 108, 124));
		Text(first, { rect.X + 8, rect.Y + 5 }, color, rect);
		Text(second, { rect.X + 8, rect.Y + a.Height + 7 }, Pixel(225, 232, 240), rect);
	}

	struct OreCluster
	{
		std::vector<CellStruct> Cells;
		int64_t Value = 0;
		const TiberiumClass* Resource = nullptr;
		bool MixedTypes = false;
	};

	bool IsOreCell(CellStruct cell)
	{
		if (!MapClass::Instance.CoordinatesLegal(cell)) return false;
		const auto* pCell = MapClass::Instance.TryGetCellAt(cell);
		if (!pCell || pCell->OverlayTypeIndex < 0 || pCell->OverlayTypeIndex >= OverlayTypeClass::Array.Count)
			return false;
		const auto* overlay = OverlayTypeClass::Array[pCell->OverlayTypeIndex];
		return overlay && overlay->Tiberium;
	}

	OreCluster CollectOreCluster(CellStruct start)
	{
		OreCluster result;
		if (!IsOreCell(start) || FullMapTerrain::VisibilityAtCell(start.X, start.Y) != 255)
			return result;

		FullMapPerf::Scope timing(FullMapPerf::Stage::OreSearch);
		std::vector<uint8_t> visited(MapClass::MaxCells, 0);
		std::queue<CellStruct> pending;
		pending.push(start);
		visited[start.Y * 512 + start.X] = 1;
		while (!pending.empty())
		{
			const CellStruct cell = pending.front();
			pending.pop();
			const auto* pCell = MapClass::Instance.TryGetCellAt(cell);
			if (!pCell || !IsOreCell(cell)) continue;
			const auto* resource = TiberiumClass::Find(pCell->OverlayTypeIndex);
			if (result.Cells.empty()) result.Resource = resource;
			else if (resource != result.Resource) result.MixedTypes = true;
			result.Cells.push_back(cell);
			// FA2sp multi-selection -> CMapDataExt::GetOreValue:
			// OverlayData * the matching Tiberium Value, with no +1. The native
			// GetContainedTiberiumValue uses +1 and does not match editor totals.
			if (resource)
				result.Value += static_cast<int64_t>(pCell->OverlayData) * resource->Value;

			// 八邻域更符合玩家看到的连续矿脉轮廓，斜角相接也算同一片矿区。
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy) continue;
					const CellStruct next { static_cast<short>(cell.X + dx), static_cast<short>(cell.Y + dy) };
					if (!MapClass::Instance.CoordinatesLegal(next)) continue;
					const int index = next.Y * 512 + next.X;
					if (visited[index] || !IsOreCell(next)
						|| FullMapTerrain::VisibilityAtCell(next.X, next.Y) != 255) continue;
					visited[index] = 1;
					pending.push(next);
				}
			}
		}
		FullMapPerf::AddWork(FullMapPerf::Work::OreCells, result.Cells.size());
		return result;
	}

	void BlendOreDiamond(FullMapIcons::Canvas& canvas, int cx, int cy, int rx, int ry,
		uint16_t color, int alpha)
	{
		for (int dy = -ry; dy <= ry; ++dy)
		{
			const int span = rx * (ry - std::abs(dy)) / std::max(1, ry);
			for (int dx = -span; dx <= span; ++dx)
			{
				const int x = cx + dx, y = cy + dy;
				if (!canvas.Contains(x, y)) continue;
				auto& pixel = canvas.Pixels[static_cast<ptrdiff_t>(y) * canvas.Pitch + x];
				pixel = Mix(pixel, color, alpha);
			}
		}
	}

	void DrawOreClusterGlow(FullMapIcons::Canvas& canvas, const FullMapObjects::View& view,
		const OreCluster& cluster)
	{
		const uint16_t glow = Pixel(255, 210, 54);
		const uint16_t edge = Pixel(255, 236, 132);
		for (const auto cell : cluster.Cells)
		{
			const auto* pCell = MapClass::Instance.TryGetCellAt(cell);
			const int level = pCell ? pCell->Level : 0;
			const auto center = Screen(view, FullMapGeometry::CellCenter(cell.X, cell.Y, level));
			const auto east = Screen(view, FullMapGeometry::CellCenter(cell.X + 1, cell.Y, level));
			const auto south = Screen(view, FullMapGeometry::CellCenter(cell.X, cell.Y + 1, level));
			const int rx = std::max(4, std::max(std::abs(east.X - center.X), std::abs(south.X - center.X)));
			const int ry = std::max(3, std::max(std::abs(east.Y - center.Y), std::abs(south.Y - center.Y)));
			if (center.X + rx + 3 < view.Clip.X || center.Y + ry + 2 < view.Clip.Y
				|| center.X - rx - 3 >= view.Clip.X + view.Clip.Width
				|| center.Y - ry - 2 >= view.Clip.Y + view.Clip.Height) continue;
			BlendOreDiamond(canvas, center.X, center.Y, rx + 3, ry + 2, glow, 18);
			BlendOreDiamond(canvas, center.X, center.Y, rx + 1, ry + 1, glow, 32);
			canvas.Line(center.X - rx, center.Y, center.X, center.Y - ry, edge, 1);
			canvas.Line(center.X, center.Y - ry, center.X + rx, center.Y, edge, 1);
			canvas.Line(center.X + rx, center.Y, center.X, center.Y + ry, edge, 1);
			canvas.Line(center.X, center.Y + ry, center.X - rx, center.Y, edge, 1);
		}
	}
}

void FullMapObjects::Invalidate()
{
	g_Frame = -1; g_Player = nullptr; g_Items.clear();
}

void FullMapObjects::Draw(const View& view)
{
	if (!FullMapTerrain::IsReady() || !DSurface::Composite || view.Clip.Width <= 0 || view.Clip.Height <= 0) return;
	FullMapPerf::Scope timing(FullMapPerf::Stage::Objects);
	Snapshot();
	auto* surface = DSurface::Composite;
	if (surface->GetBytesPerPixel() != 2) return;
	FullMapPerf::Scope lockTiming(FullMapPerf::Stage::ObjectLock);
	auto* pixels = static_cast<uint16_t*>(surface->Lock(0, 0));
	lockTiming.Finish();
	if (!pixels) return;
	FullMapIcons::Canvas canvas { pixels, surface->Width, surface->Height, surface->GetPitch() / 2,
		{ view.Clip.X, view.Clip.Y, view.Clip.Width, view.Clip.Height } };
	OreCluster hoveredOre;
	if ((FullMapSettings::ShowOreValue || FullMapSettings::HighlightOre) && Inside(view.Mouse, view.Clip))
	{
		const int px = view.SourceX + (view.Mouse.X - view.Map.X) * view.SourceW / std::max(1, view.Map.Width);
		const int py = view.SourceY + (view.Mouse.Y - view.Map.Y) * view.SourceH / std::max(1, view.Map.Height);
		short x = 0, y = 0;
		if (FullMapTerrain::CanvasToCell(px, py, x, y))
			hoveredOre = CollectOreCluster({ x, y });
	}
	if (FullMapSettings::HighlightOre && !hoveredOre.Cells.empty())
	{
		FullMapPerf::Scope glowTiming(FullMapPerf::Stage::OreGlow);
		DrawOreClusterGlow(canvas, view, hoveredOre);
	}
	FullMapPerf::Scope glyphTiming(FullMapPerf::Stage::Glyphs);
	const Item* hovered = nullptr;
	Point2D hoverPosition {};
	int bestDistance = INT_MAX;
	std::vector<std::pair<const Item*, Point2D>> labels;
	std::vector<RectangleStruct> occupied;
	for (const auto& item : g_Items)
	{
		const auto at = Screen(view, item.Position);
		if (item.Icon != Kind::Building && (at.X < view.Clip.X - 16 || at.Y < view.Clip.Y - 16
			|| at.X > view.Clip.X + view.Clip.Width + 16 || at.Y > view.Clip.Y + view.Clip.Height + 16)) continue;
		const auto color = static_cast<uint16_t>(Drawing::RGB_To_Int(item.Color));
		if (!item.Foundation.empty()) DrawFootprint(canvas, view, item, color);
		const int radius = FullMapIcons::Radius(item.Icon, view.Zoom, view.Clip.Width);
		const FullMapIcons::Colors colors { color, Pixel(9, 14, 20), Pixel(244, 248, 252),
			item.Health * 3LL >= item.Maximum ? Pixel(80, 232, 120) : Pixel(255, 112, 72), Pixel(48, 58, 68) };
		FullMapIcons::Draw(canvas, item.Icon, at.X, at.Y, radius, colors, item.Selected);
		const int dx = view.Mouse.X - at.X, dy = view.Mouse.Y - at.Y;
		const int distance = dx * dx + dy * dy;
		if (Inside(view.Mouse, view.Clip) && distance <= std::max(18, radius + 6) * std::max(18, radius + 6)
			&& distance < bestDistance)
		{
			hovered = &item; hoverPosition = at; bestDistance = distance;
		}
		if (FullMapSettings::ShowHealthBars && item.Selected && item.ShowHealth)
			FullMapIcons::HealthBar(canvas, at.X - std::max(12, radius * 2), at.Y + radius + 5, std::max(24, radius * 4), item.Health, item.Maximum, colors);
		if (FullMapSettings::ShowBuildingLabels && !item.Name.empty()
			&& ((item.Icon == Kind::Building && view.Zoom >= 1.8f)
				|| ((item.Icon == Kind::TechBuilding || item.Icon == Kind::OreSpawner) && view.Zoom >= 1.3f)))
		{
			const auto size = TextSize(item.Name);
			const RectangleStruct rect { at.X - size.Width / 2, at.Y + radius + 5, size.Width + 4, size.Height + 2 };
			const bool overlaps = std::any_of(occupied.begin(), occupied.end(), [&](const RectangleStruct& r)
				{ return rect.X < r.X + r.Width && r.X < rect.X + rect.Width && rect.Y < r.Y + r.Height && r.Y < rect.Y + rect.Height; });
			if (!overlaps && Inside({ rect.X, rect.Y }, view.Clip)
				&& Inside({ rect.X + rect.Width, rect.Y + rect.Height }, view.Clip))
			{
				occupied.push_back(rect); labels.emplace_back(&item, Point2D { rect.X + 2, rect.Y + 1 });
			}
		}
	}
	if (FullMapSettings::ShowHealthBars && hovered && hovered->ShowHealth)
	{
		const auto color = static_cast<uint16_t>(Drawing::RGB_To_Int(hovered->Color));
		const int radius = FullMapIcons::Radius(hovered->Icon, view.Zoom, view.Clip.Width);
		const int width = std::max(32, radius * 4);
		FullMapIcons::HealthBar(canvas, hoverPosition.X - width / 2, hoverPosition.Y + radius + 5, width,
			hovered->Health, hovered->Maximum, { color, Pixel(9, 14, 20), Pixel(244, 248, 252),
				hovered->Health * 3LL >= hovered->Maximum ? Pixel(80, 232, 120) : Pixel(255, 112, 72), Pixel(48, 58, 68) });
	}
	surface->Unlock();
	glyphTiming.Finish();
	FullMapPerf::Scope labelsTiming(FullMapPerf::Stage::Labels);
	for (const auto& [item, at] : labels)
	{
		const auto size = TextSize(item->Name);
		RectangleStruct backing { at.X - 2, at.Y - 1, size.Width + 4, size.Height + 2 };
		ColorStruct dark { 12, 18, 24 };
		surface->FillRectTrans(&backing, &dark, 75);
		Text(item->Name, at, Pixel(240, 244, 248), view.Clip);
	}
	if (hovered)
	{
		std::wstring first = hovered->Name + L"  ·  " + KindLabel(hovered->Icon);
		std::wstring second = hovered->Owner;
		if (hovered->ShowHealth) second += L"  " + std::to_wstring(hovered->Health) + L" / " + std::to_wstring(hovered->Maximum);
		Card(view, first, second, { view.Mouse.X + 18, view.Mouse.Y + 20 }, Pixel(244, 248, 252));
	}
	else if (FullMapSettings::ShowOreValue && !hoveredOre.Cells.empty())
	{
		const std::wstring name = ResourceName(hoveredOre.Resource);
		const std::wstring first = hoveredOre.MixedTypes
			? std::wstring(StringTable::TryFetchString("SF:FULLMAP_ORE_MIXED", L"混合矿区"))
			: (name.empty() ? std::wstring(StringTable::TryFetchString("SF:FULLMAP_ORE_GENERIC", L"矿产")) : name)
				+ L"  ·  " + StringTable::TryFetchString("SF:FULLMAP_ORE_CONNECTED", L"相连矿区");
		const std::wstring second = std::wstring(StringTable::TryFetchString("SF:FULLMAP_ORE_TOTAL", L"可见矿区总价值"))
			+ L"  $" + std::to_wstring(hoveredOre.Value)
			+ L"  （" + std::to_wstring(hoveredOre.Cells.size())
			+ StringTable::TryFetchString("SF:FULLMAP_ORE_CELLS", L"格") + L"）";
		Card(view, first, second, { view.Mouse.X + 18, view.Mouse.Y + 20 }, Pixel(255, 222, 112));
	}
}
