#include <type_traits>
struct Test
{
	void on_init()
	{
	}
};

template <typename...> using void_t = void;

template <typename T, typename = void> struct HasInit : std::false_type
{
};

template <typename T>
struct HasInit<T, void_t<decltype(std::declval<T>().on_init())>>
	: std::true_type
{
};

static_assert(HasInit<Test>::value);
static_assert(!HasInit<int>::value);