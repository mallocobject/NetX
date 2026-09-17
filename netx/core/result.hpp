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
  public:
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
    [[nodiscard]] constexpr bool settled() const noexcept {
        return state_ != kEmpty;
    }

  private:
    using enum State;

    union {
        T val_;
    };
    std::exception_ptr exception_{nullptr};
    State state_{kEmpty};
};

template <>
class Result<void> {
  public:
    void return_void() noexcept {
        settled_ = true;
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
        settled_ = true;
    }

    void put_exception(std::exception_ptr e) noexcept {
        exception_ = std::move(e);
        settled_ = true;
    }

    [[nodiscard]] constexpr bool settled() const noexcept {
        return settled_;
    }

    auto result() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return NonVoidHelper<>{};
    }

  private:
    std::exception_ptr exception_{nullptr};
    bool settled_{false}; // 区分是否有运行结局
};

template <typename T>
struct Result<const T> : Result<T> {};

template <typename T>
struct Result<T &> : Result<std::reference_wrapper<T>> {};

template <typename T>
struct Result<T &&> : Result<T> {};
} // namespace netx::core::details