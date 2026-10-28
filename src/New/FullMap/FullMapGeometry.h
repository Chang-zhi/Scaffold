#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Game-independent parts of the FullMap renderer. Coordinates are native
// tactical pixels (60 x 30 per cell), before fitting the canvas on screen.
namespace FullMapGeometry
{
	struct Point { double X, Y; };
	struct Rect { int X, Y, Width, Height; };

	inline Rect Intersect(Rect a, Rect b)
	{
		const int left = std::max(a.X, b.X), top = std::max(a.Y, b.Y);
		const int right = std::min(a.X + a.Width, b.X + b.Width);
		const int bottom = std::min(a.Y + a.Height, b.Y + b.Height);
		return { left, top, std::max(0, right - left), std::max(0, bottom - top) };
	}

	// The image stays fitted to the full source. Only its allowed display area
	// is clipped; destination offsets include letterboxing, zoom and panning.
	inline Rect MapDisplayClip(Rect area, Rect destination, Rect source, Rect allowed)
	{
		if (source.Width <= 0 || source.Height <= 0 || allowed.Width <= 0 || allowed.Height <= 0)
			return { area.X, area.Y, 0, 0 };
		const auto x = [&](int value) { return destination.X
			+ (value - source.X) * static_cast<double>(destination.Width) / source.Width; };
		const auto y = [&](int value) { return destination.Y
			+ (value - source.Y) * static_cast<double>(destination.Height) / source.Height; };
		const int left = static_cast<int>(std::ceil(x(allowed.X)));
		const int top = static_cast<int>(std::ceil(y(allowed.Y)));
		const int right = static_cast<int>(std::floor(x(allowed.X + allowed.Width)));
		const int bottom = static_cast<int>(std::floor(y(allowed.Y + allowed.Height)));
		return Intersect(area, { left, top, std::max(0, right - left), std::max(0, bottom - top) });
	}

	inline Rect ScaleAround(Rect previous, int width, int height, Point anchor)
	{
		const double x = (anchor.X - previous.X) / std::max(1, previous.Width);
		const double y = (anchor.Y - previous.Y) / std::max(1, previous.Height);
		return { static_cast<int>(std::lround(anchor.X - x * width)),
			static_cast<int>(std::lround(anchor.Y - y * height)), width, height };
	}

	enum class DragMode { None, PanMap, MoveCamera };

	inline DragMode MapDragAt(Point mouse, Rect clip, Rect camera, bool allowPan)
	{
		const auto inside = [&](Rect r) { return mouse.X >= r.X && mouse.Y >= r.Y
			&& mouse.X < r.X + r.Width && mouse.Y < r.Y + r.Height; };
		if (!inside(clip)) return DragMode::None;
		constexpr int edge = 5;
		const bool onFrame = camera.Width > 0 && camera.Height > 0
			&& inside({ camera.X - edge, camera.Y - edge, camera.Width + 2 * edge, camera.Height + 2 * edge })
			&& (std::abs(mouse.X - camera.X) <= edge || std::abs(mouse.Y - camera.Y) <= edge
				|| std::abs(mouse.X - (camera.X + camera.Width - 1)) <= edge
				|| std::abs(mouse.Y - (camera.Y + camera.Height - 1)) <= edge);
		if (onFrame) return DragMode::MoveCamera;
		if (allowPan) return DragMode::PanMap;
		return inside(camera) ? DragMode::MoveCamera : DragMode::None;
	}

	inline bool LegalCell(int width, int height, int x, int y)
	{
		// MapClass::CoordinatesLegal, gamemd 1.001 @ 0x568300.
		return x >= 0 && x < 512 && y >= 0 && y < 512
			&& x + y > width && x + y <= width + 2 * height
			&& x - y < width && y - x < width;
	}

	inline Point CellCenter(double x, double y, double level = 0.0)
	{
		return { 30.0 * (x - y), 15.0 * (x + y + 1.0 - level) };
	}

	inline Point NativeToCell(double x, double y, double level = 0.0)
	{
		const double difference = x / 30.0;
		const double sum = y / 15.0 - 1.0 + level;
		return { (sum + difference) * 0.5, (sum - difference) * 0.5 };
	}

