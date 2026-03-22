#ifndef RAC_NET_BUFFER_ENDIAN_HELPER_HPP
#define RAC_NET_BUFFER_ENDIAN_HELPER_HPP

#include <cstdint>
#include <endian.h>
#include <type_traits>
namespace rac
{

template <typename T> using make_unsigned_t = std::make_unsigned_t<T>;

template <typename T> static T hostToBE(T x)
{
	static_assert(std::is_integral_v<T>, "hostToBE<T>: T must be integral");

	using U = make_unsigned_t<T>;
	U ux = static_cast<U>(x);

	if constexpr (sizeof(T) == 1)
	{
		return x;
	}
	else if constexpr (sizeof(T) == 2)
	{
		U be = static_cast<U>(htobe16(static_cast<std::uint16_t>(ux)));
		return static_cast<T>(be);
	}
	else if constexpr (sizeof(T) == 4)
	{
		U be = static_cast<U>(htobe32(static_cast<std::uint32_t>(ux)));
		return static_cast<T>(be);
	}
	else if constexpr (sizeof(T) == 8)
	{
		U be = static_cast<U>(htobe64(static_cast<std::uint64_t>(ux)));
		return static_cast<T>(be);
	}
	else
	{
		static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
						  sizeof(T) == 8,
					  "hostToBE<T>: unsupported integer size");
	}
}

template <typename T> static T beToHost(T be)
{
	static_assert(std::is_integral_v<T>, "beToHost<T>: T must be integral");

	using U = make_unsigned_t<T>;
	U ube = static_cast<U>(be);

	if constexpr (sizeof(T) == 1)
	{
		return be;
	}
	else if constexpr (sizeof(T) == 2)
	{
		U h = static_cast<U>(be16toh(static_cast<std::uint16_t>(ube)));
		return static_cast<T>(h);
	}
	else if constexpr (sizeof(T) == 4)
	{
		U h = static_cast<U>(be32toh(static_cast<std::uint32_t>(ube)));
		return static_cast<T>(h);
	}
	else if constexpr (sizeof(T) == 8)
	{
		U h = static_cast<U>(be64toh(static_cast<std::uint64_t>(ube)));
		return static_cast<T>(h);
	}
	else
	{
		static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
						  sizeof(T) == 8,
					  "beToHost<T>: unsupported integer size");
	}
}
} // namespace rac

#endif