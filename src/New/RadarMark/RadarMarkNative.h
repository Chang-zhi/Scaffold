#pragma once

#include <GeneralStructures.h>
#include <GeneralDefinitions.h>
#include <vector>

class AnimClass;
class AnimTypeClass;
class HouseClass;
class Surface;
class TechnoClass;
class REGISTERS;
class RadarArt;
class ConvertClass;
struct SHPStruct;

namespace RadarMarkNative
{
	// Proxies exist only during a radar/FullMap drawing scope; none are serialized.
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
		HouseClass* owner, RadarArt const& art, std::vector<RectangleStruct>& rectangles, double scale = 1.0);

	// Same SHP renderer for native animation forwarding and explicit SHP marks.
	// Scaling samples the actual background before native blending, preserving
	// transparent pixels, palette/remap, tint and shadows without a color key.
	bool DrawShape(Surface* surface, RectangleStruct const& clip, Point2D position,
		ConvertClass* palette, SHPStruct* image, int frame, BlitterFlags flags,
		int remap, int brightness, int tint, double scale, Point2D pivot);

	void ClearScaleSurface();

	// Called exclusively by the scoped host hooks in Hooks.cpp.
	DWORD ConstructVisual(AnimClass* anim);
	DWORD DestructVisual(AnimClass* anim);
	DWORD RedirectShape();
}
