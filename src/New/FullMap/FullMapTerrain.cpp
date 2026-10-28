#include "FullMapTerrain.h"
#include "FullMapGeometry.h"
#include "FullMapSettings.h"
#include "FullMapPerf.h"

#include <MapClass.h>
#include <CellClass.h>
#include <IsometricTileTypeClass.h>
#include <OverlayTypeClass.h>
#include <Drawing.h>
#include <FileSystem.h>
#include <ScenarioClass.h>
#include <TacticalClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace
{
	using namespace FullMapGeometry;
	constexpr uint32_t NoCell = UINT32_MAX;
	constexpr int MaxDimension = 4096;
	constexpr int ComposeTileSize = 32;
	constexpr uint16_t MixedFog = 256;

	struct EngineArt
	{
		DEFINE_REFERENCE(int, ClearTileIndex, 0xAA10B0u)
		DEFINE_REFERENCE(int, ShroudLevelHeight, 0xABDE88u)
		DEFINE_ARRAY_REFERENCE(int, [16], FourVariants, 0x81CCA8u)
		DEFINE_ARRAY_REFERENCE(int, [64], EightVariants, 0x89E620u)
		DEFINE_REFERENCE(bool, EightVariantsReady, 0x89E7C7u)
	};

	struct Sprite
	{
		int X = 0, Y = 0, W = 60, H = 30;
		std::vector<uint16_t> Pixels;
		std::vector<uint8_t> Coverage;
	};

	struct LightingSample
	{
		uint16_t Terrain = 1000;
		uint16_t Red = 1000;
		uint16_t Green = 1000;
		uint16_t Blue = 1000;
		bool operator==(const LightingSample& other) const
		{
			return Terrain == other.Terrain && Red == other.Red && Green == other.Green && Blue == other.Blue;
		}
		bool operator!=(const LightingSample& other) const { return !(*this == other); }
	};

	struct Cell
	{
		CellClass* Source;
		int X, Y, Level;
		int TileTypeIndex;
		uint8_t TileHeight;
		const Sprite* Image;
		int OverlayIndex = -1;
		uint8_t OverlayStage = 0;
		LightingSample Lighting;
		Rect Bounds {};
	};

	Projection g_Projection;
	std::vector<Cell> g_Cells;
	std::unordered_map<const TileImageHeader*, Sprite> g_Sprites;
	std::vector<int> g_CellIndex;
	std::vector<uint16_t> g_Base;
	std::vector<uint16_t> g_View;
	std::vector<uint16_t> g_Work;
	std::vector<uint32_t> g_Owner;
	std::vector<uint8_t> g_OrePixels;
	std::vector<uint8_t> g_Fog;
	std::vector<uint8_t> g_ComposeFog;
	std::vector<LightingSample> g_Lighting;
	std::vector<LightingSample> g_ComposeLighting;
	Composition g_Composition;
	uint64_t g_Revision = 0;
	uint64_t g_ComposedPixels = 0;
	bool g_Ready = false;
	bool g_Building = false;
	size_t g_BuildCursor = 0;
	size_t g_FogCursor = 0;
	int g_LastFogFrame = -1;
	int g_FoggedCells = 0;
	bool g_ScanChanged = false;
	int g_TileColumns = 0;
	std::vector<uint8_t> g_DirtyTiles;
	std::vector<uint8_t> g_ComposeTiles;
	std::vector<uint16_t> g_UniformFog;
	struct LightingTable { uint16_t Red[256], Green[256], Blue[256]; };
	std::vector<LightingTable> g_LightingTables;
	std::vector<uint32_t> g_LightingTableIndex;
	uint8_t g_ComposeShroudBrightness = 0;
	uint8_t g_RequestedShroudBrightness = 0;

	void DirtyCell(size_t index, bool neighbours = false)
	{
		const auto mark = [](const Cell& cell)
		{
			const auto& r = cell.Bounds;
			if (r.Width <= 0 || r.Height <= 0) return;
			for (int ty = r.Y / ComposeTileSize; ty <= (r.Y + r.Height - 1) / ComposeTileSize; ++ty)
				for (int tx = r.X / ComposeTileSize; tx <= (r.X + r.Width - 1) / ComposeTileSize; ++tx)
					g_DirtyTiles[static_cast<size_t>(ty) * g_TileColumns + tx] = 1;
		};
		const auto& cell = g_Cells[index];
		mark(cell);
		if (!neighbours) return;
		// SmoothFog samples at most the eight adjacent cells, including cliff
		// pixels clamped to their owner's half-cell bounds.
		for (int y = std::max(0, cell.Y - 1); y <= std::min(511, cell.Y + 1); ++y)
			for (int x = std::max(0, cell.X - 1); x <= std::min(511, cell.X + 1); ++x)
				if (const int other = g_CellIndex[y * 512 + x]; other >= 0)
					mark(g_Cells[other]);
	}

	// Only changed tiles differ between the two image buffers. Keep both
	// complete outside an active pass, without copying the whole canvas.
	void SyncCompositionTiles()
	{
		for (size_t i = 0; i < g_ComposeTiles.size(); ++i)
		{
			if (!g_ComposeTiles[i]) continue;
			const int x = static_cast<int>(i % g_TileColumns) * ComposeTileSize;
			const int y = static_cast<int>(i / g_TileColumns) * ComposeTileSize;
			const int width = std::min(ComposeTileSize, FullMapTerrain::CanvasW - x);
			const int bottom = std::min(y + ComposeTileSize, FullMapTerrain::CanvasH);
			for (int row = y; row < bottom; ++row)
			{
				const size_t start = static_cast<size_t>(row) * FullMapTerrain::CanvasW + x;
				std::memcpy(g_Work.data() + start, g_View.data() + start, width * sizeof(uint16_t));
			}
		}
	}

	void CaptureCompositionFog()
	{
		if (!g_FoggedCells)
		{
			g_UniformFog.assign(g_Cells.size(), 255);
			return;
		}
		g_UniformFog.resize(g_Cells.size());
		for (size_t i = 0; i < g_Cells.size(); ++i)
		{
			const auto& cell = g_Cells[i];
			const uint8_t center = g_ComposeFog[cell.Y * 512 + cell.X];
			uint16_t fog = center == 40 ? g_ComposeShroudBrightness : center;
			for (int y = std::max(0, cell.Y - 1); y <= std::min(511, cell.Y + 1); ++y)
				for (int x = std::max(0, cell.X - 1); x <= std::min(511, cell.X + 1); ++x)
				{
					if (g_CellIndex[y * 512 + x] < 0) continue;
					const uint8_t value = g_ComposeFog[y * 512 + x];
					if ((value == 40 ? g_ComposeShroudBrightness : value) != fog)
						fog = MixedFog;
				}
			g_UniformFog[i] = fog;
		}
	}
	int g_MissingTiles = 0;
	int g_EdgeShroudFallbacks = 0;
	RectangleStruct g_MapRect {};
	uintptr_t g_MapCellsBase = 0;
	int g_MapCellsCapacity = 0;

	uint16_t Pack(RGB rgb)
	{
		return static_cast<uint16_t>(Drawing::RGB_To_Int(rgb.R, rgb.G, rgb.B));
	}

	uint8_t VisibilityOf(const CellClass* cell)
	{
		bool shrouded = cell->IsShrouded();
		if (shrouded && (cell->AltFlags & AltCellFlags::Mapped) && !cell->GapsCoveringThisCell)
		{
			// 0x586360 projects the shroud lookup backwards by elevation. At
			// the top/side map edge this can hit InvalidCell even on a fully
			// revealed map. Keep the real mapped terrain cell's state there.
			const int levelHeight = EngineArt::ShroudLevelHeight > 0 ? EngineArt::ShroudLevelHeight : Unsorted::LevelHeight;
			const int planes = cell->GetFloorHeight(Point2D { 128, 128 }) / levelHeight;
			const int shift = (planes + 1) / 2;
			const CellStruct query { static_cast<short>(cell->MapCoords.X - shift),
				static_cast<short>(cell->MapCoords.Y - shift) };
			const auto& map = MapClass::Instance;
			auto* projected = map.CoordinatesLegal(query) ? map.TryGetCellAt(query) : nullptr;
			bool outside = !projected;
			if (projected && (planes & 1))
			{
				const auto* adjacent = projected->GetNeighbourCell(FacingType::SouthEast);
				outside = !adjacent || adjacent == &MapClass::InvalidCell || !map.CoordinatesLegal(adjacent->MapCoords);
			}
			if (outside)
			{
				shrouded = false;
				if (g_EdgeShroudFallbacks < INT_MAX) ++g_EdgeShroudFallbacks;
			}
		}
		if (shrouded) return 40;
		const auto* scenario = ScenarioClass::Instance;
		return scenario && scenario->SpecialFlags.FogOfWar && (cell->Flags & CellFlags::Fogged) ? 150 : 255;
	}


	LightingSample CaptureLighting(CellClass* cell)
	{
		int intensity = 0;
		int ambient = 0;
		int red1 = 1000, green1 = 1000, blue1 = 1000;
		int red2 = 1000, green2 = 1000, blue2 = 1000;
		// InitLightConvert uses this native calculation for the current scenario
		// and local light sources, without changing the cell's cached palette.
		cell->CalculateLightSourceLighting(intensity, ambient, red1, green1, blue1,
			red2, green2, blue2);

		// The game stores global/height lighting in Intensity_Terrain. Apply any
		// newer local lamp ambient using UpdateCellLighting's 16.16 scaling.
		const int64_t oldIntensity = cell->Intensity;
		const int64_t terrainBase = oldIntensity > 0
			? static_cast<int64_t>(cell->Intensity_Terrain) * 65536 / oldIntensity
			: 0;
		const int64_t terrain = (static_cast<int64_t>(intensity)
			* (terrainBase + ambient - cell->Ambient)) >> 16;
		const auto scale = [](int value)
		{
			return static_cast<uint16_t>(std::clamp(value, 0, 2000));
		};
		return { scale(static_cast<int>(terrain)), scale(red2), scale(green2), scale(blue2) };
	}

	uint16_t ApplyLighting(uint16_t base, const LightingSample& light)
	{
		const auto color = Drawing::Int_To_RGB(base);
		const auto scale = [&](uint8_t channel, uint16_t color) -> int
		{
			const int64_t value = static_cast<int64_t>(channel) * light.Terrain * color / 1000000;
			return static_cast<int>(std::clamp<int64_t>(value, 0, 255));
		};
		return static_cast<uint16_t>(Drawing::RGB_To_Int(
			scale(color.R, light.Red), scale(color.G, light.Green), scale(color.B, light.Blue)));
	}


	void CaptureCompositionLighting()
	{
		g_LightingTables.clear();
		g_LightingTableIndex.assign(g_Cells.size(), NoCell);
		std::unordered_map<uint64_t, uint32_t> lookup;
		for (size_t i = 0; i < g_Cells.size(); ++i)
		{
			const auto& light = g_ComposeLighting[i];
			if (!g_UniformFog[i] || light == LightingSample {}) continue;
			const uint64_t key = light.Terrain | (uint64_t { light.Red } << 16)
				| (uint64_t { light.Green } << 32) | (uint64_t { light.Blue } << 48);
			auto found = lookup.find(key);
			if (found == lookup.end())
			{
				// Bound memory even on maps with thousands of distinct lamp
				// combinations. Uncached samples retain the exact scalar path.
				if (g_LightingTables.size() == 256)
				{
					g_LightingTableIndex[i] = NoCell - 1;
					continue;
				}
				const auto tableIndex = static_cast<uint32_t>(g_LightingTables.size());
				found = lookup.emplace(key, tableIndex).first;
				g_LightingTables.emplace_back();
				auto& table = g_LightingTables.back();
				const auto scale = [&](int channel, uint16_t color)
				{
					return static_cast<int>(std::clamp<int64_t>(int64_t { channel } * light.Terrain * color / 1000000, 0, 255));
				};
				for (int c = 0; c < 256; ++c)
				{
					table.Red[c] = static_cast<uint16_t>(Drawing::RGB_To_Int(scale(c, light.Red), 0, 0));
					table.Green[c] = static_cast<uint16_t>(Drawing::RGB_To_Int(0, scale(c, light.Green), 0));
					table.Blue[c] = static_cast<uint16_t>(Drawing::RGB_To_Int(0, 0, scale(c, light.Blue)));
				}
			}
			g_LightingTableIndex[i] = found->second;
		}
	}

	uint16_t Blend(uint16_t base, uint16_t tint, int amount)
	{
		const auto a = Drawing::Int_To_RGB(base), b = Drawing::Int_To_RGB(tint);
		return static_cast<uint16_t>(Drawing::RGB_To_Int(
			(a.R * (255 - amount) + b.R * amount) / 255,
			(a.G * (255 - amount) + b.G * amount) / 255,
			(a.B * (255 - amount) + b.B * amount) / 255));
	}

	const TileImageHeader* ReadImage(IsometricTileTypeClass* type, int subIndex)
	{
		if (!type) return nullptr;
		// GetImage @0x544CB0 only loads art and fixes up its offset table. It
		// does not choose a random tile or alter map/logic state.
		const auto* bytes = reinterpret_cast<const uint8_t*>(type->GetImage());
		if (!bytes) return nullptr;
		const auto* file = reinterpret_cast<const TileFileHeader*>(bytes);
		if (file->Width <= 0 || file->Height <= 0 || file->Width > 64 || file->Height > 64
			|| file->TileWidth != 60 || file->TileHeight != 30)
			return nullptr;
		const auto* table = reinterpret_cast<const uint32_t*>(bytes + sizeof(TileFileHeader));
		const uintptr_t address = table[subIndex % (file->Width * file->Height)];
		if (!address) return nullptr;
		// In-memory TMP table entries are absolute pointers after 0x547020;
		// unmodified file offsets are also accepted for custom art loaders.
		return reinterpret_cast<const TileImageHeader*>(address < reinterpret_cast<uintptr_t>(bytes)
			? reinterpret_cast<uintptr_t>(bytes) + address : address);
	}

	const TileImageHeader* GetTile(CellClass* cell)
	{
		if (IsometricTileTypeClass::Array.Count <= 0) return nullptr;
		int typeIndex = cell->IsoTileTypeIndex;
		int subIndex = static_cast<uint8_t>(cell->Height); // TMP sub-tile index, @+0x11A
		if (typeIndex < 0 || typeIndex == 0xFFFF || typeIndex >= IsometricTileTypeClass::Array.Count)
		{
			typeIndex = EngineArt::ClearTileIndex; // 0x480350 uses the theater's actual ClearTile
			subIndex = 0;
		}
		if (typeIndex < 0 || typeIndex >= IsometricTileTypeClass::Array.Count) return nullptr;
		auto* type = IsometricTileTypeClass::Array[typeIndex];
		const auto* image = ReadImage(type, subIndex);
		if (!image || type->unk_2F0 <= 1) return image;
		int variant = 0;
		if (image->Flags & 4)
		{
			variant = (static_cast<uint32_t>(cell->Flags) >> 13) & 1;
		}
		else
		{
			// Read the same variation tables as 0x4814F0, without calling its
			// lazy random-table initializer from a local UI/render path.
			const auto* file = reinterpret_cast<const TileFileHeader*>(type->GetImage());
			int x = cell->MapCoords.X, y = cell->MapCoords.Y;
			if (subIndex) { x /= file->Width; y = (y - subIndex / file->Width) / file->Height; }
			if (type->unk_2F0 <= 4)
				variant = EngineArt::FourVariants[(x & 3) | ((y & 3) << 2)];
			else if (EngineArt::EightVariantsReady)
				variant = EngineArt::EightVariants[(x & 7) | ((y & 7) << 3)];
		}
		variant = std::max(0, variant) % type->unk_2F0;
		for (int i = 0; i < variant && i < 64; ++i)
		{
			auto* next = reinterpret_cast<IsometricTileTypeClass*>(type->unk_2BC);
			if (!next) break;
			type = next;
		}
		const auto* alternate = ReadImage(type, subIndex);
		return alternate ? alternate : image;
	}

	const Sprite* GetSprite(const TileImageHeader* image)
	{
		auto [it, inserted] = g_Sprites.try_emplace(image);
		if (!inserted)
			return &it->second;
		Sprite& sprite = it->second;
		const bool hasExtra = image && (image->Flags & 1) && image->ExtraOffset >= sizeof(TileImageHeader)
			&& image->ExtraOffset < 16 * 1024 * 1024 && image->ExtraWidth <= 2048 && image->ExtraHeight <= 2048
			&& std::abs(image->ExtraX - image->X) <= 2048 && std::abs(image->ExtraY - image->Y) <= 2048;
		if (hasExtra)
		{
			sprite.X = std::min(0, image->ExtraX - image->X);
			sprite.Y = std::min(0, image->ExtraY - image->Y);
			sprite.W = std::max(60, image->ExtraX - image->X + static_cast<int>(image->ExtraWidth)) - sprite.X;
			sprite.H = std::max(30, image->ExtraY - image->Y + static_cast<int>(image->ExtraHeight)) - sprite.Y;
		}
		sprite.Pixels.assign(static_cast<size_t>(sprite.W) * sprite.H, 0);
		sprite.Coverage.assign(sprite.Pixels.size(), 0);

		const RGB left = image ? image->RadarLeft : RGB { 96, 96, 96 };
		const RGB right = image ? image->RadarRight : left;
		std::array<uint16_t, 256> leftColors {}, rightColors {};
		// Preserve fine terrain/cliff texture while anchoring both halves in
		// the TMP's own radar colours. No sidebar fog, units or marks are sampled.
		int sums[2] {}, counts[2] {};
		const auto* diamond = image ? reinterpret_cast<const uint8_t*>(image + 1) : nullptr;
		if (diamond)
		{
			DecodeDiamond(diamond, [&](int x, int, uint8_t index)
			{
				if (!index) return;
				const auto c = FileSystem::ISOx_PAL[index];
				const int half = x >= 30;
				sums[half] += (c.R * 3 + c.G * 6 + c.B) / 10;
				++counts[half];
			});
		}
		for (int i = 0; i < 256; ++i)
		{
			const auto c = FileSystem::ISOx_PAL[i];
			const int luminance = (c.R * 3 + c.G * 6 + c.B) / 10;
			for (int half = 0; half < 2; ++half)
			{
				const RGB radar = half ? right : left;
				const int mean = counts[half] ? std::max(1, sums[half] / counts[half]) : 128;
				const int shade = std::clamp(192 + luminance * 64 / mean, 192, 320);
				const RGB rgb { static_cast<uint8_t>(std::min(255, radar.R * shade / 256)),
					static_cast<uint8_t>(std::min(255, radar.G * shade / 256)),
					static_cast<uint8_t>(std::min(255, radar.B * shade / 256)) };
				(half ? rightColors : leftColors)[i] = Pack(rgb);
			}
		}
		auto put = [&](int x, int y, uint8_t index, bool extra)
		{
			if (image && extra && !index)
				return;
			const int sx = x - sprite.X, sy = y - sprite.Y;
			if (sx < 0 || sy < 0 || sx >= sprite.W || sy >= sprite.H)
				return;
			const size_t offset = static_cast<size_t>(sy) * sprite.W + sx;
			sprite.Pixels[offset] = image ? (x < 30 ? leftColors : rightColors)[index] : Pack(left);
			sprite.Coverage[offset] = extra ? 2 : 1;
		};
		if (diamond)
			DecodeDiamond(diamond, [&](int x, int y, uint8_t index) { put(x, y, index, false); });
		else
		{
			++g_MissingTiles;
			for (int y = 0; y < 30; ++y)
				for (int x = DiamondLeft(y); x < 60 - DiamondLeft(y); ++x)
					put(x, y, 1, false);
		}
		if (hasExtra)
		{
			const auto* extra = reinterpret_cast<const uint8_t*>(image) + image->ExtraOffset;
			for (uint32_t y = 0; y < image->ExtraHeight; ++y)
				for (uint32_t x = 0; x < image->ExtraWidth; ++x)
					put(image->ExtraX - image->X + x, image->ExtraY - image->Y + y,
						extra[static_cast<size_t>(y) * image->ExtraWidth + x], true);
		}
		return &sprite;
	}

	void DrawCell(size_t cellIndex, bool refresh = false)
	{
		Cell& cell = g_Cells[cellIndex];
		cell.OverlayIndex = cell.Source->OverlayTypeIndex;
		cell.OverlayStage = cell.Source->OverlayData;
		const Sprite& sprite = *cell.Image;
		const double left = 30.0 * (cell.X - cell.Y) - 30 + sprite.X;
		const double top = 15.0 * (cell.X + cell.Y - cell.Level) + sprite.Y;
		const auto a = g_Projection.ToCanvas(Point { left, top });
		const auto b = g_Projection.ToCanvas(Point { left + sprite.W, top + sprite.H });
		const int x0 = std::max(0, static_cast<int>(std::floor(a.X)));
		const int y0 = std::max(0, static_cast<int>(std::floor(a.Y)));
		const int x1 = std::min(FullMapTerrain::CanvasW, static_cast<int>(std::ceil(b.X)));
		const int y1 = std::min(FullMapTerrain::CanvasH, static_cast<int>(std::ceil(b.Y)));
		cell.Bounds = { x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0) };
		uint16_t overlayColor = 0;
		bool overlay = false;
		bool resource = false;
		const int index = cell.Source->OverlayTypeIndex;
		if (index >= 0 && index < OverlayTypeClass::Array.Count)
		{
			if (const auto* type = OverlayTypeClass::Array[index]; type && (type->Tiberium || type->Wall || type->DrawFlat))
			{
				const auto c = type->RadarColor;
				overlay = c.R || c.G || c.B;
				resource = type->Tiberium;
				overlayColor = static_cast<uint16_t>(Drawing::RGB_To_Int(c));
			}
		}
		for (int y = y0; y < y1; ++y)
		{
			const int sy = std::clamp(static_cast<int>((y + 0.5 - a.Y) / g_Projection.Scale), 0, sprite.H - 1);
			for (int x = x0; x < x1; ++x)
			{
				const int sx = std::clamp(static_cast<int>((x + 0.5 - a.X) / g_Projection.Scale), 0, sprite.W - 1);
				const size_t src = static_cast<size_t>(sy) * sprite.W + sx;
				if (!sprite.Coverage[src]) continue;
				const size_t dst = static_cast<size_t>(y) * FullMapTerrain::CanvasW + x;
				if (refresh && g_Owner[dst] != cellIndex) continue;
				g_OrePixels[dst] = resource && sprite.Coverage[src] == 1 ? 1 : 0;
				uint16_t color = sprite.Pixels[src];
				if (overlay && sprite.Coverage[src] == 1)
				{
					if (resource)
					{
						// Retain terrain underneath the deposit; small highlighted
						// nuggets encode density instead of a solid yellow tile.
						const int tx = sx + sprite.X, ty = sy + sprite.Y;
						const int density = std::clamp(static_cast<int>(cell.OverlayStage) + 1, 1, 12);
						const int pattern = ((tx / 6) * 7 + (ty / 4) * 11 + cell.X * 3 + cell.Y * 5) % 12;
						const bool nugget = tx % 6 >= 1 && tx % 6 <= 4 && ty % 4 >= 1 && ty % 4 <= 2;
						color = Blend(color, overlayColor, 80);
						if (nugget && pattern < density)
							color = ty % 4 == 1 ? Blend(overlayColor, Pack({ 255, 255, 220 }), 75)
								: Blend(overlayColor, Pack({ 48, 38, 16 }), 100);
					}
					else color = overlayColor;
				}
				g_Base[dst] = color;
				g_Owner[dst] = static_cast<uint32_t>(cellIndex);
			}
		}
	}

	void ComposeRows(int budgetRows)
	{
		FullMapPerf::Scope timing(FullMapPerf::Stage::Compose);
		const int begin = g_Composition.Row;
		const int end = std::min(FullMapTerrain::CanvasH, begin + std::max(1, budgetRows));
		uint64_t pixels = 0;
		const auto* base = g_Base.data();
		const auto* owners = g_Owner.data();
		const auto* ore = g_OrePixels.data();
		const auto* uniformFog = g_UniformFog.data();
		const auto* tableIndices = g_LightingTableIndex.data();
		auto* output = g_Work.data();
		for (int y = begin; y < end; ++y)
		{
			const size_t tileRow = static_cast<size_t>(y / ComposeTileSize) * g_TileColumns;
			for (int tx = 0; tx < g_TileColumns; ++tx)
			{
				if (!g_ComposeTiles[tileRow + tx]) continue;
				const int left = tx * ComposeTileSize;
				const int right = std::min(left + ComposeTileSize, FullMapTerrain::CanvasW);
				pixels += right - left;
				for (int x = left; x < right; ++x)
				{
					const size_t index = static_cast<size_t>(y) * FullMapTerrain::CanvasW + x;
					const uint32_t owner = owners[index];
					if (owner == NoCell)
					{
						output[index] = base[index];
						continue;
					}
					uint16_t fog = uniformFog[owner];
					if (fog == 0)
					{
						output[index] = 0;
						continue;
					}
					// Minerals stay independent of environmental lighting. Other
					// pixels use an exact channel lookup shared by equal samples.
					uint16_t litBase = base[index];
					if (!ore[index])
					{
						const uint32_t tableIndex = tableIndices[owner];
						if (tableIndex == NoCell - 1)
							litBase = ApplyLighting(litBase, g_ComposeLighting[owner]);
						else if (tableIndex != NoCell)
						{
							const auto rgb = Drawing::Int_To_RGB(litBase);
							const auto& table = g_LightingTables[tableIndex];
							litBase = table.Red[rgb.R] | table.Green[rgb.G] | table.Blue[rgb.B];
						}
					}
					if (fog == MixedFog)
					{
						const Cell& cell = g_Cells[owner];
						const auto visualFog = [](uint8_t value) -> uint8_t
						{
							return value == 40 ? g_ComposeShroudBrightness : value;
						};
						const uint8_t centerFog = visualFog(g_ComposeFog[cell.Y * 512 + cell.X]);
						const auto world = g_Projection.ToNative(Point { x + 0.5, y + 0.5 });
						const auto position = NativeToCell(world.X, world.Y, cell.Level);
						const double fx = std::clamp(position.X, cell.X - 0.5, cell.X + 0.5);
						const double fy = std::clamp(position.Y, cell.Y - 0.5, cell.Y + 0.5);
						fog = SmoothFog(fx, fy, [&](int cx, int cy)
						{
							if (cx < 0 || cy < 0 || cx >= 512 || cy >= 512 || g_CellIndex[cy * 512 + cx] < 0)
								return centerFog;
							return visualFog(g_ComposeFog[cy * 512 + cx]);
						});
					}
					if (fog == 255)
						output[index] = litBase;
					else
					{
						const auto rgb = Drawing::Int_To_RGB(litBase);
						output[index] = static_cast<uint16_t>(Drawing::RGB_To_Int(
							rgb.R * fog / 255, rgb.G * fog / 255, rgb.B * fog / 255));
					}
				}
			}
		}
		g_ComposedPixels += pixels;
		FullMapPerf::AddWork(FullMapPerf::Work::ComposePixels, pixels);
		if (g_Composition.Advance(end - begin, FullMapTerrain::CanvasH))
		{
			g_View.swap(g_Work);
			SyncCompositionTiles();
			++g_Revision;
			FullMapPerf::AddWork(FullMapPerf::Work::CompletedImages, 1);
			g_Ready = true;
			g_Building = false;
		}
	}
}

