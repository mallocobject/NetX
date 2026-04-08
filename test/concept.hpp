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