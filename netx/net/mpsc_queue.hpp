#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace netx::net::details {

// 队列接受"可空句柄"：裸指针，或能由 nullptr 构造的类类型
// （智能指针、Task、自定义句柄都算）。
//
//   * 哑节点靠 `T{nullptr}` 构造 —— nullptr 构造是唯一的硬性前提
//   * 不要求可拷贝、也不要求移动不抛：move-only 的任务队列正是本队列的
//     主要用途，而"拷贝不抛"只对 push(const T&) 这一个重载有意义，因此
//     用 requires 单独约束那个重载，不强加给整个类型。Node 的构造函数
//     也相应不再标 noexcept —— 让可恢复的异常正常传播，而不是变成
//     std::terminate
//   * is_class_v 那条专门挡 bool：它恰好也能由 nullptr 构造
//     （nullptr_t 转 bool 得 false），但语义上根本不是句柄；顺带也挡掉了
//     uintptr_t 这类整数
template <typename T>
concept NullableHandle =
    std::is_pointer_v<T> ||
    (std::is_class_v<T> && std::constructible_from<T, std::nullptr_t>);

// Michael-Scott 队列：哑节点 + head/tail 双 CAS，无锁、无界。
//
// 并发契约是 MPSC —— 任意多个生产者，**单个**消费者：
//   * push() / push(T&&) 可由任意多个线程并发调用
//   * pop() 必须有且只有一个线程调用
//   * size() 可从任意线程读，但只是近似值：计数在链接 CAS 之后才自增，
//     别的线程可能先看到节点、后看到计数
template <NullableHandle T>
class MpscQueue {
  private:
    struct Node {
        T value;
        std::atomic<Node *> next;

        // 不标 noexcept：T 的拷贝/移动可能抛，标了就会把可恢复的异常
        // 变成 std::terminate。这里抛出去是安全的 —— Node 是在进入
        // pushImpl 之前构造的，异常传播时无锁不变量还没被触碰
        Node(const T &val) : value(val), next(nullptr) {
        }

        Node(T &&val) : value(std::move(val)), next(nullptr) {
        }
    };

  private:
    std::atomic<Node *> head_;
    std::atomic<Node *> tail_;
    std::atomic<size_t> count_{0};

  private:
    void pushImpl(Node *n);

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

    // 拷贝入队只对可拷贝的 T 开放：move-only 句柄（Task、unique_ptr）
    // 只保留右值重载，对左值调用会得到一条清晰的约束错误
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