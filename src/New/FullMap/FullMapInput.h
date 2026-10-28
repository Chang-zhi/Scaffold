#pragma once

#include "FullMapGeometry.h"
#include <cstdint>

namespace FullMapInput
{
    using FullMapGeometry::Point;
    using FullMapGeometry::Rect;

    // RadarClass::GetMouseAction @0x653A39: only the X coordinate is shifted.
    inline Point RadarPoint(Point client, bool rightSidebar, int tacticalWidth)
    {
        if (rightSidebar) client.X -= tacticalWidth;
        return client;
    }

    inline bool Inside(Point point, Rect bounds)
    {
        return bounds.Width > 0 && bounds.Height > 0
            && point.X >= bounds.X && point.X < bounds.X + bounds.Width
            && point.Y >= bounds.Y && point.Y < bounds.Y + bounds.Height;
    }

    struct MinimapDoubleClick
    {
        bool Down = false;
        bool Pending = false;
        uint32_t LastTick = 0;
        Point LastPosition {};

        void Reset(bool down = false)
        {
            Down = down;
            Pending = false;
            LastTick = 0;
            LastPosition = {};
        }

        bool Update(bool down, Point position, Rect bounds, uint32_t tick)
        {
            const bool pressed = down && !Down;
            Down = down;
            if (!pressed) return false;
            if (!Inside(position, bounds))
            {
                Pending = false;
                return false;
            }
            const double dx = position.X - LastPosition.X;
            const double dy = position.Y - LastPosition.Y;
            const bool doubleClick = Pending && tick - LastTick < 400u
                && dx * dx + dy * dy < 12.0 * 12.0;
            Pending = !doubleClick;
            LastTick = tick;
            LastPosition = position;
            return doubleClick;
        }
    };
}
