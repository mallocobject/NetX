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
struct WhenAnyCtlBlock : public CancelHook {
    static constexpr size_t npos{static_cast<size_t>(-1)};

    bool try_complete(size_t index, std::exception_ptr ep) {
        if (winner != npos) {
            return false;
        }

        winner = index;
        exception = ep;

        // 一决雌雄
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (i != index) {
                tasks[i].coro.promise().request_cancel();
            }
        }

        auto *w = std::exchange(waiter, nullptr);
        if (w == nullptr) {
            return true;
        }

        // 唤醒等待着，等于让它从挂起点继续
        // 往下跑、把那个结果覆盖掉 —— 那才是真正的复活。
        if (!w->cancelled) {
            w->wake(); // 强入队：等待者可能刚被取消过
        }
        return true;
    }

    void cancel_downstream() noexcept override {
        for (auto &t : tasks) {
            if (t.valid() && !t.done()) {
                t.coro.promise().request_cancel();
            }
        }
        winner = npos; // 别让它之后再去唤醒一个已经取消的等待者
    }

    size_t winner{npos};
    /// 真正参与竞速的输入个数。无效输入（空壳 Task）不算
    size_t racing{0};
    CoroHandle *waiter{nullptr};
    std::exception_ptr exception;
    std::span<const Task<Expected<>>> tasks;
};

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
Task<Expected<>> WhenAnyHelper(A &&t,
                               WhenAnyCtlBlock &ctl,
                               NonVoidRetType<A> &result,
                               size_t index) {
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

        if (ctl.racing == 0) {
            return false;
        }

        // 登记"谁在等"：赢家收尾时顺着它把本协程叫醒。
        auto &promise = coro.promise();
        ctl.waiter = &promise;
        promise.cancel_hook = &ctl;

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
    WhenAnyCtlBlock ctl{};
    ctl.racing = (size_t{0} + ... + (is_valid(ts) ? size_t{1} : size_t{0}));
    std::tuple<NonVoidRetType<Ts>...> results;
    Task<Expected<>> helpers[]{
        WhenAnyHelper(std::move(ts), ctl, std::get<Is>(results), Is)...};
    ctl.tasks = helpers;

    co_await WhenAnyAwaiter{ctl};

    using VariantType = std::variant<NonVoidRetType<Ts>...>;

    std::optional<VariantType> out;

    // void 输入的结果已经由 helper 写成 NonVoidHelper
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
    static_assert(
        (details::Awaitable<Ts> && ...),
        "every when_any argument must be awaitable (a Task<...> from this "
        "library, or a type that is itself an awaiter); a bare Expected is an "
        "already-settled value, handle it with a branch instead of racing it");
    return details::when_any_impl(std::make_index_sequence<sizeof...(Ts)>{},
                                  std::move(ts)...);
}
} // namespace netx::core
