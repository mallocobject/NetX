#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>
constexpr auto hello = "hello,"
					   "world";

template <typename T>
constexpr auto str_length = str_length<std::remove_cvref_t<T>>;
template <std::size_t N> constexpr std::size_t str_length<char[N]> = N - 1;
template <std::size_t N>
constexpr std::size_t str_length<std::array<char, N>> = N - 1;

static_assert(str_length<decltype("hello")> == 5);

// template <typename T> struct Str : Str<std::remove_cvref_t<T>>
// {
// };

// template <std::size_t N> struct Str<char[N]>
// {
// 	constexpr static size_t length = N - 1;
// };

// template <std::size_t N> struct Str<std::array<char, N>>
// {
// 	constexpr static size_t length = N - 1;
// };

// static_assert(Str<decltype("hello")>::length == 5);

template <typename DelimType, std::size_t N> struct JoinHelper
{
	consteval JoinHelper(DelimType delimiter) : delimiter(delimiter)
	{
	}

	template <typename _JoinHelper, typename STR>
		requires std::is_same_v<std::remove_cvref_t<_JoinHelper>, JoinHelper>
	friend consteval decltype(auto) operator+(_JoinHelper&& self, STR&& str)
	{
		self.pstr = std::copy_n(std::begin(str), str_length<STR>, self.pstr);
		if (std::end(self.joined_str) - self.pstr > str_length<DelimType>)
		{
			self.pstr = std::copy_n(std::begin(self.delimiter),
									str_length<DelimType>, self.pstr);
		}
		return std::forward<_JoinHelper>(self);
	}

	std::array<char, N + 1> joined_str{};
	DelimType delimiter;
	decltype(joined_str.begin()) pstr = joined_str.begin();
};

template <typename DelimType, typename... STRs>
consteval auto join(DelimType&& delimiter, STRs&&... strs)
{
	constexpr std::size_t count = sizeof...(STRs);
	constexpr std::size_t len =
		(str_length<STRs> + ... + 0) + (count - 1) * str_length<DelimType>;
	return (JoinHelper<DelimType, len>{std::forward<DelimType>(delimiter)} +
			... + std::forward<STRs>(strs))
		.joined_str;
}

constexpr auto one_two_three = join(", ", "one", "two", "three");
static_assert(one_two_three ==
			  std::array<char, str_length<decltype(one_two_three)> + 1>{
				  "one, two, three"});

template <typename... STRs> consteval auto concat(STRs&&... strs)
{
	return join("", std::forward<STRs>(strs)...);
}

constexpr auto one_two_three_four = concat("one", "two", "three", "four");
static_assert(one_two_three_four ==
			  std::array<char, str_length<decltype(one_two_three_four)> + 1>{
				  "onetwothreefour"});