int FullMapTerrain::CanvasW = 0;
int FullMapTerrain::CanvasH = 0;
bool FullMapTerrain::IsReady() { return g_Ready; }
bool FullMapTerrain::IsBuilding() { return g_Building; }
const uint16_t* FullMapTerrain::Pixels() { return g_Ready ? g_View.data() : nullptr; }
uint64_t FullMapTerrain::Revision() { return g_Revision; }
RectangleStruct FullMapTerrain::PlayableBounds()
{
	// FA2sp's blue border and LocalSize use projected map coordinates, not a
	// CellStruct X/Y rectangle. Query the game's live camera clamp instead of
	// enumerating usable cells or inferring bounds from TMP art/elevation.
	const auto* tactical = TacticalClass::Instance;
	if (!tactical) return { 0, 0, CanvasW, CanvasH };
	const auto native = tactical->GetViewableBounds();
	if (native.Width <= 0 || native.Height <= 0) return {};
	const auto result = g_Projection.ToCanvas(Rect { native.X, native.Y, native.Width, native.Height });
	return { result.X, result.Y, result.Width, result.Height };
}

bool FullMapTerrain::IsCompatibleWithCurrentMap()
{
	FullMapPerf::Scope timing(FullMapPerf::Stage::CacheValidation);
	const auto& map = MapClass::Instance;
	if (!g_Ready || g_Cells.empty() || !map.Cells.Items)
		return false;
	if (g_MapRect.X != map.MapRect.X || g_MapRect.Y != map.MapRect.Y
		|| g_MapRect.Width != map.MapRect.Width || g_MapRect.Height != map.MapRect.Height
		|| g_MapCellsBase != reinterpret_cast<uintptr_t>(map.Cells.Items)
		|| g_MapCellsCapacity != map.Cells.Capacity)
		return false;

	for (const auto& cell : g_Cells)
	{
		if (!cell.Source || !map.CoordinatesLegal(cell.Source->MapCoords)
			|| cell.Source->MapCoords.X != cell.X || cell.Source->MapCoords.Y != cell.Y
			|| static_cast<int>(cell.Source->Level) != cell.Level
			|| cell.Source->IsoTileTypeIndex != cell.TileTypeIndex
			|| static_cast<uint8_t>(cell.Source->Height) != cell.TileHeight
			|| GetSprite(GetTile(cell.Source)) != cell.Image)
			return false;
	}
	return true;
}

