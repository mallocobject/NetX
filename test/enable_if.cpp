#include <type_traits>
template <bool, typename T = void> struct EnableIf
{
};

template <typename T> struct EnableIf<true, T>
{
	using type = T;
};

template <typename T>
EnableIf<std::is_arithmetic_v<T>, bool> num_eq(T lhs, T rhs)
{
	if constexpr (std::is_integral_v<T>)
	{
		return lhs == rhs;
	}
	else
	{
		return false;
	}
}