#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/result.hpp"
#include <cassert>
#include <exception>
#include <system_error>

namespace netx::core {
namespace details {
inline constexpr struct NoWaitAtInitialSuspend {
} no_wait_at_initial_suspend{};
} // namespace details
template <typename T = void>
class [[nodiscard]] Task {
  public:
    class promise_type : public details::CoroHandle, public details::Result<T> {
      private:
        details::CoroHandle *continuation_{nullptr};
        bool owns_continuation_{false}; // 所有权标志
        bool wait_at_initial_suspend_{true};
        /// 收尾动作是否已经做过：跑到 final_suspend，或由取消路径定过结果。
        /// 它和 settled() 不是一回事 —— 冻结在失败 co_await 处的任务
        /// settled() 已经为真，但从没跑过 final_suspend。取消路径靠这一位
        /// 保证同一个任务只定一次结果。
        bool sealed_{false};

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

        /// 登记恢复目标。continuation_ 是私有字段，而写入它的是兄弟嵌套类
        /// （AwaiterBase）与外部封装类（WrappedTask），所以走这个 setter
        /// 而不是直接改字段。
        ///
        /// 约定：一个任务要么挂在父协程下（AwaiterBase 登记父协程，owns
        /// 为 false），要么被包装层托管（WrappedTask 登记 DeleteNodeHandle，
        /// owns 为 true），不会两者都是 —— WrappedTask 的前提就是这个任务
        /// 没有父级协程，没人会 co_await 它。
        ///
        /// 所以这里只登记一次。真被登记第二次，说明上面某个约定已经破了，
        /// 而且不会有事：覆盖掉父协程会让那个父协程永久挂起，覆盖掉 owns
        /// 为 true 的句柄则会泄漏它。
        void set_continuation(details::CoroHandle *continuation,
                              bool owns = false) noexcept {
            assert(continuation_ == nullptr);
            continuation_ = continuation;
            owns_continuation_ = owns;
        }

        /// 通知等待者可以继续了，并解掉它的反向边。
        ///
        /// 等待者可能是父协程（owns=false），也可能是托管的终结句柄
        /// （WrappedTask 的 DeleteNodeHandle，owns=true）。用 wake() 而不是
        /// schedule()：等待者很可能正因为被取消而处于 cancelled 状态，普通
        /// 入队会被守卫挡下，那它就永远等不到这次唤醒。
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
                    // 注意 std::expected 没有从裸 E 构造的构造函数，必须包一层
                    // std::unexpected 才会走它的 unexpected 构造。
                    promise.put_value(std::unexpected{std::move(exp.error())});
                    // 存完就不恢复本协程了：它冻在挂起点，done() 为假，由
                    // Task::settled() 表达"结果已经可取"。
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
        ///
        /// 已经定局的不动 —— 跑完的任务有自己的结果，冻结在失败 co_await
        /// 处的任务有失败结果，都不该被一个后到的 cancel 覆盖。
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
            // 否则只能借异常通道 —— 这份异常不会自己逃出去，调用方是在
            // result() 里重抛时才看到它，和任务体里 throw 的路径一致。
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
    };

    struct AwaiterBase {
        /// 无效帧（空壳 Task）不属于"已完成"：它没有结果可取。这里返回
        /// false，把决定权交给 await_suspend() —— 若返回 true，紧接着的
        /// await_resume() 就会去解引用空 promise。
        bool await_ready() const noexcept {
            return callee_coro && callee_coro.done();
        }

        /// 取结果前先过这里。空壳帧没有 promise 可读，解引用它就是 UB ——
        /// 按"已取消"抛出。co_await 那条路走不到这里（无效输入会被
        /// await_suspend() 冻在挂起点），直接调 await_resume() 的只有
        /// WrappedTask::result()，而取消过的包装任务正是空壳。
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

            // 空壳任务：没有等待边可登记，也没有结果可取。当成"被取消"来
            // 收场 —— 给调用者定一个 Cancelled 结果、叫醒它的等待者，然后让
            // 它冻在挂起点（await_suspend 返回 void 就是挂起自己），语义与
            // on_cancel() 完全一致：什么都没等到的等待，和被取消的等待是同
            // 一件事。
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
                // 父协程已经被取消：这个子任务没必要再起了（它属于"取消之后
                // 才轮到上场"的迟到者）。直接给它一个取消结果，on_cancel()
                // 会顺手把父协程强制唤醒；await_suspend 返回 void 就是挂起
                // 自己，那次唤醒再把父协程放回就绪队列。
                callee->cancelled = true;
                callee->on_cancel();
                return;
            }

            callee->schedule();
        }

        std::coroutine_handle<promise_type> callee_coro;
    };

    explicit Task(std::coroutine_handle<promise_type> coro) : coro(coro) {
    }

    ~Task() {
        destroy();
    }

    // 移动只 exchange 一个 coroutine_handle（本质是指针），不会抛
    Task(Task &&other) noexcept : coro(std::exchange(other.coro, nullptr)) {
    }

    Task &operator=(Task &&other) {
        if (this == &other) {
            return *this;
        }

        destroy();
        std::swap(coro, other.coro);
        return *this;
    }

    auto operator co_await() const & noexcept;
    auto operator co_await() && noexcept;

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
    ///
    /// T 是 void 时同样成立：Result<void> 里也有一位在记"已经交代过结果"
    /// （任务被取消时会带着异常定局），所以它与 done() 的分歧不只是非 void
    /// 任务的事。
    [[nodiscard]] bool settled() const noexcept {
        return coro && coro.promise().settled();
    }

    /// 销毁协程帧（幂等）。用 detach() 而不是 request_cancel()：后者会把
    /// 挂起中的协程重新排进就绪队列，紧接着这里就 destroy()，那条记录会指
    /// 向已释放的帧。
    ///
    /// 对外开放，因为销毁时机不一定在析构点上 —— 例如包装层在任务还没开始
    /// 时就想把它丢掉（WrappedTask::cancel）。
    void destroy() {
        if (auto handle = std::exchange(coro, nullptr)) {
            handle.promise().detach();
            handle.destroy();
        }
    }

  public:
    std::coroutine_handle<promise_type> coro;
};

template <typename T>
auto Task<T>::operator co_await() const & noexcept {
    struct : AwaiterBase {
        decltype(auto) await_resume() const {
            if constexpr (std::is_void_v<T>) {
                AwaiterBase::checked_promise().result();
                return;
            } else {
                return AwaiterBase::checked_promise().result();
            }
        }
    } awaiter{coro};

    return awaiter;
}

template <typename T>
auto Task<T>::operator co_await() && noexcept {
    struct : AwaiterBase {
        decltype(auto) await_resume() const {
            if constexpr (std::is_void_v<T>) {
                std::move(AwaiterBase::checked_promise()).result();
                return;
            } else {
                return std::move(AwaiterBase::checked_promise()).result();
            }
        }
    } awaiter{coro};

    return awaiter;
};
static_assert(details::Promise<Task<>::promise_type>);
static_assert(details::Future<Task<>>);
} // namespace netx::core