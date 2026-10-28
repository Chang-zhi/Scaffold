#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace RadarMarkScaling
{
    struct Rect { int X, Y, Width, Height; };
    struct Pixels { uint16_t* Data; int Width, Height, Pitch; };

    inline Rect Intersect(Rect a, Rect b)
    {
        const int x = std::max(a.X, b.X), y = std::max(a.Y, b.Y);
        return { x, y, std::max(0, std::min(a.X + a.Width, b.X + b.Width) - x),
            std::max(0, std::min(a.Y + a.Height, b.Y + b.Height) - y) };
    }

    inline double LimitScale(double scale, int width, int height)
    {
        return std::min(scale, 192.0 / std::max({ 1, width, height }));
    }

    inline Rect ScaleBounds(Rect source, int anchorX, int anchorY, double scale)
    {
        const int x = static_cast<int>(std::floor(anchorX + (source.X - anchorX) * scale));
        const int y = static_cast<int>(std::floor(anchorY + (source.Y - anchorY) * scale));
        return { x, y,
            static_cast<int>(std::ceil(anchorX + (source.X + source.Width - anchorX) * scale)) - x,
            static_cast<int>(std::ceil(anchorY + (source.Y + source.Height - anchorY) * scale)) - y };
    }

    // Capture the existing target under a sprite before its native translucent
    // rendering. There is no reserved transparent color: black is valid art.
    inline void Capture(Pixels target, Rect clip, Rect destination, Pixels scratch)
    {
        if (!target.Data || !scratch.Data || scratch.Width <= 0 || scratch.Height <= 0
            || destination.Width <= 0 || destination.Height <= 0) return;
        clip = Intersect(clip, { 0, 0, target.Width, target.Height });
        for (int y = 0; y < scratch.Height; ++y)
        {
            const int dy = destination.Y + y * destination.Height / scratch.Height;
            for (int x = 0; x < scratch.Width; ++x)
            {
                const int dx = destination.X + x * destination.Width / scratch.Width;
                scratch.Data[static_cast<ptrdiff_t>(y) * scratch.Pitch + x] =
                    dx >= clip.X && dy >= clip.Y && dx < clip.X + clip.Width && dy < clip.Y + clip.Height
                    ? target.Data[static_cast<ptrdiff_t>(dy) * target.Pitch + dx] : uint16_t {};
            }
        }
    }

    inline void FindCoverage(Pixels rendered, const uint16_t* background,
        const uint16_t* contrast, uint8_t* coverage)
    {
        for (int y = 0; y < rendered.Height; ++y)
            for (int x = 0; x < rendered.Width; ++x)
            {
                const auto pixel = static_cast<size_t>(y) * rendered.Width + x;
                // A second draw over the inverse background distinguishes a
                // real black/color-matching sprite pixel from transparency.
                coverage[pixel] = rendered.Data[static_cast<ptrdiff_t>(y) * rendered.Pitch + x] != background[pixel]
                    || contrast[pixel] != static_cast<uint16_t>(~background[pixel]);
            }
    }

    inline void Composite(Pixels target, Rect clip, Rect destination, Pixels rendered, const uint8_t* coverage)
    {
        if (!target.Data || !rendered.Data || !coverage || rendered.Width <= 0 || rendered.Height <= 0
            || destination.Width <= 0 || destination.Height <= 0) return;
        clip = Intersect(Intersect(clip, destination), { 0, 0, target.Width, target.Height });
        for (int y = clip.Y; y < clip.Y + clip.Height; ++y)
        {
            const int sy = (y - destination.Y) * rendered.Height / destination.Height;
            for (int x = clip.X; x < clip.X + clip.Width; ++x)
            {
                const int sx = (x - destination.X) * rendered.Width / destination.Width;
                const auto offset = static_cast<ptrdiff_t>(sy) * rendered.Pitch + sx;
                const uint16_t color = rendered.Data[offset];
                // Skip all transparent samples, retaining terrain detail even
                // when each source pixel expands to several target pixels.
                if (coverage[static_cast<size_t>(sy) * rendered.Width + sx])
                    target.Data[static_cast<ptrdiff_t>(y) * target.Pitch + x] = color;
            }
        }
    }
}
