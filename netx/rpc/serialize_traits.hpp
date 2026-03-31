#ifndef NETX_RPC_SERIALIZE_TRAITS_HPP
#define NETX_RPC_SERIALIZE_TRAITS_HPP

#include "netx/meta/endian_helper.hpp"
#include "netx/net/buffer.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
namespace netx
{
namespace rpc
{
namespace net = netx::net;

struct AnyType
{
	template <typename Type> constexpr operator Type() const noexcept;
};

template <typename T, typename... Args>
concept AggregateInit = requires { T{Args{}...}; };

template <typename T> constexpr std::size_t count_fields()
{
	if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType, AnyType,
								AnyType, AnyType, AnyType, AnyType, AnyType>)
		return 10;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType, AnyType, AnyType,
									 AnyType>)
		return 9;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType, AnyType, AnyType>)
		return 8;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType, AnyType>)
		return 7;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType, AnyType>)
		return 6;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType,
									 AnyType>)
		return 5;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType, AnyType>)
		return 4;
	else if constexpr (AggregateInit<T, AnyType, AnyType, AnyType>)
		return 3;
	else if constexpr (AggregateInit<T, AnyType, AnyType>)
		return 2;
	else if constexpr (AggregateInit<T, AnyType>)
		return 1;
	else
		return 0;
}

template <typename T> constexpr auto struct_to_tuple(T& obj)
{
	using CleanT = std::remove_cvref_t<T>;
	constexpr std::size_t N = count_fields<CleanT>();

	if constexpr (N == 10)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
	}
	else if constexpr (N == 9)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8);
	}
	else if constexpr (N == 8)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6, a7] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6, a7);
	}
	else if constexpr (N == 7)
	{
		auto& [a0, a1, a2, a3, a4, a5, a6] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5, a6);
	}
	else if constexpr (N == 6)
	{
		auto& [a0, a1, a2, a3, a4, a5] = obj;
		return std::tie(a0, a1, a2, a3, a4, a5);
	}
	else if constexpr (N == 5)
	{
		auto& [a0, a1, a2, a3, a4] = obj;
		return std::tie(a0, a1, a2, a3, a4);
	}
	else if constexpr (N == 4)
	{
		auto& [a0, a1, a2, a3] = obj;
		return std::tie(a0, a1, a2, a3);
	}
	else if constexpr (N == 3)
	{
		auto& [a0, a1, a2] = obj;
		return std::tie(a0, a1, a2);
	}
	else if constexpr (N == 2)
	{
		auto& [a0, a1] = obj;
		return std::tie(a0, a1);
	}
	else if constexpr (N == 1)
	{
		auto& [a0] = obj;
		return std::tie(a0);
	}
	else
	{
		return std::tie();
	}
}

template <typename T>
constexpr bool is_custom_struct_v =
	std::is_aggregate_v<T> && !std::is_arithmetic_v<T> &&
	!std::is_same_v<T, std::string>;

template <typename T> struct is_tuple_type : std::false_type
{
};
template <typename... Args>
struct is_tuple_type<std::tuple<Args...>> : std::true_type
{
};
template <typename T> constexpr bool is_tuple_type_v = is_tuple_type<T>::value;

enum class WireType : std::uint8_t
{
	kFixed8 = 0,	 // (int8, uint8_t, char, bool)
	kFixed16 = 1,	 // (int16, uint16_t, short)
	kFixed32 = 2,	 // (int32, uint32_t, float)
	kFixed64 = 3,	 // (int64, uint64_t, double)
	kLengthDelim = 4 // (string, tuple, struct, vector)
};

template <typename T, typename Enable = void> struct WireTypeTraits;

template <typename T>
struct WireTypeTraits<
	T, std::enable_if_t<std::is_arithmetic_v<T> && sizeof(T) == 1>>
{
	static constexpr WireType value = WireType::kFixed8;
};

template <typename T>
struct WireTypeTraits<
	T, std::enable_if_t<std::is_arithmetic_v<T> && sizeof(T) == 2>>
{
	static constexpr WireType value = WireType::kFixed16;
};

template <typename T>
struct WireTypeTraits<
	T, std::enable_if_t<std::is_arithmetic_v<T> && sizeof(T) == 4>>
{
	static constexpr WireType value = WireType::kFixed32;
};