void FullMapTerrain::RefreshForOpen()
{
	if (!g_Ready || g_Cells.empty()) return;
	FullMapPerf::Scope timing(FullMapPerf::Stage::ReopenRefresh);
	// Discard an unfinished image, retaining every dirty tile it covered.
	// Work must again match the last published image before patching it.
	if (g_Composition.Active)
	{
		SyncCompositionTiles();
		for (size_t i = 0; i < g_DirtyTiles.size(); ++i)
			g_DirtyTiles[i] |= g_ComposeTiles[i];
		g_Composition.Active = false;
		g_Composition.Dirty = true;
	}
	g_FogCursor = 0;
	g_LastFogFrame = -1;
	RefreshFog(MapClass::MaxCells);
	const auto before = g_Revision;
	const auto pixelsBefore = g_ComposedPixels;
	// Still publish current fog before returning. Only tiles influenced by
	// changed fog/light/overlays are composed; no changes means no pixel work.
	PumpCompose(CanvasH);
	Debug::Log("[FullMap] reopen refresh: cells=%zu shroudedOrFogged=%d revision=%llu terrainRebuilt=0 imageUpdated=%d composePixels=%llu canvasPixels=%zu\n",
		g_Cells.size(), g_FoggedCells, g_Revision, before != g_Revision,
		g_ComposedPixels - pixelsBefore, g_Base.size());
}

