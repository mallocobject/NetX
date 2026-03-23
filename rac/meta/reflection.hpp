#ifndef RAC_NET_REFLECTION_HPP
#define RAC_NET_REFLECTION_HPP

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>

namespace rac
{
struct AnyType
{
	template <typename Type> constexpr operator Type() const noexcept;
};

template <typename T, typename... Args>
concept AggregateInit = requires { T{Args{}...}; };

template <typename T> constexpr std::size_t count_fields()
{
	if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType, AnyType,
								AnyType, AnyType, AnyType, AnyType, AnyType>)
		return 10;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType, AnyType, AnyType,
									 AnyType>)
		return 9;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType, AnyType, AnyType>)
		return 8;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType, AnyType>)
		return 7;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType>)
		return 6;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType>)
		return 5;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType>)
		return 4;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType>)
		return 3;
	else if constexpr (AggregateInit<T, AnyType, AnyType>)
		return 2;
	else if constexpr (AggregateInit<T, AnyType>)
		return 1;
	else
		return 0;
}

template <typename T> constexpr auto struct_to_tuple(T& obj)
{
	using CleanT = std::remove_cvref_t<T>;
	constexpr std::size_t N = count_fields<CleanT>();

	if constexpr (N == 10)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
	}
	else if constexpr (N == 9)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8);
	}
	else if constexpr (N == 8)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6, a7] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6, a7);
	}
	else if constexpr (N == 7)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6);
	}
	else if constexpr (N == 6)
	{
		auto& [a0, a1, a2, a3, a4, a5] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5);
	}
	else if constexpr (N == 5)
	{
		auto& [a0, a1, a2, a3, a4] = obj;
		return std::tie(a0, a1, a2, a3, a4);
	}
	else if constexpr (N == 4)
	{
		auto& [a0, a1, a2, a3] = obj;
		return std::tie(a0, a1, a2, a3);
	}
	else if constexpr (N == 3)
	{
		auto& [a0, a1, a2] = obj;
		return std::tie(a0, a1, a2);
	}
	else if constexpr (N == 2)
	{
		auto& [a0, a1] = obj;
		return std::tie(a0, a1);
	}
	else if constexpr (N == 1)
	{
		auto& [a0] = obj;
		return std::tie(a0);
	}
	else
	{
		return std::tie();
	}
}

template <typename T>
constexpr bool is_custom_struct_v =
	std::is_aggregate_v<T> && !std::is_arithmetic_v<T> &&
	!std::is_same_v<T, std::string>;

} // namespace rac

#endif