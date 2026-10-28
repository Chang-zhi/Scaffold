#pragma once

#include "FullMapGeometry.h"
#include "FullMapSettings.h"
#include <array>
#include <cstdint>
#include <cstdlib>

namespace FullMapIcons
{
	using FullMapGeometry::Point;
	using FullMapGeometry::Rect;
	enum class Kind { Building, Infantry, Vehicle, Ship, Aircraft, Signal, OreSpawner, TechBuilding };
	enum class Visibility { Hidden, Signal, Known };

	inline Visibility ObjectVisibility(bool active, bool invisible, bool own,
		bool discovered, uint8_t terrainVisibility, bool radarVisible, bool signal)
	{
		if (!active || invisible) return Visibility::Hidden;
		if (own) return Visibility::Known;
		if (terrainVisibility != 255 || !radarVisible) return Visibility::Hidden;
		if (signal) return Visibility::Signal;
		return discovered ? Visibility::Known : Visibility::Hidden;
	}

	inline int Radius(Kind kind, float zoom, int screenWidth)
	{
		const int base = kind == Kind::Infantry ? 3 : kind == Kind::Building ? 5
			: kind == Kind::OreSpawner || kind == Kind::TechBuilding ? 6 : 4;
		return std::clamp(static_cast<int>(std::lround((base + (screenWidth >= 1600 ? 1 : 0))
			* FullMapSettings::MarkerScale(zoom))), 2, 24);
	}

	struct Colors
	{
		uint16_t Owner, Dark, Light, Health, Empty;
	};

	struct Canvas
	{
		uint16_t* Pixels;
		int Width, Height, Pitch; // pitch is signed, in WORDs
		Rect Clip;

		bool Contains(int x, int y) const
		{
			return x >= 0 && y >= 0 && x < Width && y < Height
				&& x >= Clip.X && y >= Clip.Y && x < Clip.X + Clip.Width && y < Clip.Y + Clip.Height;
		}
		void Put(int x, int y, uint16_t color)
		{
			if (Contains(x, y)) Pixels[static_cast<ptrdiff_t>(y) * Pitch + x] = color;
		}
		void Box(int x, int y, int w, int h, uint16_t color)
		{
			for (int py = std::max({ y, Clip.Y, 0 }); py < std::min({ y + h, Clip.Y + Clip.Height, Height }); ++py)
				for (int px = std::max({ x, Clip.X, 0 }); px < std::min({ x + w, Clip.X + Clip.Width, Width }); ++px)
					Put(px, py, color);
		}
		void Disc(int x, int y, int radius, uint16_t color)
		{
			for (int dy = -radius; dy <= radius; ++dy)
				for (int dx = -radius; dx <= radius; ++dx)
					if (dx * dx + dy * dy <= radius * radius) Put(x + dx, y + dy, color);
		}
		void Line(int x0, int y0, int x1, int y1, uint16_t color, int thickness = 1)
		{
			// Map/camera clipping keeps endpoints bounded to the fitted canvas.
			const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
			const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
			int error = dx + dy;
			for (;;)
			{
				Box(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
				if (x0 == x1 && y0 == y1) break;
				const int doubled = error * 2;
				if (doubled >= dy) { error += dy; x0 += sx; }
				if (doubled <= dx) { error += dx; y0 += sy; }
			}
		}
		void Diamond(int x, int y, int rx, int ry, uint16_t color)
		{
			for (int dy = -ry; dy <= ry; ++dy)
			{
				const int span = rx * (ry - std::abs(dy)) / std::max(1, ry);
				Box(x - span, y + dy, 2 * span + 1, 1, color);
			}
		}
	};