void FullMapTerrain::Invalidate()
{
	++g_Revision;
	g_Ready = g_Building = false;
	CanvasW = CanvasH = 0;
	g_BuildCursor = g_FogCursor = 0;
	g_LastFogFrame = -1;
	g_FoggedCells = 0;
	g_ScanChanged = false;
	g_MissingTiles = 0;
	g_EdgeShroudFallbacks = 0;
	g_MapRect = {};
	g_MapCellsBase = 0;
	g_MapCellsCapacity = 0;
	g_Composition = {};
	g_ComposedPixels = 0;
	g_TileColumns = 0;
	g_DirtyTiles.clear(); g_ComposeTiles.clear(); g_UniformFog.clear();
	g_LightingTables.clear(); g_LightingTableIndex.clear();
	g_RequestedShroudBrightness = g_ComposeShroudBrightness = FullMapSettings::ShroudBrightness();
	g_Cells.clear();
	g_Sprites.clear();
	g_CellIndex.clear();
	g_Base.clear(); g_View.clear(); g_Work.clear(); g_Owner.clear(); g_OrePixels.clear();
	g_Fog.clear(); g_ComposeFog.clear();
	g_Lighting.clear(); g_ComposeLighting.clear();
}

bool FullMapTerrain::Build()
{
	FullMapPerf::Scope timing(FullMapPerf::Stage::Build);
	Invalidate();
	const auto& map = MapClass::Instance;
	if (map.MapRect.Width <= 0 || map.MapRect.Height <= 0 || !map.Cells.Items)
		return false;
	g_CellIndex.assign(MapClass::MaxCells, -1);
	// Scan allocated slots without touching the engine's shared cell iterator.
	// MapRect is an isometric width/height, not an X/Y iteration rectangle.
	const int capacity = std::min(MapClass::MaxCells, map.Cells.Capacity);
	for (int index = 0; index < capacity; ++index)
	{
		auto* pCell = map.Cells[index];
		if (!pCell || !map.CoordinatesLegal(pCell->MapCoords)) continue;
		const auto xy = pCell->MapCoords;
		if (xy.X < 0 || xy.X >= 512 || xy.Y < 0 || xy.Y >= 512 || xy.Y * 512 + xy.X != index)
			continue;
		g_Cells.push_back({ pCell, xy.X, xy.Y, static_cast<int>(pCell->Level),
			pCell->IsoTileTypeIndex, static_cast<uint8_t>(pCell->Height), GetSprite(GetTile(pCell)) });
	}
	if (g_Cells.empty()) return false;
	g_MapRect = map.MapRect;
	g_MapCellsBase = reinterpret_cast<uintptr_t>(map.Cells.Items);
	g_MapCellsCapacity = map.Cells.Capacity;
	std::sort(g_Cells.begin(), g_Cells.end(), [](const Cell& a, const Cell& b)
	{
		if (a.X + a.Y != b.X + b.Y) return a.X + a.Y < b.X + b.Y;
		return a.X < b.X;
	});

	int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
	g_Fog.assign(MapClass::MaxCells, 255);
	g_Lighting.assign(g_Cells.size(), LightingSample {});
	for (size_t i = 0; i < g_Cells.size(); ++i)
	{
		const Cell& cell = g_Cells[i];
		const int index = cell.Y * 512 + cell.X;
		g_CellIndex[index] = static_cast<int>(i);
		const auto fog = VisibilityOf(cell.Source);
		g_Fog[index] = fog;
		g_Cells[i].Lighting = CaptureLighting(cell.Source);
		g_Lighting[i] = g_Cells[i].Lighting;
		g_FoggedCells += fog != 255;
		const int x = 30 * (cell.X - cell.Y) - 30 + cell.Image->X;
		const int y = 15 * (cell.X + cell.Y - cell.Level) + cell.Image->Y;
		minX = std::min(minX, x); minY = std::min(minY, y);
		maxX = std::max(maxX, x + cell.Image->W); maxY = std::max(maxY, y + cell.Image->H);
	}
	g_Projection = { std::min({ 0.5, MaxDimension / static_cast<double>(maxX - minX),
		MaxDimension / static_cast<double>(maxY - minY) }), static_cast<double>(minX), static_cast<double>(minY) };
	CanvasW = std::max(1, static_cast<int>(std::ceil((maxX - minX) * g_Projection.Scale)));
	CanvasH = std::max(1, static_cast<int>(std::ceil((maxY - minY) * g_Projection.Scale)));
	const size_t count = static_cast<size_t>(CanvasW) * CanvasH;
	g_Base.assign(count, 0); g_View.assign(count, 0); g_Work.assign(count, 0);
	g_Owner.assign(count, NoCell);
	g_OrePixels.assign(count, 0);
	g_ComposeLighting = g_Lighting;
	g_TileColumns = (CanvasW + ComposeTileSize - 1) / ComposeTileSize;
	g_DirtyTiles.assign(static_cast<size_t>(g_TileColumns) * ((CanvasH + ComposeTileSize - 1) / ComposeTileSize), 1);
	g_ComposeTiles.assign(g_DirtyTiles.size(), 0);
	g_Building = true;
	FullMapPerf::AddWork(FullMapPerf::Work::BuiltCells, g_Cells.size());
	Debug::Log("[FullMap] TMP terrain: legal=%zu expected=%d sprites=%zu missing=%d native=(%d,%d %dx%d) canvas=%dx%d fog=%d edgeShroud=%d\n",
		g_Cells.size(), (map.MapRect.Width * 2 - 1) * map.MapRect.Height, g_Sprites.size(), g_MissingTiles,
		minX, minY, maxX - minX, maxY - minY, CanvasW, CanvasH, g_FoggedCells, g_EdgeShroudFallbacks);
	return true;
}

