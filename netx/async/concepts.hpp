#ifndef NETX_ASYNC_CONCEPTS_HPP
#define NETX_ASYNC_CONCEPTS_HPP

#include <coroutine>
namespace netx
{
namespace async
{
template <typename A>
concept Awaiter = requires(A a, std::coroutine_handle<> h) {
	{ a.await_ready() } -> std::same_as<bool>;
	{ a.await_suspend(h) };
	{ a.await_resume() };
};

template <typename A>
concept Awaitable = Awaiter<A> || requires(A a) {
	{ a.operator co_await() } -> Awaiter;
};

template <typename Fut>
concept Future = Awaitable<Fut> && requires(Fut fut) {
	// requires !std::default_initializable<Fut>;
	requires std::move_constructible<Fut>;
	typename std::remove_cvref_t<Fut>::promise_type;
	fut.result();
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
} // namespace async
} // namespace netx

#endif