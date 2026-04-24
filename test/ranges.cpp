#include <algorithm>
#include <concepts>
#include <iostream>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

inline auto res = std::views::iota(1) |
				  std::views::transform([](auto n) { return n * n; }) |
				  std::views::filter([](auto n) { return n % 2 == 0; }) |
				  std::views::take_while([](auto n) { return n < 10000; });

template <typename R>
concept range = requires(R& r) {
	std::ranges::begin(r);
	std::ranges::end(r);
};

template <typename R> inline constexpr bool enable_borrowed_range = false;
template <>
inline constexpr bool enable_borrowed_range<std::string_view> = true;

template <typename R>
concept borrowed_range =
	range<R> && (std::is_lvalue_reference_v<R> ||
				 enable_borrowed_range<std::remove_cvref_t<R>>);

template <borrowed_range R> void f(R&& r)
{
}

template <typename R>
concept sized_range = range<R> && requires(R& r) { std::ranges::size(r); };

static_assert(sized_range<std::vector<int>>);

template <typename R>
concept view = range<R> && std::movable<R> && std::default_initializable<R> &&
			   std::ranges::enable_view<R>;
// 标准库中仅有string_view, span是view

static_assert(!std::ranges::view<std::vector<int>>);
static_assert(!std::ranges::view<std::string>);
static_assert(std::ranges::view<std::string_view>);
static_assert(std::ranges::view<std::span<int>>);

int main()
{
	// f(std::vector<int>{1, 2, 3, 4});
	std::vector vec{1, 2, 3, 4};
	f(vec);

	f(std::string_view{"1234"});

	std::vector v{1,2,3,4,5};
	auto iter = std::ranges::find(v, 3);

	std::cout << *iter << std::endl;

	std::vector<int> f{};
	auto iter2 = std::ranges::find(f, 3);

}