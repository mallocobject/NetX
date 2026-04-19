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

namespace details
{
// transform
inline constexpr auto transform_impl =
	[]<typename F, typename... Ts>(
		TypeList<Ts...>, F) -> TypeList<std::invoke_result_t<F, Ts>...>
{ return {}; };

// filter
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

inline constexpr auto filter_impl =
	[]<typename P, typename... Ts>(
		TypeList<Ts...>, P) -> Filter<TypeList<Ts...>, P>::type { return {}; };

template <TL In, TL Out = TypeList<>> struct Unique
{
	using type = Out;
};

// unique
template <typename H, typename... Ts, typename... Es>
struct Unique<TypeList<H, Ts...>, TypeList<Es...>>
{
	using type = typename std::conditional_t<
		(false || ... || std::is_same_v<typename H::type, typename Es::type>),
		Unique<TypeList<Ts...>, TypeList<Es...>>,
		Unique<TypeList<Ts...>, TypeList<Es..., H>>>::type;
};

inline constexpr auto unique_impl = []<TL In>(In) -> typename Unique<In>::type
{ return {}; };

// convert_to
template <template <typename...> typename T>
inline constexpr auto convert_to_impl =
	[]<typename... Ts>(TypeList<TypeConst<Ts>...>) -> TypeConst<T<Ts...>>
{ return {}; };

// pipe
template <typename Fn> struct PipeAdapter : private Fn
{
	consteval PipeAdapter(Fn)
	{
	}

	template <typename... Ts, typename... Args>
		requires std::is_invocable_v<Fn, TypeList<Ts...>, Args...>
	consteval auto operator()(Args... args) const
	{
		return [=, this]<TL In>(In in) consteval
		{ return static_cast<const Fn&>(*this)(in, args...); };
	}

	using Fn::operator();
};

template <TL In, typename Adapter>
consteval auto operator|(In in, Adapter adapter)
{
	return adapter(in);
}
} // namespace details

inline constexpr auto transform = details::PipeAdapter(details::transform_impl);
inline constexpr auto filter = details::PipeAdapter(details::filter_impl);
inline constexpr auto unique = details::PipeAdapter(details::unique_impl);
template <template <typename...> typename T>
inline constexpr auto convert_to =
	details::PipeAdapter(details::convert_to_impl<T>);

inline constexpr auto result =
	type_list<int, char, long, char, short, float, double>;

static_assert(convert_to<std::variant>(
				  unique(transform(filter(result, []<typename T>(TypeConst<T>)
										  { return _v<(sizeof(T) < 4)>; }),
								   []<typename T>(TypeConst<T>) {
									   return _t<std::add_pointer_t<T>>;
								   }))) == _t<std::variant<char*, short*>>);

static_assert(
	(result |
	 filter([]<typename T>(TypeConst<T>) { return _v<(sizeof(T) < 4)>; }) |
	 transform([]<typename T>(TypeConst<T>)
			   { return _t<std::add_pointer_t<T>>; }) |
	 unique() | convert_to<std::variant>()) == _t<std::variant<char*, short*>>);
