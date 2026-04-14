// #include <type_traits>
// template <typename... Ts> struct TypeList
// {
// };

// // using List = TypeList<int, double>;

// // 用类型携带值，值承载于类型，即值可以映射成类型从而存储到TypeList中

// using One = std::integral_constant<int, 1>;
// constexpr auto one = One::value;
// using Two = std::integral_constant<int, 2>;
// constexpr auto two = Two::value;

// using List = TypeList<One, Two>;

#include <cstddef>
#include <tuple>
#include <type_traits>
template <typename... Ts> struct TypeList
{
	struct IsTypeList
	{
	};

	using type = TypeList;
	constexpr inline static size_t size = sizeof...(Ts);

	template <typename... T> using append = TypeList<Ts..., T...>;
	template <typename... T> using prepend = TypeList<T..., Ts...>;
	template <template <typename...> typename T>
	using to = T<Ts...>; // 接收模板本身
};

template <typename TypeList>
concept TL = requires {
	typename TypeList::IsTypeList;
	typename TypeList::type;
};

using AList = TypeList<int, char>;
static_assert(TL<AList>);
static_assert(AList::size == 2);
static_assert(
	std::is_same_v<AList::prepend<double>, TypeList<double, int, char>>);
static_assert(
	std::is_same_v<AList::append<double>, TypeList<int, char, double>>);
static_assert(std::is_same_v<AList::to<std::tuple>, std::tuple<int, char>>);

// 如果一个函数的输入或者输出为函数，那么这个函数就是高阶函数

// map
template <TL In, template <typename> typename F> struct Map;

template <template <typename> typename F, typename... Ts>
struct Map<TypeList<Ts...>, F> : TypeList<typename F<Ts>::type...>
{
};

template <TL In, template <typename> typename F>
using Map_t = typename Map<In, F>::type;

using LongList = TypeList<char, float, double, int, char>;
static_assert(std::is_same_v<Map_t<LongList, std::add_pointer>,
							 TypeList<char*, float*, double*, int*, char*>>);

// filter
template <TL In, template <typename> typename P, TL Out = TypeList<>>
struct Filter : Out
{
};

// typename 告诉编译器：结果是类型
// template 告诉编译器：append是模板，后续 <H> 是模板参数
template <template <typename> typename P, TL Out, typename H, typename... Ts>
struct Filter<TypeList<H, Ts...>, P, Out>
	: std::conditional_t<
		  P<H>::value,
		  Filter<TypeList<Ts...>, P, typename Out::template append<H>>,
		  Filter<TypeList<Ts...>, P, Out>>
{
};

template <TL In, template <typename> typename P>
using Filter_t = typename Filter<In, P>::type;

template <typename T> using SizeLess4 = std::bool_constant<(sizeof(T) < 4)>;
static_assert(
	std::is_same_v<Filter_t<LongList, SizeLess4>, TypeList<char, char>>);

// fold
// template <typename T> struct Return
// {
// 	using type = T;
// };

template <TL In, typename Init, template <typename, typename> typename Op>
struct Fold
{
	using type = Init;
};

template <typename Sum, template <typename, typename> typename Op, typename H,
		  typename... Ts>
struct Fold<TypeList<H, Ts...>, Sum, Op>
	: Fold<TypeList<Ts...>, typename Op<Sum, H>::type, Op>
{
};

template <TL In, typename Init, template <typename, typename> typename Op>
using Fold_t = typename Fold<In, Init, Op>::type;

template <typename Sum, typename E>
using Add = std::integral_constant<size_t, Sum::value + E::value>;

static_assert(Fold_t<TypeList<std::integral_constant<size_t, 1>,
							  std::integral_constant<size_t, 2>>,
					 std::integral_constant<size_t, 0>, Add>::value == 3);

// concat
template <TL... In> struct Concat;
template <TL... In> using Concat_t = typename Concat<In...>::type;

template <> struct Concat<> : TypeList<>
{
};

template <TL In> struct Concat<In> : In
{
};

template <TL In1, TL In2, TL... In>
struct Concat<In1, In2, In...> : Concat_t<Concat_t<In1, In2>, In...>
{
};