	inline void Draw(Canvas& canvas, Kind kind, int x, int y, int r, Colors colors, bool selected)
	{
		if (selected)
		{
			canvas.Diamond(x, y, r + 5, r + 5, colors.Dark);
			canvas.Diamond(x, y, r + 4, r + 4, colors.Light);
			canvas.Diamond(x, y, r + 2, r + 2, colors.Dark);
		}
		switch (kind)
		{
		case Kind::OreSpawner:
			canvas.Diamond(x, y, r + 3, r + 2, colors.Dark);
			canvas.Line(x - r / 2, y + r / 2, x - r / 3, y - r / 2, colors.Owner, 3);
			canvas.Line(x - r / 3, y - r / 2, x, y + r / 2, colors.Light, 2);
			canvas.Line(x, y + r / 2, x + r / 3, y - r, colors.Owner, 3);
			canvas.Line(x + r / 3, y - r, x + r / 2, y + r / 2, colors.Light, 2);
			canvas.Line(x - r, y + r, x + r, y + r, colors.Owner, 2);
			break;
		case Kind::TechBuilding:
		{
			canvas.Diamond(x, y, r + 3, r + 3, colors.Dark);
			canvas.Diamond(x, y, r + 1, r + 1, colors.Owner);
			canvas.Diamond(x, y, std::max(1, r - 1), std::max(1, r - 1), colors.Dark);
			// 科技建筑使用黄色中心十字，和灰色建筑群快速区分。
			constexpr uint16_t techYellow = static_cast<uint16_t>((31u << 11) | (52u << 5) | 4u);
			canvas.Line(x, y - r / 2, x, y + r / 2, colors.Dark, 4);
			canvas.Line(x - r / 2, y, x + r / 2, y, colors.Dark, 4);
			canvas.Line(x, y - r / 2, x, y + r / 2, techYellow, 2);
			canvas.Line(x - r / 2, y, x + r / 2, y, techYellow, 2);
			break;
		}
		case Kind::Building:
			canvas.Disc(x, y, r + 2, colors.Dark);
			canvas.Box(x - r + 1, y - 1, r * 2 - 1, r, colors.Owner);
			canvas.Line(x - r, y - 1, x, y - r, colors.Owner, 2);
			canvas.Line(x, y - r, x + r, y - 1, colors.Owner, 2);
			canvas.Box(x - 1, y + 1, 2, r - 1, colors.Light);
			break;
		case Kind::Infantry:
			canvas.Disc(x, y, r + 1, colors.Dark);
			canvas.Disc(x, y, std::max(2, r - 1), colors.Owner);
			canvas.Put(x, y - 1, colors.Light);
			break;
		case Kind::Vehicle:
			canvas.Box(x - r - 2, y - r + 1, 2 * r + 5, 2 * r - 1, colors.Dark);
			canvas.Box(x - r, y - r + 2, 2 * r + 1, 2 * r - 3, colors.Owner);
			canvas.Box(x - 1, y - 2, 3, 3, colors.Light);
			canvas.Line(x + 1, y - 1, x + r + 1, y - r, colors.Dark, 3);
			canvas.Line(x + 1, y - 1, x + r + 1, y - r, colors.Light);
			break;
		case Kind::Ship:
			canvas.Diamond(x, y, r + 3, r, colors.Dark);
			canvas.Diamond(x, y, r + 1, std::max(1, r - 2), colors.Owner);
			canvas.Line(x - 2, y, x + 2, y, colors.Light);
			break;
		case Kind::Aircraft:
			canvas.Line(x - r - 1, y + 1, x, y - r - 1, colors.Dark, 4);
			canvas.Line(x, y - r - 1, x + r + 1, y + 1, colors.Dark, 4);
			canvas.Line(x, y - r, x, y + r, colors.Dark, 4);
			canvas.Line(x - r, y, x, y - r, colors.Owner, 2);
			canvas.Line(x, y - r, x + r, y, colors.Owner, 2);
			canvas.Line(x, y - r, x, y + r, colors.Light);
			break;
		case Kind::Signal:
			canvas.Diamond(x, y, r + 2, r + 2, colors.Dark);
			canvas.Diamond(x, y, r, r, colors.Owner);
			canvas.Diamond(x, y, r - 2, r - 2, colors.Dark);
			break;
		}
	}

	inline void HealthBar(Canvas& canvas, int x, int y, int width, int health, int maximum, Colors colors)
	{
		const int fill = static_cast<int>(static_cast<int64_t>(std::clamp(health, 0, std::max(1, maximum)))
			* width / std::max(1, maximum));
		canvas.Box(x - 1, y - 1, width + 2, 5, colors.Dark);
		canvas.Box(x, y, width, 3, colors.Empty);
		canvas.Box(x, y, fill, 3, colors.Health);
	}
}
