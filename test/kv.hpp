#include "type_list.hpp"
#include <algorithm>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

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

template <typename E>
concept KVEntry = requires {
	typename E::type;
	requires std::is_standard_layout_v<typename E::type>;
	requires std::is_trivial_v<typename E::type>;
	{ E::key } -> std::convertible_to<size_t>;
	{ E::dim } -> std::convertible_to<size_t>;
};

static_assert(KVEntry<Entry<0, char[10]>>);

template <TL Es = TypeList<>, TL Gs = TypeList<>> struct GroupEntriesTrait : Gs
{
};

template <KVEntry H, KVEntry... Ts, TL Gs>
struct GroupEntriesTrait<TypeList<H, Ts...>, Gs>
{
	template <KVEntry E>
	using P = std::bool_constant<(
		(H::dim == E::dim) &&
		(sizeof(typename H::type) == sizeof(typename E::type)) &&
		(alignof(typename H::type) == alignof(typename E::type)))>;

	using Group = Partition<TypeList<H, Ts...>, P>;
	using type = typename GroupEntriesTrait<
		typename Group::Rest,
		typename Gs::template append<typename Group::Satisfied>>::type;
};

template <TL Es>
using GroupEntriesTrait_t = typename GroupEntriesTrait<Es>::type;

static_assert(std::is_same_v<
			  GroupEntriesTrait_t<TypeList<Entry<0, int>, Entry<1, char>,
										   Entry<2, float>, Entry<3, double>>>,
			  TypeList<TypeList<Entry<0, int>, Entry<2, float>>,
					   TypeList<Entry<1, char>>, TypeList<Entry<3, double>>>>);

struct Region
{
	virtual bool get_data(size_t nth_data, void* out, size_t len) noexcept = 0;
	virtual bool set_data(size_t nth_data, const void* value,
						  size_t len) noexcept = 0;
	virtual ~Region() = default;
};

template <KVEntry EH, KVEntry... ET> struct GenericRegion : Region
{
	using type = GenericRegion;

	bool get_data(size_t nth_data, void* out, size_t len) noexcept override
	{
		if (nth_data >= number_of_entries) [[unlikely]]
		{
			return false;
		}
		std::copy_n(data[nth_data], std::min(len, max_size),
					reinterpret_cast<char*>(out));
		return true;
	}

	bool set_data(size_t nth_data, const void* value,
				  size_t len) noexcept override
	{
		if (nth_data >= number_of_entries) [[unlikely]]
		{
			return false;
		}
		std::copy_n(reinterpret_cast<const char*>(value),
					std::min(len, max_size), data[nth_data]);
		return true;
	}

  private:
	constexpr static size_t number_of_entries =
		sizeof...(ET) + 1; // include EH, <EH, ET...>
	constexpr static size_t max_size =
		std::max(alignof(typename EH::type), sizeof(typename EH::type)) *
		EH::dim;
	char data[number_of_entries][max_size];
};

template <typename... R> struct Regions
{
	template <typename... Args>
	explicit Regions(Args&&... args) : regions(std::forward<Args>(args)...)
	{
	}

	template <size_t I, typename Op>
	bool for_data(Op&& op, size_t index) noexcept
	{
		size_t region_idx = index >> 16;
		size_t nth_data = index & 0xFFFF;
		if (region_idx == I)
		{
			return op(std::get<I>(regions), nth_data);
		}
		return false;
	}

	template <typename Op, size_t... Is>
	bool for_data(std::index_sequence<Is...>, Op&& op, size_t index) noexcept
	{
		return (for_data<Is>(std::forward<Op>(op), index) || ...);
	}

	bool get_data(size_t index, void* out, size_t len) noexcept
	{
		auto op = [out, len](auto& region, size_t nth_data)
		{ return region.get_data(nth_data, out, len); };
		return for_data(std::make_index_sequence<sizeof...(R)>{}, op, index);
	}

	bool set_data(size_t index, const void* value, size_t len) noexcept
	{
		auto op = [value, len](auto& region, size_t nth_data)
		{ return region.set_data(nth_data, value, len); };
		return for_data(std::make_index_sequence<sizeof...(R)>{}, op, index);
	}

  private:
	std::tuple<R...> regions;
};

template <typename... Args>
explicit Regions(Args&&... args) -> Regions<std::decay_t<Args>...>;

template <TL Gs> struct GenericRegionTrait
{
	template <TL G> using ToRegion = typename G::template to<GenericRegion>;
	using type = Map_t<Gs, ToRegion>;
};

template <TL Gs>
using GenericRegionTrait_t = typename GenericRegionTrait<Gs>::type;

template <TL Gs>
using RegionClass = typename GenericRegionTrait_t<Gs>::template to<Regions>;

template <typename... Indexes> struct Indexer
{
	size_t key_to_id[sizeof...(Indexes)];
	std::bitset<sizeof...(Indexes)> mask;
	constexpr Indexer()
	{
		constexpr size_t index_size = sizeof...(Indexes);
		static_assert(((Indexes::key < index_size) && ...));
		((key_to_id[Indexes::key] = Indexes::id), ...);
	}
};

template <TL Gs> struct GroupIndexTrait
{
	template <size_t GroupIdx = 0, size_t InnerIdx = 0, TL Res = TypeList<>>
	struct Index
	{
		constexpr static size_t group_idx = GroupIdx;
		constexpr static size_t inner_idx = InnerIdx;
		using Result = Res;
	};

	template <typename Acc, TL G> struct AddGroup
	{
		constexpr static size_t group_idx = Acc::group_idx;
		template <typename _Acc, KVEntry E> struct AddKey
		{
			constexpr static size_t inner_idx = _Acc::inner_idx;
			struct KeyWithIndex
			{
				constexpr static size_t key = E::key;
				constexpr static size_t id = (group_idx << 16) | inner_idx;
			};

			using Result = typename _Acc::Result::template append<KeyWithIndex>;
			using type = Index<group_idx, inner_idx + 1, Result>;
		};

		using Result = Acc::Result;
		using type = Fold_t<G, Index<group_idx + 1, 0, Result>, AddKey>;
	};

	using type = typename Fold_t<Gs, Index<>, AddGroup>::Result;
};

template <TL Gs> using GroupIndexTrait_t = typename GroupIndexTrait<Gs>::type;

template <TL Gs>
using IndexerClass = typename GroupIndexTrait_t<Gs>::template to<Indexer>;

template <TL Es> struct Datatable
{
	using Group = GroupEntriesTrait_t<Es>;

	bool get_data(size_t key, void* out, size_t len = -1) noexcept
	{
		if (key >= Es::size || !indexer.mask[key])
		{
			return false;
		}
		return regions.get_data(indexer.key_to_id[key], out, len);
	}

	bool set_data(size_t key, const void* value, size_t len = -1) noexcept
	{
		if (key >= Es::size)
		{
			return false;
		}

		if (regions.set_data(indexer.key_to_id[key], value, len))
		{
			indexer.mask.set(key);
			return true;
		}
		return false;
	}

  private:
	RegionClass<Group> regions;
	IndexerClass<Group> indexer;
};
