#pragma once

namespace Selection
{
	constexpr bool CanAdd(int limit, int count)
	{
		return limit <= 0 || count < limit;
	}

	// A grounded Jumpjet infantry can still have the walking locomotor. Ground
	// reachability/formation must not replace a plain air-move click by its
	// launch cell. Vehicle deployers and units already in the air are excluded.
	constexpr bool PreserveGroundedFlightDestination(bool infantry, bool jumpjet, bool airborne)
	{
		return infantry && jumpjet && !airborne;
	}

	// Query native actions with the actual destination and modifier flags.
	// A stationary representative only yields to an actual Move result.
	template <typename Object, typename Range, typename IsBlockedDeployer, typename GetAction>
	Object* MoveRepresentative(Object* original, const Range& objects,
		IsBlockedDeployer isBlockedDeployer, GetAction getAction, int move, int noMove, int none)
	{
		if (!original || !isBlockedDeployer(original))
			return original;

		const int action = getAction(original);
		if (action != noMove && action != none)
			return original;

		for (auto object : objects)
		{
			if (object && object != original && !isBlockedDeployer(object) && getAction(object) == move)
				return object;
		}
		return original;
	}
}
