#ifndef RAC_ASYNC_RESULT_HPP
#define RAC_ASYNC_RESULT_HPP

#include "rac/async/non_void_helper.hpp"
#include <exception>
#include <memory>
namespace rac
{
template <typename T = void> struct Result
{
	template <typename... Args> void return_value(Args... args)
	{
		new (std::addressof(val)) T(std::forward<Args>(args)...);
		has_value_ = true;
	}

	void unhandled_exception() noexcept
	{
		exception = std::current_exception();
	}

	bool has_value() const noexcept
	{
		return has_value_;
	}

	T result() &
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		return val;
	}

	T result() &&
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		T ret = std::move(val);
		val.~T();
		has_value_ = false;
		return ret;
	}

	Result() noexcept
	{
	}

	Result(Result&&) = delete;

	~Result()
	{
		if (has_value_)
		{
			val.~T();
		}
	}

	union
	{
		T val;
	};
	std::exception_ptr exception;
	bool has_value_{false};
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

	bool has_value() const noexcept
	{
		return false;
	}

	auto result() noexcept
	{
		if (exception)
		{
			std::rethrow_exception(exception);
		}
		return NonVoidHelper<>{};
	}

	Result() = default;

	Result(Result&&) = delete;

	~Result() = default;

	std::exception_ptr exception;
};

template <typename T> struct Result<T const> : public Result<T>
{
};

template <typename T>
struct Result<T&> : public Result<std::reference_wrapper<T>>
{
};

template <typename T> struct Result<T&&> : public Result<T>
{
};
} // namespace rac

#endif