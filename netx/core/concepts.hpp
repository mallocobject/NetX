#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace netx::core::details {
template <typename A, typename P = void>
concept Awaiter = requires(A a) {
    { a.await_ready() } -> std::convertible_to<bool>;
    a.await_resume();
} && (std::is_void_v<P> || requires(A a, std::coroutine_handle<P> h) {
                      a.await_suspend(h);
                  });

template <typename A, typename P = void>
concept Awaitable = Awaiter<A, P> || requires(A a) {
    { a.operator co_await() } -> Awaiter<P>;
};

template <typename F>
concept Future = requires(F f) {
    requires !std::default_initializable<F>;
    requires !std::copy_constructible<F>;
    requires std::move_constructible<F>;
    typename std::decay_t<F>::promise_type;
};

template <typename P>
concept Promise = requires(P p) {
    { p.get_return_object() } -> Future;
    { p.initial_suspend() } -> Awaitable<P>;
    { p.final_suspend() } noexcept -> Awaitable<P>;
    p.unhandled_exception();
    requires(
        requires(int v) { p.return_value(v); } ||
        requires { p.return_void(); });
};
// Value type held by a promise: void means "no return value", otherwise the
// type must be movable (move-constructible + assignable + swappable) after
// stripping cv/ref, because Result writes into its storage both with
// placement new and with assignment.
template <typename T>
concept ResultValue = std::is_void_v<std::remove_cvref_t<T>> ||
                      std::movable<std::remove_cvref_t<T>>;
} // namespace netx::core::details