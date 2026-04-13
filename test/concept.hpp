#include <functional>
#include <type_traits>
template <typename T>
// concept C = std::is_integral_v<typename T::type>;
concept C = requires(T v) {
	typename T::type;							   // 检查表达式合法性
	requires std::is_integral_v<typename T::type>; // 检查表达式正确性
};

struct Foo
{
	using type = int;
};

// template <typename T>
// concept D = requires { requires C<T>; };

static_assert(C<Foo>);

template <typename T, typename U>
concept same_as = std::is_same_v<T, U> && std::is_same_v<U, T>;

static_assert(!same_as<int, double>);

template <typename Derived, typename Base>
concept derived_form =
	std::is_base_of_v<Base, Derived> &&
	std::is_convertible_v<const volatile Derived*, const volatile Base*>;

template <typename From, typename To>
concept convertible_to =
	std::is_convertible_v<From, To> &&
	requires(std::add_rvalue_reference<From> (&f)()) { static_cast<To>(f()); };

template <typename F, typename... Args>
concept invocable = requires(F&& f, Args&&... args) {
	std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
};