#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>

template <typename T>
	requires requires(T a, T b) {
		{ a > b } -> std::convertible_to<bool>;
	}
constexpr T max(T a, T b)
{
	return a > b ? a : b;
}

static_assert(max(1, 2) == 2);

// 使用递归代替迭代，使用特化代替分支
template <size_t N> struct Fibonacci
{
	constexpr inline static size_t value =
		Fibonacci<N - 1>::value + Fibonacci<N - 2>::value;
};

template <> struct Fibonacci<0>
{
	constexpr inline static size_t value = 0;
};

template <> struct Fibonacci<1>
{
	constexpr inline static size_t value = 1;
};

static_assert(Fibonacci<10>::value == 55);

template <typename T, size_t I, size_t... Is> struct Array
{
	using type = std::array<typename Array<T, Is...>::type, I>;
};

template <typename T, size_t I> struct Array<T, I>
{
	using type = std::array<T, I>;
};

static_assert(std::is_same_v<Array<int, 5, 4, 3>::type,
							 std::array<std::array<std::array<int, 3>, 4>, 5>>);