bool FullMapTerrain::BuildIncremental(int budgetCells)
{
	if (!g_Building || g_BuildCursor >= g_Cells.size()) return false;
	FullMapPerf::Scope timing(FullMapPerf::Stage::Raster);
	const size_t end = std::min(g_Cells.size(), g_BuildCursor + std::max(1, budgetCells));
	FullMapPerf::AddWork(FullMapPerf::Work::RasterCells, end - g_BuildCursor);
	while (g_BuildCursor < end) DrawCell(g_BuildCursor++);
	if (g_BuildCursor == g_Cells.size())
	{
		g_Composition.Dirty = true;
	}
	return true;
}

void FullMapTerrain::RefreshFog(int budgetCells)
{
	if (!g_Ready || g_Cells.empty() || g_LastFogFrame == Unsorted::CurrentFrame) return;
	FullMapPerf::Scope timing(FullMapPerf::Stage::Scan);
	g_LastFogFrame = Unsorted::CurrentFrame;
	const size_t end = std::min(g_Cells.size(), g_FogCursor + std::max(1, budgetCells));
	FullMapPerf::AddWork(FullMapPerf::Work::ScanCells, end - g_FogCursor);
	uint64_t lightChanges = 0, fogChanges = 0, overlayChanges = 0;
	while (g_FogCursor < end)
	{
		const size_t cellIndex = g_FogCursor++;
		Cell& cell = g_Cells[cellIndex];
		const auto lighting = CaptureLighting(cell.Source);
		if (cell.Lighting != lighting)
		{
			++lightChanges;
			cell.Lighting = lighting;
			g_Lighting[cellIndex] = lighting;
			DirtyCell(cellIndex);
			g_ScanChanged = true;
		}
		if (cell.OverlayIndex != cell.Source->OverlayTypeIndex || cell.OverlayStage != cell.Source->OverlayData)
		{
			++overlayChanges;
			DrawCell(cellIndex, true);
			DirtyCell(cellIndex);
			g_ScanChanged = true;
		}
		uint8_t& old = g_Fog[cell.Y * 512 + cell.X];
		const uint8_t value = VisibilityOf(cell.Source);
		if (old != value)
		{
			++fogChanges;
			g_FoggedCells += (value != 255) - (old != 255);
			old = value;
			DirtyCell(cellIndex, true);
			g_ScanChanged = true;
		}
	}
	FullMapPerf::AddWork(FullMapPerf::Work::LightChanges, lightChanges);
	FullMapPerf::AddWork(FullMapPerf::Work::FogChanges, fogChanges);
	FullMapPerf::AddWork(FullMapPerf::Work::OverlayChanges, overlayChanges);
	if (g_FogCursor == g_Cells.size())
	{
		g_FogCursor = 0;
		g_Composition.Dirty |= g_ScanChanged;
		g_ScanChanged = false;
	}
}

