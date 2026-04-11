#pragma once

#include "netx/third_party/expected.hpp"
#include <system_error>
#include <utility>
namespace netx
{
namespace core
{
template <typename T = void> struct Expected
{
	// using ValueType = T;

	Expected() = default;
	Expected(const T& value) : exp_(value)
	{
	}
	Expected(T&& value) : exp_(std::move(value))
	{
	}
	Expected(tl::expected<T, std::error_code> e) : exp_(std::move(e))
	{
	}
	Expected(std::error_code ec) : exp_(tl::unexpected{ec})
	{
	}

	Expected& operator=(const T& value)
	{
		exp_ = value;
		return *this;
	}

	Expected& operator=(T&& value)
	{
		exp_ = std::move(value);
		return *this;
	}

	Expected& operator=(std::error_code ec)
	{
		exp_ = tl::unexpected{ec};
		return *this;
	}

	Expected& operator=(tl::expected<T, std::error_code> e)
	{
		exp_ = std::move(e);
		return *this;
	}

	constexpr bool has_value() const noexcept
	{
		return exp_.has_value();
	}
	constexpr explicit operator bool() const noexcept
	{
		return exp_.has_value();
	}

	T& value() &
	{
		return exp_.value();
	}
	const T& value() const&
	{
		return exp_.value();
	}
	T&& value() &&
	{
		return std::move(exp_).value();
	}
	const T&& value() const&&
	{
		return std::move(exp_).value();
	}

	const std::error_code& error() const& noexcept
	{
		return exp_.error();
	}

	std::error_code& error() & noexcept
	{
		return exp_.error();
	}

	std::error_code&& error() && noexcept
	{
		return std::move(exp_).error();
	}

	template <typename U> T value_or(U&& default_value) const&
	{
		return exp_.value_or(std::forward<U>(default_value));
	}

	template <typename U> T value_or(U&& default_value) &&
	{
		return std::move(exp_).value_or(std::forward<U>(default_value));
	}

  private:
	tl::expected<T, std::error_code> exp_{};
};

template <> struct Expected<void>
{
	// using ValueType = void;

	Expected() = default;
	Expected(tl::expected<void, std::error_code> e) : exp_(std::move(e))
	{
	}
	Expected(std::error_code ec) : exp_(tl::unexpected{ec})
	{
	}

	Expected& operator=(std::error_code ec)
	{
		exp_ = tl::unexpected{ec};
		return *this;
	}

	Expected& operator=(tl::expected<void, std::error_code> e)
	{
		exp_ = std::move(e);
		return *this;
	}

	constexpr bool has_value() const noexcept
	{
		return exp_.has_value();
	}
	constexpr explicit operator bool() const noexcept
	{
		return exp_.has_value();
	}

	const std::error_code& error() const& noexcept
	{
		return exp_.error();
	}

	std::error_code& error() & noexcept
	{
		return exp_.error();
	}

	std::error_code&& error() && noexcept
	{
		return std::move(exp_).error();
	}

  private:
	tl::expected<void, std::error_code> exp_{};
};
} // namespace core
} // namespace netx