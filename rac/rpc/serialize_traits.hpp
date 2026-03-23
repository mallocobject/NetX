#ifndef RAC_RPC_SERIALIZE_TRAITS_HPP
#define RAC_RPC_SERIALIZE_TRAITS_HPP

#include "rac/meta/buffer_endian_helper.hpp"
#include "rac/meta/reflection.hpp"
#include "rac/net/buffer.hpp"
#include <cassert>
#include <cstddef>
#include <tuple>
#include <type_traits>
namespace rac
{
template <typename T> void appendArithmetic(Buffer* buf, T val)
{
	static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
	T be = hostToBE(val);
	buf->append(reinterpret_cast<const char*>(&be), sizeof(T));
}

template <typename T> T readArithmetic(Buffer* buf)
{
	static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
	assert(buf->readableBytes() >= sizeof(T));

	T val;
	std::memcpy(&val, buf->peek(), sizeof(T));
	buf->retrieve(sizeof(T));
	return beToHost(val);
}

template <typename T, typename Enable = void> struct SerializeTraits;

template <typename T, typename Enable = void> struct DeserializeTraits;

template <typename T>
struct SerializeTraits<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static void serialize(Buffer* buf, const T& val)
	{
		appendArithmetic(buf, val);
	}
};

template <typename T>
struct DeserializeTraits<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static void deserialize(Buffer* buf, T* val)
	{
		*val = readArithmetic<T>(buf);
	}
};

template <> struct SerializeTraits<std::string>
{
	static void serialize(Buffer* buf, const std::string& val)
	{
		std::size_t len = val.size();
		SerializeTraits<std::size_t>::serialize(buf, len);
		buf->append(val.data(), len);
	}
};

template <> struct DeserializeTraits<std::string>
{
	static void deserialize(Buffer* buf, std::string* val)
	{
		std::size_t len;
		DeserializeTraits<std::size_t>::deserialize(buf, &len);
		if (buf->readableBytes() < len)
		{
			throw std::runtime_error("Buffer underflow for string");
		}
		val->assign(buf->peek(), len);
		buf->retrieve(len);
	}
};

template <typename T>
struct SerializeTraits<T, std::enable_if_t<is_custom_struct_v<T>>>
{
	static void serialize(Buffer* buf, const T& val)
	{
		auto t = struct_to_tuple(const_cast<T&>(val));
		SerializeTraits<decltype(t)>::serialize(buf, t);
	}
};

template <typename T>
struct DeserializeTraits<T, std::enable_if_t<is_custom_struct_v<T>>>
{
	static void deserialize(Buffer* buf, T* val)
	{
		auto t = struct_to_tuple(*val);
		DeserializeTraits<decltype(t)>::deserialize(buf, &t);
	}
};

template <typename... Args> struct SerializeTraits<std::tuple<Args...>>
{
	static void serialize(Buffer* buf, const std::tuple<Args...>& t)
	{
		std::apply(
			[buf](const auto&... args)
			{
				(SerializeTraits<
					 std::remove_cvref_t<decltype(args)>>::serialize(buf, args),
				 ...);
			},
			t);
	}
};

template <typename... Args> struct DeserializeTraits<std::tuple<Args...>>
{
	static void deserialize(Buffer* buf, std::tuple<Args...>* t)
	{
		std::apply(
			[buf](auto&... args)
			{
				(DeserializeTraits<
					 std::remove_cvref_t<decltype(args)>>::deserialize(buf,
																	   &args),
				 ...);
			},
			*t);
	}
};
} // namespace rac

#endif