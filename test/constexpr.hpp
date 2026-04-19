#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <type_traits>

inline constexpr double PI = 3.1415926535;
constexpr double area = PI * 2.0 * 2.0;

constexpr size_t MAX_LEN = 32;
inline int8_t buffer[MAX_LEN];

// 使用constexpr修饰的对象，也可以看作是const的

template <typename T>
inline constexpr bool is_integral_v = std::is_integral_v<T>;
static_assert(is_integral_v<int>);
static_assert(!is_integral_v<void>);

constexpr bool func()
{
	return true;
}

template <char c> constexpr bool is_digit = (c >= '0' && c <= '9');
static_assert(!is_digit<'c'>);
static_assert(is_digit<'0'>);

// 变量模板特化，what the fuck
template <size_t N>
inline constexpr size_t fibonacci = fibonacci<N - 1> + fibonacci<N - 2>;
template <> inline constexpr size_t fibonacci<0> = 0;
template <> inline constexpr size_t fibonacci<1> = 1;
static_assert(fibonacci<10> == 55);

// 使用constexpr定义的变量被要求其表达式能够在编译时进行求值，它们需要拥有常量的属性。
// constinit定义的变量同样要求在编译时能够对表达式求值，但仍可保留可变的属性。

// 折叠表达式空包
// && -> true
// || -> false
// , -> void()

// 使用constexpr修饰的函数声明意味着它是inline的

constexpr int min(std::initializer_list<int> xs)
{
	int low = std::numeric_limits<int>::max();
	for (int x : xs)
	{
		low = std::min(low, x);
	}
	return low;
}

static_assert(min({1, 3, 2, 4}) == 1);

// 使用constexpr修饰仅表达一个函数是否可能被编译时求值，而使用consteval修饰时则要求函数必须能够被编译时求值

consteval int compiler_min(std::initializer_list<int> xs)
{
	int low = std::numeric_limits<int>::max();
	for (int x : xs)
	{
		low = std::min(low, x);
	}
	return low;
}

static_assert(min({1, 3, 2, 4}) == 1);

struct Shape
{
	virtual ~Shape() = default;
	virtual double get_area() const noexcept = 0;
};

struct Circle : Shape
{
	constexpr Circle(double r) : r_(r)
	{
	}

	constexpr double get_area() const noexcept override
	{
		return std::numbers::pi * r_ * r_;
	}

  private:
	double r_;
};

constexpr double square(double b)
{
	if (std::is_constant_evaluated())
	{
		return b * b;
	}
	else
	{
		return b * b;
	}
}

static_assert(square(10.0) == 100.0);

inline int n = 3;
inline double mucho = square(n);

constexpr size_t collatz_time(size_t n)
{
	size_t step = 0;
	for (; n > 1; ++step)
	{
		n = (n % 2 == 0) ? n / 2 : 3 * n + 1;
	}
	return step;
}

constexpr size_t sum_collatz_time(size_t n)
{
	int sum = 0;
	for (size_t i = 1; i <= n; i++)
	{
		sum += collatz_time(i);
	}
	return sum;
}

// inline int res =
// 	sum_collatz_time(10000); // Constexpr evaluation hit maximum step limit

inline constexpr struct CountedPolicy
{
	bool a = true;
	bool b = true;
} default_counted_policy;

template <CountedPolicy policy = default_counted_policy> struct Counted
{
	constexpr static bool a = policy.a;
	constexpr static bool b = policy.b;
};

using CountedOnlyA = Counted<{.b = false}>;

constexpr CountedOnlyA tmp;
static_assert(tmp.a && !tmp.b);