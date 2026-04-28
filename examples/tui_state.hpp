#pragma once

#include <atomic>
#include <cstddef>
#include <ftxui/screen/color.hpp>
#include <functional>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class ServerState
{
	kStopped,
	kRunning
};

struct SharedState
{
	std::atomic<ServerState> state{ServerState::kStopped};
	std::atomic<bool> running{true};

	std::atomic<size_t> trace_count{0};
	std::atomic<size_t> debug_count{0};
	std::atomic<size_t> info_count{0};
	std::atomic<size_t> warn_count{0};
	std::atomic<size_t> error_count{0};
	std::atomic<size_t> fatal_count{0};

	std::mutex log_mtx;
	std::string latest_log{"Waiting for logs..."};
	ftxui::Color latest_log_color{ftxui::Color::White};

	std::atomic<size_t> current_reqs{0};
	std::shared_mutex qps_mtx;
	std::vector<size_t> qps_history;

	std::mutex fs_mtx;
	std::unordered_map<std::string, bool> config_allowed_files;

	std::function<void()> trigger_redraw;

	void log(const std::string& level, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(log_mtx);
		latest_log = message;
		if (level == "TRACE")
		{
			latest_log_color = ftxui::Color::GrayDark;
			++trace_count;
		}
		else if (level == "DEBUG")
		{
			latest_log_color = ftxui::Color::Cyan;
			++debug_count;
		}
		else if (level == "INFO")
		{
			latest_log_color = ftxui::Color::Green;
			++info_count;
		}
		else if (level == "WARN")
		{
			latest_log_color = ftxui::Color::Yellow;
			++warn_count;
		}
		else if (level == "ERROR")
		{
			latest_log_color = ftxui::Color::Red;
			++error_count;
		}
		else if (level == "FATAL")
		{
			latest_log_color = ftxui::Color::Magenta;
			++fatal_count;
		}
		if (trigger_redraw)
		{
			trigger_redraw();
		}
	}
};

inline SharedState g_state;