void FullMapTerrain::PumpCompose(int budgetRows)
{
	if (g_Cells.empty() || g_BuildCursor < g_Cells.size()) return;
	const auto brightness = FullMapSettings::ShroudBrightness();
	if (brightness != g_RequestedShroudBrightness)
	{
		g_RequestedShroudBrightness = brightness;
		std::fill(g_DirtyTiles.begin(), g_DirtyTiles.end(), uint8_t { 1 });
		g_Composition.Dirty = true;
	}
	if (g_Composition.Begin())
	{
		g_ComposeTiles = g_DirtyTiles;
		// A forced invalidation also has to work without a preceding scan.
		if (std::none_of(g_ComposeTiles.begin(), g_ComposeTiles.end(), [](uint8_t dirty) { return dirty != 0; }))
			std::fill(g_ComposeTiles.begin(), g_ComposeTiles.end(), uint8_t { 1 });
		std::fill(g_DirtyTiles.begin(), g_DirtyTiles.end(), uint8_t { 0 });
		FullMapPerf::AddWork(FullMapPerf::Work::ComposeTiles,
			std::count(g_ComposeTiles.begin(), g_ComposeTiles.end(), uint8_t { 1 }));
		g_ComposeFog = g_Fog;
		g_ComposeLighting = g_Lighting;
		g_ComposeShroudBrightness = brightness;
		CaptureCompositionFog();
		CaptureCompositionLighting();
	}
	if (g_Composition.Active) ComposeRows(budgetRows);
}

