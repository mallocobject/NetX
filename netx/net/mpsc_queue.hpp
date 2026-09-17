#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace netx::net::details {

// 队列接受"可空句柄"：裸指针，或能由 nullptr 构造的类类型
template <typename T>
concept NullableHandle =
    std::is_pointer_v<T> ||
    (std::is_class_v<T> && std::constructible_from<T, std::nullptr_t>);

// Michael-Scott 队列：哑节点 + head/tail 双 CAS，无锁、无界。
template <NullableHandle T>
class MpscQueue {
  private:
    struct Node {
        T value;
        std::atomic<Node *> next;

        Node(const T &val) : value(val), next(nullptr) {
        }

        Node(T &&val) : value(std::move(val)), next(nullptr) {
        }
    };

  public:
    MpscQueue() {
        Node *dummy = new Node(T{nullptr});

        head_.store(dummy, std::memory_order_release);
        tail_.store(dummy, std::memory_order_release);
    }

    MpscQueue(MpscQueue &&other) noexcept {
        Node *old_head = other.head_.load(std::memory_order_relaxed);
        head_.store(old_head, std::memory_order_relaxed);
        other.head_.store(nullptr, std::memory_order_relaxed);

        Node *old_tail = other.tail_.load(std::memory_order_relaxed);
        tail_.store(old_tail, std::memory_order_relaxed);
        other.tail_.store(nullptr, std::memory_order_relaxed);

        size_t old_count = other.count_.load(std::memory_order_relaxed);
        count_.store(old_count, std::memory_order_relaxed);
        other.count_.store(0, std::memory_order_relaxed);
    }

    ~MpscQueue() {
        Node *cur = head_.load(std::memory_order_acquire);
        while (cur) {
            Node *next = cur->next.load(std::memory_order_acquire);
            delete cur;
            cur = next;
        }
    }

    void push(const T &data)
        requires std::is_copy_constructible_v<T>
    {
        pushImpl(new Node(data));
    }

    void push(T &&data) {
        pushImpl(new Node(std::move(data)));
    }

    bool pop(T &out);

    size_t size() const noexcept {
        return count_.load(std::memory_order_acquire);
    }

  private:
    void pushImpl(Node *n);

    std::atomic<Node *> head_;
    std::atomic<Node *> tail_;
    std::atomic<size_t> count_{0};
};

template <NullableHandle T>
void MpscQueue<T>::pushImpl(Node *n) {
    Node *tail_ptr = nullptr;
    while (true) {
        tail_ptr = tail_.load(std::memory_order_acquire);
        Node *next_ptr = tail_ptr->next.load(std::memory_order_acquire);

        if (tail_ptr != tail_.load(std::memory_order_acquire)) {
            continue;
        }

        if (next_ptr != nullptr) {
            tail_.compare_exchange_weak(tail_ptr,
                                        next_ptr,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed);
            continue;
        }

        if (tail_ptr->next.compare_exchange_weak(next_ptr,
                                                 n,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed)) {
            break;
        }
    }

    tail_.compare_exchange_weak(tail_ptr, n);
    count_.fetch_add(1, std::memory_order_acq_rel);
}

template <NullableHandle T>
bool MpscQueue<T>::pop(T &out) {
    while (true) {
        Node *head_ptr = head_.load(std::memory_order_acquire);
        Node *tail_ptr = tail_.load(std::memory_order_acquire);
        Node *next_ptr = head_ptr->next.load(std::memory_order_acquire);

        if (head_ptr != head_.load(std::memory_order_acquire)) {
            continue;
        }

        if (head_ptr == tail_ptr) {
            if (next_ptr == nullptr) {
                return false;
            }

            tail_.compare_exchange_weak(tail_ptr,
                                        next_ptr,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed);
            continue;
        }

        if (head_.compare_exchange_weak(head_ptr,
                                        next_ptr,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
            out = std::move(next_ptr->value);
            // FIXME: cannot apply in multi-consumer
            delete head_ptr;

            count_.fetch_sub(1, std::memory_order_acq_rel);
            return true;
        }
    }
}
} // namespace netx::net::details