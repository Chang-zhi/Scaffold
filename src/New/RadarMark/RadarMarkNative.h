#pragma once

#include <GeneralStructures.h>
#include <vector>

class AnimClass;
class AnimTypeClass;
class HouseClass;
class Surface;
class TechnoClass;
class REGISTERS;
class RadarArt;

namespace RadarMarkNative
{
	// Proxies exist only during RadarClass::Render; no native objects are serialized.
	class FrameScope
	{
	public:
		FrameScope();
		~FrameScope();
		FrameScope(FrameScope const&) = delete;
		FrameScope& operator=(FrameScope const&) = delete;
	};

	bool Draw(Surface* surface, RectangleStruct const& clip, Point2D position,
		CoordStruct world, AnimTypeClass* type, int frame, TechnoClass* target,
		HouseClass* owner, RadarArt const& art, std::vector<RectangleStruct>& rectangles);

	// Called exclusively by the scoped host hooks in Hooks.cpp.
	DWORD ConstructVisual(AnimClass* anim);
	DWORD DestructVisual(AnimClass* anim);
	DWORD RedirectShape();
}
