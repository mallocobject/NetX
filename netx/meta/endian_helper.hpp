#ifndef NETX_META_ENDIAN_HELPER_HPP
#define NETX_META_ENDIAN_HELPER_HPP

#include <cstdint>
#include <cstring>
#include <endian.h>
#include <type_traits>
namespace netx
{
namespace meta
{
template <typename T> using make_unsigned_t = std::make_unsigned_t<T>;

template <typename T> static T hostToBE(T x)
{
	if constexpr (std::is_integral_v<T>)
	{
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
	else if constexpr (std::is_floating_point_v<T>)
	{
		using U = std::conditional_t<
			sizeof(T) == 4, std::uint32_t,
			std::conditional_t<sizeof(T) == 8, std::uint64_t, void>>;
		static_assert(!std::is_same_v<U, void>,
					  "hostToBE: unsupported floating point size");

		U raw;
		std::memcpy(&raw, &x, sizeof(T));
		raw = hostToBE(raw);
		T result;
		std::memcpy(&result, &raw, sizeof(T));
		return result;
	}
	else
	{
		static_assert(sizeof(T) == 0, "hostToBE: unsupported type");
	}
}

template <typename T> static T beToHost(T be)
{
	if constexpr (std::is_integral_v<T>)
	{
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
	else if constexpr (std::is_floating_point_v<T>)
	{
		using U = std::conditional_t<
			sizeof(T) == 4, std::uint32_t,
			std::conditional_t<sizeof(T) == 8, std::uint64_t, void>>;
		static_assert(!std::is_same_v<U, void>,
					  "beToHost: unsupported floating point size");

		U raw;
		std::memcpy(&raw, &be, sizeof(T));
		raw = beToHost(raw);
		T result;
		std::memcpy(&result, &raw, sizeof(T));
		return result;
	}
	else
	{
		static_assert(sizeof(T) == 0, "beToHost: unsupported type");
	}
}
} // namespace meta
} // namespace netx

#endif