template <typename... Ts1, typename... Ts2>
struct Concat<TypeList<Ts1...>, TypeList<Ts2...>> : TypeList<Ts1..., Ts2...>
{
};

static_assert(
	std::is_same_v<Concat_t<TypeList<char, double>, TypeList<int, float>>,
				   TypeList<char, double, int, float>>);

// elem
// template <TL In, typename E> struct Elem
// {
// 	template <typename Sum, typename T>
// 	using FindE = std::conditional_t<Sum::value, Sum, std::is_same<T, E>>;
// 	using Found = Fold_t<In, std::false_type, FindE>;

// 	constexpr inline static bool value = Found::value;
// };

template <TL In, typename E> struct Elem : std::false_type
{
};

template <typename E, typename... Ts>
struct Elem<TypeList<Ts...>, E>
	: std::bool_constant<(false || ... || std::is_same_v<E, Ts>)>
{
};

static_assert(Elem<LongList, char>::value);
static_assert(!Elem<LongList, long long>::value);

// unique
// template <TL In, TL Out = TypeList<>> struct Unique : Out
// {
// };

// template <TL Out> struct Unique<TypeList<>, Out> : Out
// {
// };

// template <TL Out, typename H, typename... Ts>
// struct Unique<TypeList<H, Ts...>, Out>
// 	: Unique<TypeList<Ts...>,
// 			 std::conditional_t<Elem<Out, H>::value, Out,
// 								typename Out::template append<H>>>
// {
// };

template <TL In> struct Unique
{
	template <TL Acc, typename E>
	using Append = std::conditional_t<Elem<Acc, E>::value, Acc,
									  typename Acc::template append<E>>;

	using type = Fold_t<In, TypeList<>, Append>;
};

template <TL In> using Unique_t = Unique<In>::type;

static_assert(
	std::is_same_v<Unique_t<LongList>, TypeList<char, float, double, int>>);

// partition
// template <TL In, template <typename> typename P> struct Partition
// {
// 	template <typename Args> using NotP = std::bool_constant<!P<Args>::value>;

// 	using Satisfied = Filter_t<In, P>;
// 	using Rest = Filter_t<In, NotP>;
// };

template <TL In, template <typename> typename P, TL In1 = TypeList<>,
		  TL In2 = TypeList<>>
struct Partition;

template <template <typename> typename P, TL In1, TL In2>
struct Partition<TypeList<>, P, In1, In2>
{
	using Satisfied = In1;
	using Rest = In2;
};

template <template <typename> typename P, TL In1, TL In2, typename H,
		  typename... Ts>
struct Partition<TypeList<H, Ts...>, P, In1, In2>
	: Partition<TypeList<Ts...>, P,
				std::conditional_t<P<H>::value,
								   typename In1::template append<H>, In1>,
				std::conditional_t<P<H>::value, In2,
								   typename In2::template append<H>>>
{
};

template <typename T> using SizeLess4 = std::bool_constant<(sizeof(T) < 4)>;
static_assert(std::is_same_v<Partition<LongList, SizeLess4>::Satisfied,
							 TypeList<char, char>>);

static_assert(std::is_same_v<Partition<LongList, SizeLess4>::Rest,
							 TypeList<float, double, int>>);

// sort
template <TL In, template <typename, typename> typename Cmp>
struct Sort : TypeList<>
{
};

template <template <typename, typename> typename Cmp, typename H,
		  typename... Ts>
struct Sort<TypeList<H, Ts...>, Cmp>
{
	template <typename T> using LT = Cmp<T, H>;

	using P = Partition<TypeList<Ts...>, LT>;
	using LeftSorted = typename Sort<typename P::Satisfied, Cmp>::type;
	using RightSorted = typename Sort<typename P::Rest, Cmp>::type;

	using type = Concat_t<typename LeftSorted::template append<H>, RightSorted>;
};

template <typename L, typename R>
using SizeCmp = std::bool_constant<(sizeof(L) < sizeof(R))>;
static_assert(std::is_same_v<Sort<LongList, SizeCmp>::type,
							 TypeList<char, char, float, int, double>>);