	struct Projection
	{
		double Scale = 1.0;
		double Left = 0.0;
		double Top = 0.0;

		Point ToCanvas(Point p) const { return { (p.X - Left) * Scale, (p.Y - Top) * Scale }; }
		Point ToNative(Point p) const { return { p.X / Scale + Left, p.Y / Scale + Top }; }

		Rect ToCanvas(Rect r) const
		{
			const auto a = ToCanvas(Point { static_cast<double>(r.X), static_cast<double>(r.Y) });
			const auto b = ToCanvas(Point { static_cast<double>(r.X + r.Width), static_cast<double>(r.Y + r.Height) });
			const int left = static_cast<int>(std::floor(a.X));
			const int top = static_cast<int>(std::floor(a.Y));
			return { left, top, static_cast<int>(std::ceil(b.X)) - left,
				static_cast<int>(std::ceil(b.Y)) - top };
		}
	};

#pragma pack(push, 1)
	struct RGB { uint8_t R, G, B; };
	struct TileFileHeader
	{
		int32_t Width, Height, TileWidth, TileHeight;
	};
	struct TileImageHeader
	{
		int32_t X, Y;
		uint32_t ExtraOffset, ZOffset, ExtraZOffset;
		int32_t ExtraX, ExtraY;
		uint32_t ExtraWidth, ExtraHeight, Flags;
		uint8_t Height, Terrain, Ramp;
		RGB RadarLeft, RadarRight;
		uint8_t Padding[3];
	};
#pragma pack(pop)
	static_assert(sizeof(TileFileHeader) == 16);
	static_assert(sizeof(TileImageHeader) == 52);
	static_assert(offsetof(TileImageHeader, Ramp) == 42);
	static_assert(offsetof(TileImageHeader, RadarLeft) == 43);
	static_assert(offsetof(TileImageHeader, RadarRight) == 46);

	// TMP data contains a packed 60x30 diamond followed by optional cliff/ramp
	// graphics. These graphics already encode the game's complete ramp family;
	// treating Ramp as four repeated directions discards corner/double ramps.
	inline int DiamondLeft(int row) { return std::abs(14 - row) * 2; }
	inline int DiamondWidth(int row) { return 60 - DiamondLeft(row) * 2; }

	template<typename Emit>
	void DecodeDiamond(const uint8_t* pixels, Emit emit)
	{
		int offset = 0;
		for (int y = 0; y < 30; ++y)
		{
			const int left = DiamondLeft(y);
			const int width = DiamondWidth(y);
			for (int x = 0; x < width; ++x)
				emit(left + x, y, pixels[offset++]);
		}
	}

	template<typename Sample>
	uint8_t SmoothFog(double x, double y, Sample sample)
	{
		const int x0 = static_cast<int>(std::floor(x));
		const int y0 = static_cast<int>(std::floor(y));
		// Smoothstep has zero slope at cell centers, so a revealed cell does not
		// acquire a hard diamond outline at a partially revealed neighbour.
		double tx = x - x0;
		double ty = y - y0;
		tx = tx * tx * (3.0 - 2.0 * tx);
		ty = ty * ty * (3.0 - 2.0 * ty);
		const double a = sample(x0, y0) * (1.0 - tx) + sample(x0 + 1, y0) * tx;
		const double b = sample(x0, y0 + 1) * (1.0 - tx) + sample(x0 + 1, y0 + 1) * tx;
		return static_cast<uint8_t>(std::clamp(static_cast<int>(a * (1.0 - ty) + b * ty + 0.5), 0, 255));
	}

	// Keep displaying a complete previous image while preparing a new fog
	// image. Changes arriving during composition request a subsequent pass;
	// they must never rewind the active pass to row zero.
	struct Composition
	{
		int Row = 0;
		bool Active = false;
		bool Dirty = false;

		bool Begin()
		{
			if (Active || !Dirty)
				return false;
			Row = 0;
			Active = true;
			Dirty = false;
			return true;
		}

		bool Advance(int rows, int height)
		{
			Row = std::min(height, Row + std::max(1, rows));
			if (Row < height)
				return false;
			Active = false;
			return true;
		}
	};
}