template <typename T>
struct WireTypeTraits<
	T, std::enable_if_t<std::is_arithmetic_v<T> && sizeof(T) == 8>>
{
	static constexpr WireType value = WireType::kFixed64;
};

template <> struct WireTypeTraits<std::string>
{
	static constexpr WireType value = WireType::kLengthDelim;
};

template <typename T>
struct WireTypeTraits<T, std::enable_if_t<is_custom_struct_v<T>>>
{
	static constexpr WireType value = WireType::kLengthDelim;
};

template <typename... Args> struct WireTypeTraits<std::tuple<Args...>>
{
	static constexpr WireType value = WireType::kLengthDelim;
};

template <typename T> void appendArithmetic(net::Buffer* buf, T val)
{
	static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
	T be = meta::hostToBE(val);
	buf->append(reinterpret_cast<const char*>(&be), sizeof(T));
}

template <typename T> T readArithmetic(net::Buffer* buf)
{
	static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
	assert(buf->readableBytes() >= sizeof(T));

	T val;
	std::memcpy(&val, buf->peek(), sizeof(T));
	buf->retrieve(sizeof(T));
	return meta::beToHost(val);
}

template <typename T, typename Enable = void> struct SerializeTraits;

template <typename T, typename Enable = void> struct DeserializeTraits;

template <typename T>
struct SerializeTraits<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static void serialize(net::Buffer* buf, const T& val)
	{
		appendArithmetic(buf, val);
	}
};

template <typename T>
struct DeserializeTraits<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static void deserialize(net::Buffer* buf, T* val)
	{
		*val = readArithmetic<T>(buf);
	}
};

template <> struct SerializeTraits<std::string>
{
	static void serialize(net::Buffer* buf, const std::string& val)
	{
		std::uint32_t len = val.size();
		SerializeTraits<std::uint32_t>::serialize(buf, len);
		buf->append(val.data(), len);
	}
};

template <> struct DeserializeTraits<std::string>
{
	static void deserialize(net::Buffer* buf, std::string* val)
	{
		std::uint32_t len;
		DeserializeTraits<std::uint32_t>::deserialize(buf, &len);
		if (buf->readableBytes() < len)
		{
			throw std::runtime_error("Buffer underflow for string");
		}
		val->assign(buf->peek(), len);
		buf->retrieve(len);
	}
};

inline void skip_unknown_field(net::Buffer* buf, WireType wt)
{
	if (wt == WireType::kFixed8)
	{
		buf->retrieve(1);
	}
	else if (wt == WireType::kFixed16)
	{
		buf->retrieve(2);
	}
	else if (wt == WireType::kFixed32)
	{
		buf->retrieve(4);
	}
	else if (wt == WireType::kFixed64)
	{
		buf->retrieve(8);
	}
	else if (wt == WireType::kLengthDelim)
	{
		std::uint32_t len;
		DeserializeTraits<std::uint32_t>::deserialize(buf, &len);
		buf->retrieve(len); // 无脑跳过指定的长度
	}
	else
	{
		throw std::runtime_error("Unknown WireType in TLV parser!");
	}
}

template <typename Tuple, std::size_t... Is>
void serialize_tuple_tlv_impl(net::Buffer* buf, const Tuple& t,
							  std::index_sequence<Is...>)
{
	(...,
	 (
		 [&]
		 {
			 using FieldType = std::remove_cvref_t<decltype(std::get<Is>(t))>;
			 constexpr WireType wt = WireTypeTraits<FieldType>::value;

			 std::uint8_t tag_and_type = (static_cast<std::uint8_t>(Is) << 3) |
										 static_cast<uint8_t>(wt);
			 buf->append(reinterpret_cast<const char*>(&tag_and_type),
						 sizeof(tag_and_type));

			 if constexpr (wt == WireType::kLengthDelim)
			 {
				 if constexpr (is_custom_struct_v<FieldType> ||
							   is_tuple_type_v<FieldType>)
				 {
					 std::size_t size_before = buf->readableBytes();
					 SerializeTraits<FieldType>::serialize(buf,
														   std::get<Is>(t));
					 std::uint32_t len = static_cast<std::uint32_t>(
						 buf->readableBytes() - size_before);
					 std::size_t n = sizeof(len);
					 buf->ensureWritableBytes(n);
					 std::copy(buf->data_.data() + buf->wptr_ - len,
							   buf->data_.data() + buf->wptr_,
							   buf->data_.data() + buf->wptr_ - len + n);
					 buf->wptr_ -= len;
					 SerializeTraits<std::uint32_t>::serialize(buf, len);
					 buf->wptr_ += len;
					 //  Buffer tmp{};
					 //  SerializeTraits<FieldType>::serialize(&tmp,
					 // 									   std::get<Is>(t));

					 //  std::uint32_t len =
					 // 	 static_cast<std::uint32_t>(tmp.readableBytes());
					 //  SerializeTraits<std::uint32_t>::serialize(buf, len);

					 //  buf->append(tmp.peek(), tmp.readableBytes());
				 }
				 else
				 {
					 SerializeTraits<FieldType>::serialize(buf,
														   std::get<Is>(t));
				 }
			 }
			 else
			 {
				 SerializeTraits<std::remove_cvref_t<FieldType>>::serialize(
					 buf, std::get<Is>(t));
			 }
		 }()));
}

