#pragma once

#include "netx/core/awaitable_trait.hpp"
#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include <coroutine>
#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace netx::core {
namespace details {
struct WhenAnyCtlBlock {
    static constexpr size_t npos{static_cast<size_t>(-1)};

    size_t winner{npos};
    /// 真正参与竞速的输入个数。无效输入（空壳 Task）不算 —— 它们既没有
    /// 等待边，也不该被当成赢家；全都无效时就没必要挂起了。
    size_t racing{0};
    CoroHandle *waiter{nullptr};
    std::exception_ptr exception;
    std::span<const Task<Expected<>>> tasks;

    bool try_complete(size_t index, std::exception_ptr ep) {
        if (winner != npos) {
            return false;
        }

        winner = index;
        exception = ep;

        // 其余的都可以不跑了。取消会沿"我正在等谁"这条边继续往下传到各自的
        // 输入上 —— helper 正 co_await 在它身上。
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (i != index) {
                tasks[i].coro.promise().request_cancel();
            }
        }

        auto *w = std::exchange(waiter, nullptr);
        if (w == nullptr) {
            return true;
        }
        // 已经被取消的等待者早就带着 Cancelled 结果定局了（取消一个没有挂起
        // 记录的协程就是"给它一个结果"），此时再唤醒它，等于让它从挂起点继续
        // 往下跑、把那个结果覆盖掉 —— 那才是真正的复活。
        if (!w->cancelled) {
            w->wake(); // 强入队：等待者可能刚被取消过
        }
        return true;
    }
};

/// 能不能问"你有效吗"。Task 有 valid()，而裸 awaiter（比如 EventAwaiter）
/// 没有，后者一律当成有效的。
template <typename T>
constexpr bool is_valid(const T &t) {
    if constexpr (requires { t.valid(); }) {
        return t.valid();
    } else {
        return true;
    }
}

/// 把一路输入的结果写进它自己的槽位，并参与竞速。
///
/// 槽位类型必须由 AwaitableTrait 推出来（Task<Expected<T>> -> Expected<T>，
/// Task<T> -> T，裸 awaiter -> 它 await_resume() 的类型），不能写死成
/// Expected<T>&：写死的那版只对 Task<Expected<...>> 成立，因为 T 是从
/// Expected<T>& 反推的；换成 Task<int> 时槽位是裸 int，int& 绑不上
/// Expected<T>&，模板直接不可行，整条 when_any 编译失败。
template <Awaitable A>
Task<Expected<>>
WhenAnyHelper(A &&t, WhenAnyCtlBlock &ctl, NonVoidRetType<A> &result,
              size_t index) {
    // 空壳输入：没有东西可等。它不算赢家，也不能这么 co_await —— 等一个
    // 空壳任务等于"被取消"（Task 的 awaiter 会把调用者冻在挂起点），helper
    // 就再也走不到 try_complete()，when_any 便永远等不到赢家。
    if (!is_valid(t)) {
        co_return {};
    }

    try {
        if constexpr (std::is_same_v<NonVoidRetType<A>, NonVoidHelper<void>>) {
            // await_resume() 返回 void 的 awaiter（Task / Expected 之外的
            // 自定义 awaiter）：没有值可存，把槽位占上就行
            co_await std::forward<A>(t);
            result = NonVoidRetType<A>{};
        } else {
            result = co_await std::forward<A>(t);
        }
        ctl.try_complete(index, nullptr);
    } catch (...) {
        ctl.try_complete(index, std::current_exception());
    }
    co_return {};
}

struct WhenAnyAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    template <typename P>
    bool await_suspend(std::coroutine_handle<P> coro) const noexcept {
        for (const auto &t : ctl.tasks) {
            if (t.valid() && !t.done()) {
                t.coro.promise().schedule();
            }
        }

        // 全都无效／没有可等的：不挂起，让 await_resume() 后直接走"没赢家"
        // 那条路给出 InvalidOperation
        if (ctl.racing == 0) {
            return false;
        }

        // 登记"谁在等"：赢家收尾时顺着它把本协程叫醒。
        ctl.waiter = &coro.promise();
        return true;
    }

    void await_resume() const {
        if (ctl.exception) [[unlikely]] {
            std::rethrow_exception(ctl.exception);
        }
    }

    WhenAnyCtlBlock &ctl;
};

template <size_t... Is, typename... Ts>
Task<Expected<std::variant<NonVoidRetType<Ts>...>>>
when_any_impl(std::index_sequence<Is...>, Ts... ts) {
    // 参数按值收：输入对象必须活到 helper 真正跑起来，而本协程是惰性的 ——
    // 引用参数会指向调用方那个早就结束的全表达式。
    WhenAnyCtlBlock ctl{};
    ctl.racing = (size_t{0} + ... + (is_valid(ts) ? size_t{1} : size_t{0}));
    std::tuple<NonVoidRetType<Ts>...> results;
    Task<Expected<>> helpers[]{
        WhenAnyHelper(std::move(ts), ctl, std::get<Is>(results), Is)...};
    ctl.tasks = helpers;

    co_await WhenAnyAwaiter{ctl};

    using VariantType = std::variant<NonVoidRetType<Ts>...>;
    // 用 optional 而不是直接建 variant：后者要求第一个备选类型可默认构造，
    // 而我们只想在赢家那一支里把它建出来。
    std::optional<VariantType> out;

    // void 输入的结果已经由 helper 写成 NonVoidHelper，这里统一搬过来即可
    ((ctl.winner == Is ? (void)out.emplace(std::in_place_index<Is>,
                                           std::move(std::get<Is>(results)))
                       : (void)0),
     ...);

    if (!out) [[unlikely]] {
        // 没有任何一方能给出结果（比如传进来的全是空壳 Task）
        co_return std::unexpected{make_error_code(Error::InvalidOperation)};
    }
    co_return std::move(*out);
}
} // namespace details

template <typename... Ts>
auto when_any(Ts... ts) {
    // 按值收再往下挪：Task 是 move-only，调用方传右值（或 std::move）。
    // 这样输入的生命周期就归 when_any 返回的那个任务所有了。
    //
    // 这条断言把"哪些输入能用"讲清楚，替掉 WhenAnyHelper 那条晦涩的
    // "no matching function"。裸 Expected 看着像能进（AwaitableTrait 里
    // 确实有它的特化），但它没有可等的动作：promise 的 await_transform
    // 在失败时把结果写进 helper 自己的 promise 并冻结协程，
    // try_complete() 永远不会被调用，整条 when_any 会静默死等。
    static_assert(
        (details::Awaitable<Ts> && ...),
        "when_any 的每个输入都必须是 awaitable（本库的 Task<...>，或本身就"
        "是 awaiter 的类型）；裸 Expected 是已经算好的值，请直接用分支处理，"
        "不要放进竞速");
    return details::when_any_impl(std::make_index_sequence<sizeof...(Ts)>{},
                                  std::move(ts)...);
}
} // namespace netx::core

// 两个已知缺口（都要"一个节点多条等待边"才能彻底解决 —— Handle::waiting_on
// 是单指针）：
//   1. 取消 when_any 返回的任务，不会连带取消它的 helper：等待关系登记在
//      自定义 awaiter 上（ctl.waiter），不走 AwaiterBase，没有 waiting_on 边；
//   2. 所以 helper 会继续跑到结束，只是结果被丢掉 —— 它们的帧归 when_any 的
//      帧所有，一起销毁，不泄漏也不复活（唤醒前会检查 cancelled）。