#pragma once

#include "netx/core/non_void_helper.hpp"
#include <exception>
#include <functional>
#include <memory>
#include <utility>
namespace netx
{
namespace core
{
namespace details
{
template <typename T = void> struct Result
{
	template <typename... Args> void return_value(Args&&... args)
	{
		new (std::addressof(val)) T(std::forward<Args>(args)...);
		has_value = true;
	}

	void return_value(T value)
	{
		new (std::addressof(val)) T(std::move(value));
		has_value = true;
	}

	void unhandled_exception() noexcept
	{
		exception = std::current_exception();
	}

	template <typename... Args> void put_value(Args&&... args)
	{
		if (has_value)
		{
			val = T(std::forward<Args>(args)...);
		}
		else
		{
			new (std::addressof(val)) T(std::forward<Args>(args)...);
			has_value = true;
		}
	}

	T& result() &
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		has_value = true;
		return val;
	}

	T result() &&
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		T ret = std::move(val);
		has_value = false;
		return ret;
	}

	Result()
	{
	}

	~Result()
	{
		if (has_value)
		{
			val.~T();
		}
	}

	union
	{
		T val;
	};
	std::exception_ptr exception;
	bool has_value{false};
};

template <> struct Result<void>
{
	void return_void() const noexcept
	{
	}

	void unhandled_exception() noexcept
	{
		exception = std::current_exception();
	}

	auto result() noexcept
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		return NonVoidHelper<>{};
	}

	std::exception_ptr exception;
};

template <typename T> struct Result<const T> : Result<T>
{
};

template <typename T> struct Result<T&> : Result<std::reference_wrapper<T>>
{
};

template <typename T> struct Result<T&&> : Result<T>
{
};
} // namespace details
} // namespace core
} // namespace netx