#pragma once

#include <cstdint>

namespace TransportQueue::Policy
{
	enum class EnterMode { Vanilla, ImprovedQueue, Parallel };

	struct Options
	{
		bool ImprovedQueue = false;
		bool NoQueueEnter = false;
		bool NoQueueUnload = false;
		int EnterMaxCount = 0;
		int UnloadDelay = 0;
	};

	constexpr int NonNegative(int value)
	{
		return value > 0 ? value : 0;
	}

	template <typename LocalOptions>
	Options ResolveOptions(const LocalOptions* local, const Options& global)
	{
		return {
			local ? local->Passengers_ImprovedQueue.Get(global.ImprovedQueue) : global.ImprovedQueue,
			local ? local->Passengers_NoQueueEnter.Get(global.NoQueueEnter) : global.NoQueueEnter,
			local ? local->Passengers_NoQueueUnload.Get(global.NoQueueUnload) : global.NoQueueUnload,
			NonNegative(local ? local->Passengers_NoQueueEnter_MaxCount.Get(global.EnterMaxCount) : global.EnterMaxCount),
			NonNegative(local ? local->Passengers_NoQueueUnload_Delay.Get(global.UnloadDelay) : global.UnloadDelay)
		};
	}

	constexpr EnterMode GetEnterMode(bool improvedQueue, bool noQueueEnter)
	{
		return noQueueEnter ? EnterMode::Parallel
			: (improvedQueue ? EnterMode::ImprovedQueue : EnterMode::Vanilla);
	}

	constexpr bool FitsCapacity(double occupied, double reserved, double incoming, double capacity)
	{
		return occupied + reserved + incoming <= capacity;
	}

	// Only admitted, still entering passengers consume concurrent places.
	// Repeated requests from an admitted passenger never need another place.
	template <typename Passenger, typename Confirmed>
	bool HasEntrySlot(Passenger* incoming, Passenger* const* links, int count,
		int maxCount, Confirmed confirmed)
	{
		if (maxCount <= 0 || confirmed(incoming))
			return true;
		int entering = 0;
		for (int i = 0; i < count; ++i)
		{
			auto pending = links[i];
			if (pending && pending != incoming && confirmed(pending) && ++entering >= maxCount)
				return false;
		}
		return true;
	}

	template <typename Passenger, typename Confirmed, typename Size>
	bool CanReserve(Passenger* incoming, Passenger* const* links, int count,
		double occupied, double capacity, Confirmed confirmed, Size size)
	{
		double reserved = 0.0;
		for (int i = 0; i < count; ++i)
		{
			auto pending = links[i];
			if (pending && pending != incoming && confirmed(pending))
				reserved += size(pending);
		}
		return FitsCapacity(occupied, reserved, size(incoming), capacity);
	}

	// Cell coordinates, not pointer addresses or local selection order, decide
	// admission. The caller breaks equal distances by the engine's Foot array.
	constexpr std::int64_t DistanceSquared(int dx, int dy)
	{
		return std::int64_t(dx) * dx + std::int64_t(dy) * dy;
	}

	constexpr bool IsNear(int dx, int dy, int dz, int radius, int cellHeight)
	{
		return dx >= -radius && dx <= radius && dy >= -radius && dy <= radius
			&& dz > -cellHeight && dz < cellHeight;
	}

	template <typename Mission>
	constexpr bool HasEnterMission(Mission current, Mission queued)
	{
		return queued == Mission::Enter || (queued == Mission::None && current == Mission::Enter);
	}

	template <typename Command>
	constexpr bool IsLoadingCommand(Command command)
	{
		return command == Command::NotifyBeginLoad || command == Command::RequestLoading;
	}

	template <typename T>
	bool FocusLink(T** links, int count, int index)
	{
		if (index <= 0 || index >= count)
			return false;
		auto previous = links[0];
		links[0] = links[index];
		links[index] = previous;
		return true;
	}

	template <typename T>
	int CompactLinks(T** links, int count)
	{
		if (!links || count <= 0)
			return 0;

		int write = 0;
		for (int read = 0; read < count; ++read)
		{
			if (links[read])
			{
				if (write != read)
					links[write] = links[read];
				++write;
			}
		}

		for (int i = write; i < count; ++i)
			links[i] = nullptr;

		return write;
	}

	template <typename T, typename Eligible, typename Distance>
	T* Nearest(T* const* items, int count, Eligible eligible, Distance distance)
	{
		T* nearest = nullptr;
		std::int64_t best = INT64_MAX;
		for (int i = 0; i < count; ++i)
		{
			auto item = items[i];
			if (!eligible(item))
				continue;
			const auto value = distance(item);
			if (value < best)
			{
				nearest = item;
				best = value;
			}
		}
		return nearest;
	}

	enum class UnloadAction { Finish, ContinueNow, Wait };

	constexpr UnloadAction AfterSuccessfulUnload(int passengers, int retainedPassengers, int delay)
	{
		if (passengers <= retainedPassengers)
			return UnloadAction::Finish;
		return delay > 0 ? UnloadAction::Wait : UnloadAction::ContinueNow;
	}

	constexpr bool CanContinueUnload(int passengers, int retainedPassengers)
	{
		return passengers > retainedPassengers;
	}
}
