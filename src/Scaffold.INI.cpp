#include "Scaffold.h"

#include <CCINIClass.h>
#include <Utilities/Macro.h>
#include <New/FullMap/FullMapSettings.h>

bool Scaffold::Config::SaveGameOnScenarioStart = true;
bool Scaffold::Config::AllowTabBriefingInSinglePlayer = false;
bool Scaffold::Config::PreserveCSFSpaces = false;
bool Scaffold::Config::FullMapMarksInWorld = true;

DEFINE_HOOK(0x5FACDF, OptionsClass_LoadSettings_LoadScaffoldSettings, 0x5)
{
	const auto scaffoldSection = "Scaffold";

	Scaffold::Config::AllowTabBriefingInSinglePlayer =
		CCINIClass::INI_RA2MD.ReadBool(scaffoldSection, "AllowTabBriefingInSinglePlayer", false);

	// 战术地图标记是否同时显示在实际战术视野中
	Scaffold::Config::FullMapMarksInWorld =
		CCINIClass::INI_RA2MD.ReadBool(scaffoldSection, "FullMap.MarksInWorld",
			CCINIClass::INI_RA2MD.ReadBool(scaffoldSection, "FullMapMarksInWorld", true));


	// FullMap.* are per-computer display settings, independent of rulesmd.ini.
	auto& ini = CCINIClass::INI_RA2MD;
	FullMapSettings::GPUScaling = ini.ReadBool(scaffoldSection, "FullMap.GPUScaling", false);
	FullMapSettings::PerformanceLogging = ini.ReadBool(scaffoldSection, "FullMap.PerformanceLogging", false);
	FullMapSettings::PerformanceInterval = ini.ReadInteger(scaffoldSection, "FullMap.PerformanceInterval", 2000);
	FullMapSettings::SlowFrameThreshold = ini.ReadInteger(scaffoldSection, "FullMap.SlowFrameThreshold", 20);
	FullMapSettings::UseNativeRadar = ini.ReadBool(scaffoldSection, "FullMap.UseNativeRadar", false);
	FullMapSettings::TransparentShroud = ini.ReadBool(scaffoldSection, "FullMap.TransparentShroud", false);
	FullMapSettings::ScaleIconsWithZoom = ini.ReadBool(scaffoldSection, "FullMap.ScaleIconsWithZoom", true);
	FullMapSettings::ShowOreSpawners = ini.ReadBool(scaffoldSection, "FullMap.ShowOreSpawners", true);
	FullMapSettings::ShowTechBuildings = ini.ReadBool(scaffoldSection, "FullMap.ShowTechBuildings", true);
	FullMapSettings::ShowBuildingLabels = ini.ReadBool(scaffoldSection, "FullMap.ShowBuildingLabels", true);
	FullMapSettings::ShowHealthBars = ini.ReadBool(scaffoldSection, "FullMap.ShowHealthBars", true);
	FullMapSettings::ShowOreValue = ini.ReadBool(scaffoldSection, "FullMap.ShowOreValue", true);
	FullMapSettings::HighlightOre = ini.ReadBool(scaffoldSection, "FullMap.HighlightOre", true);
	FullMapSettings::LeftDragPan = ini.ReadBool(scaffoldSection, "FullMap.LeftDragPan", true);
	FullMapSettings::ShroudOpacity = ini.ReadInteger(scaffoldSection, "FullMap.ShroudOpacity", 80);
	FullMapSettings::IconScale = ini.ReadDouble(scaffoldSection, "FullMap.IconScale", 1.0);
	FullMapSettings::ZoomStep = ini.ReadDouble(scaffoldSection, "FullMap.ZoomStep", 1.5);
	FullMapSettings::MaxZoom = ini.ReadDouble(scaffoldSection, "FullMap.MaxZoom", 16.0);
	FullMapSettings::Normalize();

	return 0;
}


DEFINE_HOOK(0x734B46, StringTable_ReadFile_PreserveCSFSpaces, 0x5)
{
	static const bool preserve =
		CCINIClass::INI_RA2MD.ReadBool("Scaffold", "PreserveCSFSpaces", false);

	Scaffold::Config::PreserveCSFSpaces = preserve;

	if (preserve)
	{
		return 0x734BB4;
	}

	R->EAX(Make_Global<DWORD>(0xB17F24));
	return 0;
}

DEFINE_HOOK(0x434F53, Font_DrawTextBlock_WrapMinWidth, 0x6)
{
	DWORD const spacePos = R->Stack<DWORD>(0x24);

	if (!spacePos)
		return 0x434F6A;

	if (Scaffold::Config::PreserveCSFSpaces)
	{
		constexpr int MinPercent = 50;
		int const lineWidth = R->Stack<int>(0x58); 
		int const spaceWidth = R->Stack<int>(0x2C);

		if (lineWidth > 0 && spaceWidth * 100 < lineWidth * MinPercent)
			return 0x434F6A;
	}

	R->ECX(spacePos);
	return 0x434F5B;
}
