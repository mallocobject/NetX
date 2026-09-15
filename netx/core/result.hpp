#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/non_void_helper.hpp"
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace netx::core::details {
enum class State : std::uint8_t {
    kEmpty,
    kValue,
    kException,
};

template <ResultValue T = void>
class Result {
  private:
    using enum State;

  private:
    union {
        T val_;
    };
    std::exception_ptr exception_{nullptr};
    State state_{kEmpty};

  public:
    // The implicitly-declared default constructor is deleted when the anonymous
    // union's variant member has a non-trivial default constructor
    // ([class.default.ctor]/3). A user-provided one is fine instead: no
    // initialization is performed for anonymous union members
    // ([class.base.init]/9), which is exactly what state_ == kEmpty means.
    Result() noexcept {
    }

    ~Result() {
        if (state_ == kValue) {
            val_.~T();
        }
    }

    template <typename... Args>
    void return_value(Args &&...args) {
        new (std::addressof(val_)) T(std::forward<Args>(args)...);
        state_ = kValue;
    }

    void return_value(T value) {
        new (std::addressof(val_)) T(std::move(value));
        state_ = kValue;
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
        state_ = kException;
    }

    /// 直接登记一个异常（result() 会重抛）。
    ///
    /// 给那些"不经过 throw"的路径用 —— 比如取消：T 装不下 error_code 时，
    /// 取消结果只能借异常通道送出去。已有的值会被丢弃，保持"kValue 蕴含
    /// val_ 活着"这个不变式。
    void put_exception(std::exception_ptr e) noexcept {
        if (state_ == kValue) {
            val_.~T();
            state_ = kEmpty;
        }
        exception_ = std::move(e);
        state_ = kException;
    }

    template <typename... Args>
    void put_value(Args &&...args) {
        if (state_ == kValue) {
            val_ = T(std::forward<Args>(args)...);
        } else {
            new (std::addressof(val_)) T(std::forward<Args>(args)...);
            state_ = kValue;
        }
    }

    template <typename Self>
    decltype(auto) result(this Self &&self) {
        if (self.state_ == State::kException) {
            std::rethrow_exception(self.exception_);
        }
        if (self.state_ != State::kValue) {
            std::terminate(); // HACK
        }

        if constexpr (std::is_lvalue_reference_v<Self>) {
            return (self.val_); // (val) -> &
        } else {
            T ret = std::move(self.val_);
            self.val_.~T();
            self.state_ = kEmpty; // value moved out and destroyed: keep the
                                  // invariant "kValue implies a live object"
            return ret;
        }
    }

    /// 结果是否已经定局（值已写入或异常已捕获），即现在调用 result() 会拿到
    /// 值或异常，而不会掉进 terminate()。
    ///
    /// 它与"协程是否跑到 final_suspend"不是一回事：co_await 一个失败的
    /// Expected 时，任务把失败写进结果后就冻结在挂起点，此时结果已经可取。
    /// 反过来，右值重载的 result() 会把值搬走并清回 kEmpty，此后又变回假。
    [[nodiscard]] constexpr bool settled() const noexcept {
        return state_ != kEmpty;
    }
};

template <>
class Result<void> {
  private:
    std::exception_ptr exception_{nullptr};
    /// 结果是否已经定局。void 没有值可查，"成功了"与"还没跑"只能靠这一位
    /// 区分，所以除了 exception_ 还得要它。
    bool settled_{false};

  public:
    // 非 const：要置位（Result<T> 的 return_value 同样是非 const）
    void return_void() noexcept {
        settled_ = true;
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
        settled_ = true;
    }

    /// 同 Result<T>::put_exception —— 取消结果借异常通道送出
    void put_exception(std::exception_ptr e) noexcept {
        exception_ = std::move(e);
        settled_ = true;
    }

    /// 同 Result<T>::settled：结果是否已经定局，即现在调用 result() 会拿到
    /// 值或异常，而不会掉进 terminate()。
    [[nodiscard]] constexpr bool settled() const noexcept {
        return settled_;
    }

    auto result() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return NonVoidHelper<>{};
    }
};

template <typename T>
struct Result<const T> : Result<T> {};

template <typename T>
struct Result<T &> : Result<std::reference_wrapper<T>> {};

template <typename T>
struct Result<T &&> : Result<T> {};
} // namespace netx::core::details