template <typename Tuple, std::size_t... Is>
void deserialize_tuple_tlv_impl(net::Buffer* buf, Tuple* t,
								std::index_sequence<Is...>,
								std::uint32_t total_limit)
{
	std::size_t target_remaining = buf->readableBytes() - total_limit;

	while (buf->readableBytes() > target_remaining)
	{
		std::uint8_t tag_and_type;
		std::memcpy(&tag_and_type, buf->peek(), 1);
		buf->retrieve(1);

		std::uint8_t tag = tag_and_type >> 3;
		WireType wt = static_cast<WireType>(tag_and_type & 0x07);

		bool handled = false;

		(...,
		 ((tag == static_cast<std::uint8_t>(Is))
			  ? (
					[&]
					{
						using FieldType =
							std::remove_cvref_t<decltype(std::get<Is>(*t))>;

						if constexpr (WireTypeTraits<FieldType>::value ==
									  WireType::kLengthDelim)
						{
							if constexpr (is_custom_struct_v<FieldType> ||
										  is_tuple_type_v<FieldType>)
							{
								std::uint32_t len;
								DeserializeTraits<std::uint32_t>::deserialize(
									buf, &len);
								DeserializeTraits<FieldType>::deserialize(
									buf, &std::get<Is>(*t), len);
							}
							else
							{
								DeserializeTraits<FieldType>::deserialize(
									buf, &std::get<Is>(*t));
							}
						}
						else
						{
							DeserializeTraits<FieldType>::deserialize(
								buf, &std::get<Is>(*t));
						}
						handled = true;
					}(),
					true)
			  : false));

		if (!handled)
		{
			skip_unknown_field(buf, wt);
		}
	}
	assert(buf->readableBytes() == target_remaining &&
		   "TLV parse error: length mismatch!");
}

template <typename... Args> struct SerializeTraits<std::tuple<Args...>>
{
	static void serialize(net::Buffer* buf, const std::tuple<Args...>& t)
	{
		serialize_tuple_tlv_impl(buf, t, std::index_sequence_for<Args...>{});
	}
};

template <typename... Args> struct DeserializeTraits<std::tuple<Args...>>
{
	static void deserialize(net::Buffer* buf, std::tuple<Args...>* t,
							std::uint32_t limit = 0)
	{
		if (limit == 0)
		{
			limit = static_cast<uint32_t>(buf->readableBytes());
		}
		deserialize_tuple_tlv_impl(buf, t, std::index_sequence_for<Args...>{},
								   limit);
	}
};

template <typename T>
struct SerializeTraits<T, std::enable_if_t<is_custom_struct_v<T>>>
{
	static void serialize(net::Buffer* buf, const T& val)
	{
		auto t = struct_to_tuple(const_cast<T&>(val));
		SerializeTraits<decltype(t)>::serialize(buf, t);
	}
};

template <typename T>
struct DeserializeTraits<T, std::enable_if_t<is_custom_struct_v<T>>>
{
	static void deserialize(net::Buffer* buf, T* val, std::uint32_t limit = 0)
	{
		auto t = struct_to_tuple(*val);
		DeserializeTraits<decltype(t)>::deserialize(buf, &t, limit);
	}
};
} // namespace rpc
} // namespace netx

#endif