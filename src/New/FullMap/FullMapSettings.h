#pragma once

#include <algorithm>
#include <cmath>

// Local display preferences from RA2MD.ini [Scaffold]. Never serialized or sent
// as gameplay events; the map/rules remain authoritative for game information.
struct FullMapSettings
{
	// GPU scaling is experimental: GL renders offscreen and reads back into
	// DirectDraw. Keep CPU as the default until target-machine timings win.
	inline static bool GPUScaling = false;
	inline static bool PerformanceLogging = false;
	inline static int PerformanceInterval = 2000;
	inline static int SlowFrameThreshold = 20;
	inline static bool UseNativeRadar = false;
	inline static bool TransparentShroud = false;
	inline static int ShroudOpacity = 80;
	inline static bool ScaleIconsWithZoom = true;
	inline static double IconScale = 1.0;
	inline static bool ShowOreSpawners = true;
	inline static bool ShowTechBuildings = true;
	inline static bool ShowBuildingLabels = true;
	inline static bool ShowHealthBars = true;
	inline static bool ShowOreValue = true;
	inline static bool HighlightOre = true;
	inline static bool LeftDragPan = true;
	inline static double ZoomStep = 1.5;
	inline static double MaxZoom = 16.0;

	// Shared size rule for unit icons and radar animation overlays.
	static double MarkerScale(float zoom)
	{
		return (ScaleIconsWithZoom
			? 1.0 + std::clamp((static_cast<double>(zoom) - 2.0) * 0.12, 0.0, 1.6)
			: 1.0) * IconScale;
	}

	static void Normalize()
	{
		PerformanceInterval = std::clamp(PerformanceInterval, 250, 10000);
		SlowFrameThreshold = std::clamp(SlowFrameThreshold, 1, 1000);
		ShroudOpacity = std::clamp(ShroudOpacity, 0, 100);
		IconScale = std::isfinite(IconScale) ? std::clamp(IconScale, 0.5, 3.0) : 1.0;
		ZoomStep = std::isfinite(ZoomStep) ? std::clamp(ZoomStep, 1.05, 2.0) : 1.5;
		MaxZoom = std::isfinite(MaxZoom) ? std::clamp(MaxZoom, 2.0, 16.0) : 16.0;
	}

	static unsigned char ShroudBrightness()
	{
		return TransparentShroud ? static_cast<unsigned char>((100 - std::clamp(ShroudOpacity, 0, 100)) * 255 / 100) : 0;
	}
};
