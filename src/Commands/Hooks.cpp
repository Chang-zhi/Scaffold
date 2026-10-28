#include <Helpers/Macro.h>
#include "Command.h"
#include "RecruitPassengers.h"
#include "ToggleFullMap.h"

/// <summary>
/// Registers custom commands into the game's command system.
/// This hook runs after Ares has initialized its command registration,
/// so our custom commands are available alongside the game's built-in commands.
///
/// The address 0x533066 is the Ares command registration callback hook point.
/// To add more commands, call MakeCommand<YourClass>() here.
/// </summary>
DEFINE_HOOK(0x533066, CommandClassCallback_Register, 0x6)
{
	MakeCommand<AutoPassengersLoad>();
	MakeCommand<ToggleFullMapCommand>();

	return 0;
}
