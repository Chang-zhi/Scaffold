#pragma once

#include <GeneralStructures.h>

// Dynamic, locally visible world information on top of the terrain canvas.
class FullMapObjects
{
public:
	struct View
	{
		RectangleStruct Map;
		RectangleStruct Clip;
		int SourceX, SourceY;
		int SourceW, SourceH;
		Point2D Mouse;
		float Zoom;
	};

	static void Draw(const View& view);
	static void Invalidate();
};
