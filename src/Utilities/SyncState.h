#pragma once

// These policies use simulation values only and can be tested without the host.
namespace SyncState
{
	struct FrameGate
	{
		int LastFrame = -1;

		bool TryEnter(int frame)
		{
			if (frame < 0 || LastFrame == frame)
				return false;
			LastFrame = frame;
			return true;
		}

		void Reset() { LastFrame = -1; }
	};

	constexpr bool IsPeriodicFrame(int frame, int period)
	{
		return frame >= 0 && period > 0 && frame % period == 0;
	}

	constexpr bool HasQueueRoom(int queued, int requested, int capacity)
	{
		return queued >= 0 && queued <= capacity && requested >= 0
			&& requested <= capacity - queued;
	}

	// Keep the native command path, including any installed DLL wrappers.
	// Roll back if it rejects the command or fails to enqueue every order.
	template <typename Queue, typename Submit>
	bool QueueTransaction(Queue& queue, int count, int capacity, Submit&& submit)
	{
		if (!HasQueueRoom(queue.Count, count, capacity))
			return false;
		const Queue checkpoint(queue);
		if (!submit() || queue.Count != checkpoint.Count + count)
		{
			queue = checkpoint;
			return false;
		}
		return true;
	}

	template <typename Map>
	void InvalidatePointer(Map& map, const void* ptr)
	{
		for (auto it = map.begin(); it != map.end(); )
		{
			if (it->first == ptr || it->second == ptr)
				it = map.erase(it);
			else
				++it;
		}
	}
}
