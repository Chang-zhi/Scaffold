#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <vector>

namespace Selection
{
	// Native may send only a prefix of the last batch before the next mouse
	// command arrives. That prefix is older than Pending; newly appended native
	// events are newer. Keep both sides of that boundary in FIFO order.
	template <typename T, int Capacity, int BatchSize, typename Identity = std::equal_to<T>>
	class CommandBuffer
	{
		static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);
		static_assert(BatchSize > 0 && BatchSize < Capacity);

		std::deque<T> Pending;
		std::vector<T> Exposed;
		int HeadIndex = 0;
		int HeadCount = 0;

		template <typename Queue>
		void Recover(Queue& queue)
		{
			const int advanced = (queue.GetHead() - this->HeadIndex) & (Capacity - 1);
			// A complete native ring can drain and wrap Head back to its old
			// index. An unhooked control event added afterwards is NEW, even
			// though the index matches. Verify the outstanding command identity.
			const bool matches = queue.Count && advanced < this->HeadCount
				&& Identity {}(this->Exposed[advanced], queue[0]);
			const int prefix = matches ? std::min(queue.Count, this->HeadCount - advanced) : 0;

			for (int i = prefix; i > 0; --i)
				this->Pending.push_front(queue[i - 1]);
			for (int i = prefix; i < queue.Count; ++i)
				this->Pending.push_back(queue[i]);

			queue.Init();
			this->HeadCount = 0;
			this->Exposed.clear();
		}

	public:
		template <typename Queue>
		void BeforeAdd(Queue& queue)
		{
			if (!this->Pending.empty() || this->HeadCount || queue.Count >= BatchSize)
				this->Recover(queue);
		}

		template <typename Queue>
		void Pump(Queue& queue, bool allowNewBatch)
		{
			if (this->Pending.empty() && !this->HeadCount)
				return;

			this->Recover(queue);
			if (!allowNewBatch)
				return;

			while (!this->Pending.empty() && queue.Count < BatchSize)
			{
				if (!queue.Add(this->Pending.front()))
					break;
				this->Exposed.push_back(this->Pending.front());
				this->Pending.pop_front();
			}
			this->HeadIndex = queue.GetHead();
			this->HeadCount = queue.Count;
		}

		template <typename Queue, typename Transform>
		void TransformPending(Queue& queue, Transform&& transform)
		{
			this->Recover(queue);
			this->Pending = transform(std::move(this->Pending));
		}

		void Clear()
		{
			this->Pending.clear();
			this->HeadIndex = 0;
			this->HeadCount = 0;
			this->Exposed.clear();
		}

		std::size_t PendingCount() const { return this->Pending.size(); }
		int ExposedCount() const { return this->HeadCount; }

		bool Empty() const { return this->Pending.empty() && !this->HeadCount; }
	};
}
