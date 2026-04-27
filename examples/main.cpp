#include "elog/logger.hpp"
#include "file_tree.hpp"
#include "netx/core/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include "tui_state.hpp"
#include <atomic>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <thread>
using namespace ftxui;
using namespace std::chrono_literals;
using namespace netx::core;
using namespace netx::http;
using namespace std::chrono_literals;

int main()
{
	auto screen = ScreenInteractive::Fullscreen();
	g_state.trigger_redraw = [&screen] { screen.PostEvent(Event::Custom); };
	elog::details::g_log_callback = [](elog::LogLevel lv, std::string_view msg)
	{
		g_state.log(std::string(elog::details::log_level_to_string(lv)),
					std::string(msg));
	};
	auto root_node = scan_directory(NETX_WEB_SRC_DIR);
	auto sync_files = [&]
	{
		std::lock_guard<std::shared_mutex> lock(g_state.fs_mtx);
		g_state.allowed_files.clear();
		gather_allowed_files(root_node, g_state.allowed_files);
	};
	auto file_tree_ui = build_tree_ui(root_node, sync_files);
	sync_files();
	std::thread qps_thread(
		[&]
		{
			while (g_state.running.load(std::memory_order_acquire))
			{
				std::this_thread::sleep_for(1s);
				int reqs = g_state.current_reqs.exchange(0);
				{
					std::lock_guard<std::shared_mutex> lock(g_state.qps_mtx);
					g_state.qps_history.push_back(reqs);
					if (g_state.qps_history.size() > 200)
					{
						g_state.qps_history.erase(g_state.qps_history.begin());
					}
				}
				g_state.trigger_redraw();
			}
		});
	std::thread server_thread(
		[&]
		{
			try
			{
				Server::server()
					.listen("0.0.0.0", 8080)
					.loop(2)
					.route(
						"GET", "/*",
						[](Request& req) -> netx::core::Task<Expected<Response>>
						{
							g_state.current_reqs++;
							bool allowed = false;
							{
								std::shared_lock<std::shared_mutex> lock(
									g_state.fs_mtx);
								allowed = g_state.allowed_files[req.url_path];
							}
							netx::http::Response res;
							if (!allowed)
							{
								res.with_status(403).with_body(
									"<h1>403 Forbidden</h1>");
							}
							else
							{
								res.with_status(200).with_file(
									NETX_WEB_SRC_DIR + req.url_path);
							}
							co_return res;
						})
					.start();
				g_state.log("INFO", "Server loop safely exited.");
			}
			catch (const std::exception& e)
			{
				g_state.log("ERROR", std::string("Fatal: ") + e.what());
			}
		});
	auto do_quit = [&]
	{
		g_state.log("WARN", "Initiating graceful shutdown...");
		g_state.running = false;
		Server::server().stop();
		screen.ExitLoopClosure()();
	};
	auto left_panel =
		Renderer(file_tree_ui,
				 [&]
				 {
					 return window(text(" Shared Resources "),
								   file_tree_ui->Render() | yframe) |
							size(WIDTH, GREATER_THAN, 30);
				 });
	auto qps_graph_func = [&](int width, int height)
	{
		std::vector<int> output(width, 0);
		std::shared_lock<std::shared_mutex> lock(g_state.qps_mtx);
		int n = g_state.qps_history.size();
		for (int i = 0; i < width; ++i)
		{
			int idx = n - width + i;
			if (idx >= 0 && idx < n)
			{
				output[i] = g_state.qps_history[idx];
			}
		}
		return output;
	};
	auto dashboard = Renderer(
		Container::Vertical({file_tree_ui}),
		[&]
		{
			auto traffic_panel =
				window(text(" Real-time Traffic (QPS) "),
					   graph(qps_graph_func) | color(Color::White)) |
				flex;
			auto stats_panel =
				window(text(" Log Statistics "),
					   hbox({filler(), text("● WARN ") | color(Color::Yellow),
							 text(std::to_string(g_state.warn_count) + "  "),
							 text("● ERROR ") | color(Color::Red),
							 text(std::to_string(g_state.error_count) + "  "),
							 text("● FATAL ") | color(Color::Magenta),
							 text(std::to_string(g_state.fatal_count)),
							 filler()}) |
						   center) |
				size(HEIGHT, EQUAL, 3);
			auto event_panel = window(text(" Latest Event "),
									  text(g_state.latest_log) |
										  color(g_state.latest_log_color)) |
							   size(HEIGHT, EQUAL, 3);
			auto right_column = vbox({traffic_panel, stats_panel, event_panel});
			auto left_column = vbox(
				{left_panel->Render() | flex,
				 window(text(" Control "),
						hbox({text(g_state.running ? "Status: [RUNNING]"
												   : "Status: [STOPPING]") |
								  flex,
							  filler(), dim(text("q/Ctrl-C: quit"))})) |
					 size(HEIGHT, EQUAL, 3)});
			return hbox({left_column, right_column | flex}) | clear_under;
		});
	auto dashboard_q = CatchEvent(dashboard,
								  [&](Event e)
								  {
									  if (e == Event::Character('q') ||
										  e == Event::Character('\x03'))
									  {
										  do_quit();
										  return true;
									  }
									  return false;
								  });
	screen.Loop(dashboard_q);
	g_state.running = false;
	if (qps_thread.joinable())
		qps_thread.join();
	if (server_thread.joinable())
		server_thread.join();
	return 0;
}