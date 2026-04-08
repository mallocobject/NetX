#include <type_traits>
template <typename...> using void_t = void;

template <typename T, typename = void> struct HasTypeMember : std::false_type
{
};

template <typename T>
struct HasTypeMember<T, void_t<typename T::type>> : std::true_type
{
};

static_assert(!HasTypeMember<int>::value);
static_assert(HasTypeMember<std::false_type>::value);