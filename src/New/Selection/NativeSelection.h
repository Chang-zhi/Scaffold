#pragma once

class TacticalClass;
class ObjectClass;

namespace Selection::Native
{
	// 6D9EF0 is a thiscall entry: ECX = TacticalClass*, three stack args,
	// callee pops 12 bytes. The unused EDX parameter adapts that ABI to fastcall.
	using RegisterSelectableFn = bool(__fastcall*)(TacticalClass*, void*, ObjectClass*, int, int);

	template <typename Service>
	bool __fastcall RegisterSelectable(TacticalClass* tactical, void*, ObjectClass* object, int x, int y)
	{
		return Service::AddSelectable(tactical, object, x, y);
	}

	template <typename Service>
	constexpr RegisterSelectableFn RegistrationEntry = &RegisterSelectable<Service>;
}
