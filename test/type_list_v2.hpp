#include <cstddef>
#include <type_traits>
#include <variant>

template <typename T> struct TypeConst
{
	using type = T;

	template <typename H> consteval bool operator==(TypeConst<H>) const noexcept
	{
		return (std::is_same_v<T, H>);
	}
};
template <typename T> inline constexpr TypeConst<T> _t; // value
template <auto V> inline constexpr auto _v = std::bool_constant<V>::value;

template <typename... Ts> struct TypeList
{
	struct IsTypeList
	{
	};

	using type = TypeList;

	consteval size_t size() const noexcept
	{
		return sizeof...(Ts);
	}

	template <typename... T> consteval auto append(T...) const
	{
		return _t<TypeList<Ts..., T...>>;
	}

	template <typename... T> consteval auto prepend(T...) const
	{
		return _t<TypeList<T..., Ts...>>;
	}

	// template <template <typename...> typename T> consteval auto to() const
	// {
	// 	return _t<T<Ts...>>;
	// }

	template <typename... RHs>
	consteval bool operator==(TypeList<RHs...>) const noexcept
	{
		if constexpr (sizeof...(Ts) != sizeof...(RHs))
		{
			return false;
		}
		else
		{
			return ((std::is_same_v<Ts, RHs>) && ...);
		}
	}
};

template <typename TypeList>
concept TL = requires {
	typename TypeList::IsTypeList;
	typename TypeList::type;
};

template <typename... Ts>
inline constexpr auto type_list = TypeList<TypeConst<Ts>...>{};

// transform
inline constexpr auto transform =
	[]<typename F, typename... Ts>(
		TypeList<Ts...>, F) -> TypeList<std::invoke_result_t<F, Ts>...>
{ return {}; };

inline constexpr auto tl = type_list<int, char, double>;
inline constexpr auto res = transform(tl, []<typename T>(TypeConst<T>)
									  { return _t<std::add_pointer_t<T>>; });
static_assert(res == type_list<int*, char*, double*>);

// filter
namespace details
{
template <TL In, typename P, TL Out = TypeList<>> struct Filter
{
	using type = Out;
};

template <typename P, TL Out, typename H, typename... Ts>
struct Filter<TypeList<H, Ts...>, P, Out>
{
	using type = typename Filter<
		TypeList<Ts...>, P,
		std::conditional_t<P{}(H{}), typename decltype(Out{}.append(H{}))::type,
						   Out>>::type;
};
} // namespace details

inline constexpr auto filter =
	[]<typename P, typename... Ts>(
		TypeList<Ts...>, P) -> details::Filter<TypeList<Ts...>, P>::type
{ return {}; };

inline constexpr auto res2 =
	filter(tl, []<typename T>(TypeConst<T>) { return _v<(sizeof(T) < 4)>; });
static_assert(res2 == type_list<char>);

constexpr auto result = type_list<int, char, long, char, short, float, double>;
static_assert(transform(filter(result, []<typename T>(TypeConst<T>)
							   { return _v<(sizeof(T) < 4)>; }),
						[]<typename T>(TypeConst<T>) {
							return _t<std::add_pointer_t<T>>;
						}) == type_list<char*, char*, short*>);

// unique
// inline constexpr auto unique = []<typename H, typename... Ts, typename...
// Es>( 								   TypeList<H, Ts...>, TypeList<Es...>)
// {
// 	if constexpr (sizeof...(Ts))
// 	{
// 		if constexpr ((false || ... ||
// 					   std::is_same_v<typename H::type, typename Es::type>))
// 		{
// 			return type_list<Es...>;
// 		}
// 		else
// 		{
// 			return type_list<H, Es...>;
// 		}
// 	}

// 	if constexpr ((false || ... ||
// 				   std::is_same_v<typename H::type, typename Es::type>))
// 	{
// 		return unique(type_list<Ts...>, type_list<Es...>);
// 	}
// 	else
// 	{
// 		return unique(type_list<Ts...>, type_list<H, Es...>);
// 	}
// };

namespace details
{
template <TL In, TL Out = TypeList<>> struct Unique
{
	using type = Out;
};

template <typename H, typename... Ts, typename... Es>
struct Unique<TypeList<H, Ts...>, TypeList<Es...>>
{
	using type = typename std::conditional_t<
		(false || ... || std::is_same_v<typename H::type, typename Es::type>),
		Unique<TypeList<Ts...>, TypeList<Es...>>,
		Unique<TypeList<Ts...>, TypeList<Es..., H>>>::type;
};

} // namespace details
static_assert(
	std::is_same_v<typename details::Unique<
					   TypeList<TypeConst<int>, TypeConst<char>,
								TypeConst<long>, TypeConst<short>>>::type,
				   TypeList<TypeConst<int>, TypeConst<char>, TypeConst<long>,
							TypeConst<short>>>);

inline constexpr auto unique = []<TL In>(In) ->
	typename details::Unique<In>::type { return {}; };

constexpr auto res3 = type_list<int, char, long, short, char>;
static_assert(unique(res3) == type_list<int, char, long, short>);

// convert_to
template <template <typename...> typename T>
inline constexpr auto convert_to =
	[]<typename... Ts>(TypeList<TypeConst<Ts>...>) -> TypeConst<T<Ts...>>
{ return {}; };

static_assert(unique(transform(filter(result, []<typename T>(TypeConst<T>)
									  { return _v<(sizeof(T) < 4)>; }),
							   []<typename T>(TypeConst<T>) {
								   return _t<std::add_pointer_t<T>>;
							   })) == type_list<char*, short*>);

static_assert(convert_to<std::variant>(
				  unique(transform(filter(result, []<typename T>(TypeConst<T>)
										  { return _v<(sizeof(T) < 4)>; }),
								   []<typename T>(TypeConst<T>) {
									   return _t<std::add_pointer_t<T>>;
								   }))) == _t<std::variant<char*, short*>>);

template <typename Fn> struct PipeAdapter : private Fn
{
	consteval PipeAdapter(Fn)
	{
	}

	template <TL In, typename... Args>
		requires std::is_invocable_v<Fn, In, Args...>
	consteval auto operator()(Args... args) const
	{
		return [=, this](In in) consteval
		{ return static_cast<const Fn&>(*this)(in, args...); };
	}

	using Fn::operator();
};

template <TL In, typename Adapter>
consteval auto operator|(In in, Adapter adapter)
{
	return adapter(in);
}