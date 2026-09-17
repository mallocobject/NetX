#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/result.hpp"
#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <system_error>
#include <type_traits>
#include <utility>

namespace netx::core {
namespace details {
inline constexpr struct NoWaitAtInitialSuspend {
} no_wait_at_initial_suspend{};
} // namespace details

/// 惰性协程任务：被 co_await 或交给调度器时才跑，跑完停在 final_suspend 上，
/// 结果留在 promise 里等等待者取走。本对象持有协程帧，析构即销毁。
template <typename T = void>
class [[nodiscard]] Task {
  public:
    class promise_type : public details::CoroHandle, public details::Result<T> {
      public:
        promise_type() = default;

        ~promise_type() {
            if (owns_continuation_ && continuation_) {
                delete continuation_;
                continuation_ = nullptr;
            }
        }

        details::CoroHandle *continuation() const noexcept {
            return continuation_;
        }

        /// 约定：一个任务要么挂在父协程下（AwaiterBase 登记父协程，owns
        /// 为 false），要么被包装层托管（WrappedTask 登记 DeleteNodeHandle，
        /// owns 为 true）
        void set_continuation(details::CoroHandle *continuation,
                              bool owns = false) noexcept {
            assert(continuation_ == nullptr);
            continuation_ = continuation;
            owns_continuation_ = owns;
        }

        /// 通知等待者可以继续了，并解掉它的反向边。
        void notify_continuation() noexcept {
            auto *next = std::exchange(continuation_, nullptr);
            if (next == nullptr) {
                return;
            }
            if (next->waiting_on == this) {
                next->waiting_on = nullptr;
            }
            next->wake();
        }

        template <typename... Args>
        promise_type(details::NoWaitAtInitialSuspend, Args &&...) noexcept
            : wait_at_initial_suspend_(false) {
        }

        template <typename Obj, typename... Args>
        promise_type(Obj &&,
                     details::NoWaitAtInitialSuspend,
                     Args &&...) noexcept
            : wait_at_initial_suspend_(false) {
        }

        Task get_return_object() noexcept {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() const noexcept {
            struct {
                bool await_ready() const noexcept {
                    return !wait_at_initial_suspend;
                }

                void await_suspend(std::coroutine_handle<>) const noexcept {
                }

                void await_resume() const noexcept {
                }

                bool wait_at_initial_suspend{true};
            } initial_suspend_awaiter{wait_at_initial_suspend_};

            return initial_suspend_awaiter;
        }

        std::suspend_always final_suspend() noexcept {
            sealed_ = true;
            notify_continuation();
            return {};
        }

        template <typename U>
        auto await_transform(Expected<U> &&exp) {
            struct {
                bool await_ready() const noexcept {
                    return exp.has_value();
                }

                void await_suspend(
                    std::coroutine_handle<promise_type> coro) noexcept {
                    auto &promise = coro.promise();
                    // T 声明为 Expected<...> 时，这里直接把"失败的 expected"
                    // 存成任务结果，失败信息以 error_code 形式原样带出。
                    promise.put_value(std::unexpected{std::move(exp.error())});
                    promise.notify_continuation();
                }

                U await_resume() {
                    if constexpr (!std::is_void_v<U>) {
                        return std::move(exp.value());
                    }
                }

                Expected<U> exp;
            } awaiter{std::move(exp)};
            return awaiter;
        }

        template <typename U>
        auto await_transform(Expected<U> &exp) {
            return await_transform(std::move(exp));
        }

        template <typename U>
        auto await_transform(Task<U> &task) {
            return task.operator co_await();
        }

        template <typename U>
        auto await_transform(Task<U> &&task) {
            return std::move(task).operator co_await();
        }

        template <typename A>
        A &&await_transform(A &&awaiter) noexcept {
            return std::forward<A>(awaiter);
        }

        /// 被 EventLoop::cancel() 取消时的收尾：给尚未定局的任务定一个取消
        /// 结果，并唤醒等待者。
        void on_cancel() noexcept override {
            if (sealed_) {
                return;
            }
            if constexpr (!std::is_void_v<T>) {
                if (this->settled()) {
                    return;
                }
            }
            sealed_ = true;

            // T 能装下 error_code（也就是"声明成了 Expected"）就直接写值；
            // 否则只能借异常通道
            if constexpr (!std::is_void_v<T> &&
                          std::constructible_from<
                              T,
                              std::unexpected<std::error_code>>) {
                this->put_value(std::unexpected{
                    details::make_error_code(details::Error::Cancelled)});
            } else {
                this->put_exception(std::make_exception_ptr(std::system_error(
                    details::make_error_code(details::Error::Cancelled))));
            }

            notify_continuation();
        }

        void run() override final {
            std::coroutine_handle<promise_type>::from_promise(*this).resume();
        }

      private:
        details::CoroHandle *continuation_{nullptr};
        bool owns_continuation_{false}; // 所有权标志
        bool wait_at_initial_suspend_{true};
        /// 收尾动作是否已经做过：跑到 final_suspend，或由取消路径定过结果。
        /// 它和 settled() 不是一回事 —— 冻结在失败 co_await 处的任务
        /// settled() 已经为真，但从没跑过 final_suspend。取消路径靠这一位
        /// 保证同一个任务只定一次结果。
        bool sealed_{false};
    };

