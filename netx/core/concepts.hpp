#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>
namespace netx
{
namespace core
{
template <typename A>
concept Awaiter = requires(A a, std::coroutine_handle<> h) {
	{ a.await_ready() } -> std::convertible_to<bool>;
	a.await_suspend(h);
	a.await_resume();
};

template <typename A>
concept Awaitable = Awaiter<A> || requires(A a) {
	{ a.operator co_await() } -> Awaiter;
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
	{ p.initial_suspend() } -> Awaitable;
	{ p.final_suspend() } noexcept -> Awaitable;
	p.unhandled_exception();
	requires(
		requires(int v) { p.return_value(v); } ||
		requires { p.return_void(); });
};
} // namespace core
} // namespace netx