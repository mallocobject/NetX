#pragma once

#include <atomic>
#include <cstddef>
#include <utility>
namespace netx
{
namespace net
{
namespace details
{
template <typename T> struct LockFreeQueue
{
	struct Node
	{
		T value;
		std::atomic<Node*> next;

		Node(const T& val) noexcept : value(val), next(nullptr)
		{
		}

		Node(T&& val) noexcept : value(std::move(val)), next(nullptr)
		{
		}
	};

	LockFreeQueue()
	{
		Node* dummy = new Node(T{nullptr});

		head.store(dummy, std::memory_order_release);
		tail.store(dummy, std::memory_order_release);
	}

	LockFreeQueue(LockFreeQueue&& other) noexcept
	{
		Node* old_head = other.head.load(std::memory_order_relaxed);
		head.store(old_head, std::memory_order_relaxed);
		other.head.store(nullptr, std::memory_order_relaxed);

		Node* old_tail = other.tail.load(std::memory_order_relaxed);
		tail.store(old_tail, std::memory_order_relaxed);
		other.tail.store(nullptr, std::memory_order_relaxed);

		size_t old_count = other.count.load(std::memory_order_relaxed);
		count.store(old_count, std::memory_order_relaxed);
		other.count.store(0, std::memory_order_relaxed);
	}

	~LockFreeQueue()
	{
		Node* cur = head.load(std::memory_order_acquire);
		while (cur)
		{
			Node* next = cur->next.load(std::memory_order_acquire);
			delete cur;
			cur = next;
		}
	}

	void push(const T& data)
	{
		pushImpl(new Node(data));
	}

	void push(T&& data)
	{
		pushImpl(new Node(std::move(data)));
	}

	bool pop(T& out);

	std::size_t size() const noexcept
	{
		return count.load(std::memory_order_acquire);
	}

  private:
	void pushImpl(Node* n);

  private:
	std::atomic<Node*> head;
	std::atomic<Node*> tail;
	std::atomic<std::size_t> count{0};
};

template <typename T> void LockFreeQueue<T>::pushImpl(Node* n)
{
	Node* tail_ptr = nullptr;
	while (true)
	{
		tail_ptr = tail.load(std::memory_order_acquire);
		Node* next_ptr = tail_ptr->next.load(std::memory_order_acquire);

		if (tail_ptr != tail.load(std::memory_order_acquire))
		{
			continue;
		}

		if (next_ptr != nullptr)
		{
			tail.compare_exchange_weak(tail_ptr, next_ptr,
									   std::memory_order_acquire,
									   std::memory_order_relaxed);
			continue;
		}

		if (tail_ptr->next.compare_exchange_weak(next_ptr, n,
												 std::memory_order_release,
												 std::memory_order_relaxed))
		{
			break;
		}
	}

	tail.compare_exchange_weak(tail_ptr, n);
	count.fetch_add(1, std::memory_order_acq_rel);
}

template <typename T> bool LockFreeQueue<T>::pop(T& out)
{
	while (true)
	{
		Node* head_ptr = head.load(std::memory_order_acquire);
		Node* tail_ptr = tail.load(std::memory_order_acquire);
		Node* next_ptr = head_ptr->next.load(std::memory_order_acquire);

		if (head_ptr != head.load(std::memory_order_acquire))
		{
			continue;
		}

		if (head_ptr == tail_ptr)
		{
			if (next_ptr == nullptr)
			{
				return false;
			}

			tail.compare_exchange_weak(tail_ptr, next_ptr,
									   std::memory_order_acquire,
									   std::memory_order_relaxed);
			continue;
		}

		if (head.compare_exchange_weak(head_ptr, next_ptr,
									   std::memory_order_acquire,
									   std::memory_order_relaxed))
		{
			out = std::move(next_ptr->value);
			// FIXME: cannot apply in multi-consumer
			delete head_ptr;

			count.fetch_sub(1, std::memory_order_acq_rel);
			return true;
		}
	}
}
} // namespace details
} // namespace net
} // namespace netx