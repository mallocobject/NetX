#ifndef ELOG_LOGGER_HPP
#define ELOG_LOGGER_HPP

#include "elog/async_logger.hpp"
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <source_location>
#include <string>
#include <thread>
#include <utility>
namespace elog
{

#define ELOG_FOREACH_LOG_LEVEL(f)                                              \
	f(TRACE) f(DEBUG) f(INFO) f(WARN) f(ERROR) f(FATAL)

enum class LogLevel : std::uint8_t
{
#define _FUNCTION(name) name,
	ELOG_FOREACH_LOG_LEVEL(_FUNCTION)
#undef _FUNCTION
};

namespace details
{
inline const char
	level_ansi_colors[static_cast<std::uint8_t>(LogLevel::FATAL) + 1][6] = {
		"\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[35m"};

inline std::string log_level_to_string(LogLevel lv)
{
	switch (lv)
	{
#define _FUNCTION(name)                                                        \
	case LogLevel::name:                                                       \
		return #name;
		ELOG_FOREACH_LOG_LEVEL(_FUNCTION)
#undef _FUNCTION
	}
	return "UNKNOWN";
}

inline LogLevel log_level_from_string(std::string lv)
{
#define _FUNCTION(name)                                                        \
	if (lv == #name)                                                           \
		return LogLevel::name;
	ELOG_FOREACH_LOG_LEVEL(_FUNCTION)
#undef _FUNCTION

	return LogLevel::INFO;
}

template <typename T> struct WithSourceLocation
{
	template <typename U>
		requires std::constructible_from<T, U>
	consteval WithSourceLocation(
		U&& inner, std::source_location loc = std::source_location::current())
		: inner_(std::forward<U>(inner)), loc_(std::move(loc))
	{
	}

	constexpr const T& format() const noexcept
	{
		return inner_;
	}

	constexpr const std::source_location& location() const noexcept
	{
		return loc_;
	}

  private:
	T inner_;
	std::source_location loc_;
};

inline auto g_log_file = []() -> std::unique_ptr<AsyncLogger>
{
	if (auto path = std::getenv("ELOG_PATH"))
	{
		return std::make_unique<AsyncLogger>(path, "");
	}
	return nullptr;
}();

inline auto g_log_threshold = []() -> LogLevel
{
	if (auto lv = std::getenv("ELOG_LEVEL"))
	{
		return log_level_from_string(lv);
	}
	return LogLevel::INFO;
}();

inline auto g_log_callback = std::function<void(LogLevel, std::string_view)>{};

inline void output_log(LogLevel lv, std::string&& msg,
					   const std::source_location& loc)
{

	if (lv >= g_log_threshold)
	{
		thread_local uint64_t tid =
			std::hash<std::thread::id>{}(std::this_thread::get_id());
		std::chrono::zoned_time now{std::chrono::current_zone(),
									std::chrono::system_clock::now()};
		std::string fmsg = std::format("{}[{}]<{}> {}:{} {}()-> {}", now, tid,
									   log_level_to_string(lv), loc.file_name(),
									   loc.line(), loc.function_name(), msg);

		if (g_log_file)
		{
			g_log_file->append_message(fmsg + '\n');
		}

		if (g_log_callback)
		{
			g_log_callback(lv, msg);
		}
		else
		{
			std::cout << level_ansi_colors[static_cast<std::uint8_t>(lv)] +
							 fmsg + "\033[0m\n";
		}
	}
}
} // namespace details

inline void set_log_path(std::string dir, std::string prefix, size_t roll_size,
						 std::chrono::seconds flush_interval,
						 size_t check_per_count)
{
	details::g_log_file = std::make_unique<details::AsyncLogger>(
		dir, prefix, roll_size, flush_interval, check_per_count);
}

inline void set_log_threshold(LogLevel lv)
{
	details::g_log_threshold = lv;
}

template <typename... Args>
void log(LogLevel lv,
		 details::WithSourceLocation<std::format_string<Args...>> fmt,
		 Args&&... args)
{
	const auto& loc = fmt.location();
	auto msg = std::vformat(fmt.format().get(), std::make_format_args(args...));
	details::output_log(lv, std::move(msg), loc);
}

#define _FUNCTION(name)                                                        \
	template <typename... Args>                                                \
	void LOG_##name(                                                           \
		details::WithSourceLocation<std::format_string<Args...>> fmt,          \
		Args&&... args)                                                        \
	{                                                                          \
		return log(LogLevel::name, std::move(fmt),                             \
				   std::forward<Args>(args)...);                               \
	}
ELOG_FOREACH_LOG_LEVEL(_FUNCTION)
#undef _FUNCTION
} // namespace elog

#endif