#include "Scaffold.h"

#include <CCINIClass.h>
#include <Utilities/Macro.h>

bool Scaffold::Config::SaveGameOnScenarioStart = true;
bool Scaffold::Config::AllowTabBriefingInSinglePlayer = false;
bool Scaffold::Config::PreserveCSFSpaces = false;

DEFINE_HOOK(0x5FACDF, OptionsClass_LoadSettings_LoadScaffoldSettings, 0x5)
{
	const auto scaffoldSection = "Scaffold";

	Scaffold::Config::AllowTabBriefingInSinglePlayer =
		CCINIClass::INI_RA2MD.ReadBool(scaffoldSection, "AllowTabBriefingInSinglePlayer", false);

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
