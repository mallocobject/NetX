#include <cstddef>
#include <tuple>
template <typename T> struct FunctionTrait;

template <typename Ret, typename... Args> struct FunctionTrait<Ret(Args...)>
{
	using result_type = Ret;
	using args_type = std::tuple<Args...>;
	static constexpr size_t args_size = sizeof...(Args);

	template <size_t I> using arg_type = std::tuple_element_t<I, args_type>;
};

int f(char, double);

int main()
{
	FunctionTrait<decltype(f)>::result_type a;
	FunctionTrait<decltype(f)>::args_type b;
	FunctionTrait<decltype(f)>::arg_type<1> c;
}