    // ---- 等待者：await_ready/await_suspend 共用，await_resume 分左右值 ----
    struct AwaiterBase {
        /// 无效帧（空壳 Task）不属于"已完成"：它没有结果可取。这里返回
        /// false，把决定权交给 await_suspend() —— 若返回 true，紧接着的
        /// await_resume() 就会去解引用空 promise。
        bool await_ready() const noexcept {
            return callee_coro && callee_coro.done();
        }

        /// 取结果前先过这里。空壳帧没有 promise 可读，解引用它就是 UB
        promise_type &checked_promise() const {
            if (callee_coro == nullptr) [[unlikely]] {
                throw std::system_error{
                    details::make_error_code(details::Error::Cancelled)};
            }
            return callee_coro.promise();
        }

        template <typename P>
        void
        await_suspend(std::coroutine_handle<P> caller_coro) const noexcept {
            static_assert(std::is_base_of_v<details::CoroHandle, P>,
                          "Caller promise must inherit CoroHandle");

            auto *caller = &caller_coro.promise();

            if (callee_coro == nullptr) {
                caller->cancelled = true;
                caller->on_cancel();
                return;
            }

            auto *callee = &callee_coro.promise();

            // 先登记等待关系：父协程在这段等待里没有挂起记录（记录属于子
            // 任务），只有这条边能让 EventLoop::cancel() 从父走到子。
            caller->waiting_on = callee;
            callee->set_continuation(caller);

            if (caller->cancelled) {
                callee->cancelled = true;
                callee->on_cancel();
                return;
            }

            callee->schedule();
        }

        std::coroutine_handle<promise_type> callee_coro;
    };

    template <bool kMoveValue>
    struct Awaiter : AwaiterBase {
        decltype(auto) await_resume() const;
    };

  public:
    // ---- 生命周期 ----
    explicit Task(std::coroutine_handle<promise_type> coro) : coro(coro) {
    }

    ~Task() {
        destroy();
    }

    Task(Task &&other) noexcept : coro(other.release()) {
    }

    Task &operator=(Task &&other) {
        if (this == &other) {
            return *this;
        }

        destroy();
        coro = other.release();
        return *this;
    }

    // ---- 等待 ----
    template <typename Self>
    auto operator co_await(this Self &&self) noexcept;

    // ---- 状态查询 ----
    constexpr bool valid() const noexcept {
        return coro != nullptr;
    }

    constexpr bool done() const noexcept {
        return coro.done();
    }

    /// 结果是否已经可取：true 表示现在调用 result() 会拿到值或异常，而不会
    /// 掉进 terminate()。
    ///
    /// 与 done() 的分歧就在失败路径上：co_await 一个失败的 Expected（且 T
    /// 能装下那个错误）时，任务把失败写进结果后冻结在挂起点 —— done() 为
    /// 假，结果却已经可取，这时必须问 settled()。反过来，用右值 result()
    /// 把值搬走之后结果就没了，settled() 又变回假。
    [[nodiscard]] bool settled() const noexcept {
        return coro && coro.promise().settled();
    }

    /// 销毁协程帧（幂等）。用 detach() 而不是 request_cancel()：后者会把
    /// 挂起中的协程重新排进就绪队列，紧接着这里就 destroy()，那条记录会指
    /// 向已释放的帧。
    void destroy() {
        if (auto handle = release()) {
            handle.promise().detach();
            handle.destroy();
        }
    }

    std::coroutine_handle<promise_type> coro;

  private:
    /// 交出帧的所有权而**不**销毁：本对象变成空壳，调用方拿到句柄。
    std::coroutine_handle<promise_type> release() noexcept {
        return std::exchange(coro, nullptr);
    }
};

template <typename T>
template <bool kMoveValue>
decltype(auto) Task<T>::Awaiter<kMoveValue>::await_resume() const {
    if constexpr (std::is_void_v<T>) {
        AwaiterBase::checked_promise().result();
        return;
    } else if constexpr (kMoveValue) {
        return std::move(AwaiterBase::checked_promise()).result();
    } else {
        return (AwaiterBase::checked_promise().result());
    }
}

template <typename T>
template <typename Self>
auto Task<T>::operator co_await(this Self && self) noexcept {
    return Awaiter<!std::is_lvalue_reference_v<Self>>{self.coro};
}

// ---- 契约断言 ----
static_assert(details::Promise<Task<>::promise_type>);
static_assert(details::Future<Task<>>);
static_assert(!std::is_copy_constructible_v<Task<>>);
static_assert(!std::is_copy_assignable_v<Task<>>);
} // namespace netx::core