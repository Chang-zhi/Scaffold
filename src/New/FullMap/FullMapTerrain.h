#pragma once

#include <GeneralStructures.h>
#include <cstdint>

// 高清地形画布：使用原始 TMP 的形状、左右雷达色以及战术层 60x30 投影。
// 迷雾单独采样并在完整合成后换帧，不从侧栏雷达位图获取地形颜色。
class FullMapTerrain
{
public:
	static int CanvasW;
	static int CanvasH;

	static bool Build();
	static bool BuildIncremental(int budgetCells);
	static void RefreshFog(int budgetCells);
	static void PumpCompose(int budgetRows);
	static bool IsReady();
	static bool IsBuilding();
	// True when the cached static TMP canvas belongs to the current map.
	// Overlay, fog and lighting changes are refreshed incrementally.
	static bool IsCompatibleWithCurrentMap();
	// Re-read all fog/light/overlays before reopening; patch changed regions atomically, retaining static TMP.
	static void RefreshForOpen();
	static void Invalidate();
	static const uint16_t* Pixels();
	static uint64_t Revision();
	// Current viewable area from the live native camera clamp. Coordinates
	// are canvas pixels; no trigger-specific cache or terrain rebuild needed.
	static RectangleStruct PlayableBounds();
	static uint8_t VisibilityAtCell(int x, int y);
	static void TacticalPointToCanvas(double x, double y, int& px, int& py);
	static void CellToCanvas(int cellX, int cellY, int& px, int& py);
	// False for the margin/outside the actual rendered terrain.
	static bool CanvasToCell(int px, int py, short& cellX, short& cellY);
	// Native tactical pixels, including terrain elevation (not radar pixels).
	static void TacticalRectToCanvas(const RectangleStruct& source, RectangleStruct& canvas);
};
