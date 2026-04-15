#include "type_list.hpp"
#include <cstddef>

template <auto Key, typename ValueType, size_t Dim = 1> struct Entry
{
	constexpr static auto key = Key;
	constexpr static size_t dim = Dim;
	constexpr static bool is_array = Dim > 1;
	using type = ValueType;
};

template <auto Key, typename ValueType, size_t Dim>
struct Entry<Key, ValueType[Dim]> : Entry<Key, ValueType, Dim>
{
};