void FullMapTerrain::CellToCanvas(int cellX, int cellY, int& px, int& py)
{
	double level = 0.0;
	if (cellX >= 0 && cellX < 512 && cellY >= 0 && cellY < 512 && !g_CellIndex.empty())
	{
		const int i = g_CellIndex[cellY * 512 + cellX];
		if (i >= 0) level = g_Cells[i].Level;
	}
	const auto p = g_Projection.ToCanvas(CellCenter(cellX, cellY, level));
	px = static_cast<int>(std::lround(p.X)); py = static_cast<int>(std::lround(p.Y));
}

bool FullMapTerrain::CanvasToCell(int px, int py, short& cellX, short& cellY)
{
	if (!g_Ready || px < 0 || py < 0 || px >= CanvasW || py >= CanvasH) return false;
	const uint32_t index = g_Owner[static_cast<size_t>(py) * CanvasW + px];
	if (index == NoCell) return false;
	cellX = static_cast<short>(g_Cells[index].X); cellY = static_cast<short>(g_Cells[index].Y);
	return true;
}

void FullMapTerrain::TacticalRectToCanvas(const RectangleStruct& source, RectangleStruct& canvas)
{
	const auto rect = g_Projection.ToCanvas(Rect { source.X, source.Y, source.Width, source.Height });
	canvas = { rect.X, rect.Y, rect.Width, rect.Height };
}

void FullMapTerrain::TacticalPointToCanvas(double x, double y, int& px, int& py)
{
	const auto p = g_Projection.ToCanvas(Point { x, y });
	px = static_cast<int>(std::lround(p.X));
	py = static_cast<int>(std::lround(p.Y));
}

uint8_t FullMapTerrain::VisibilityAtCell(int x, int y)
{
	if (!g_Ready || x < 0 || y < 0 || x >= 512 || y >= 512 || g_CellIndex[y * 512 + x] < 0)
		return 40;
	// Dynamic objects/tooltips need current visibility, independently of the
	// budgeted background fog compositor.
	return VisibilityOf(g_Cells[g_CellIndex[y * 512 + x]].Source);
}
