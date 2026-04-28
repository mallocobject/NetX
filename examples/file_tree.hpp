#pragma once
#include "examples/tui_state.hpp"
#include <atomic>
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
struct FileNode
{
	std::string name;
	std::string path;
	bool is_dir = false;
	bool expanded = true;
	bool checked = false;
	std::vector<std::shared_ptr<FileNode>> children;
	void set_check_recursive(bool check)
	{
		checked = check;
		for (auto& child : children)
		{
			child->set_check_recursive(check);
		}
	}
};
inline std::shared_ptr<FileNode> scan_directory(
	const std::filesystem::path& root_path)
{
	auto node = std::make_shared<FileNode>();
	node->name = root_path.filename().string(); // public

	node->path = root_path.string(); // .../public

	node->is_dir = std::filesystem::is_directory(root_path);
	if (node->is_dir)
	{
		for (const auto& entry : std::filesystem::directory_iterator(root_path))
		{
			node->children.push_back(scan_directory(entry.path()));
		}
	}
	return node;
}
inline ftxui::Component build_tree_ui_impl(std::shared_ptr<FileNode> node,
										   std::function<void()> on_change,
										   bool is_last = true, int depth = 0)
{
	using namespace ftxui;

	auto indent = std::string(depth * 2, ' ');
	auto connector = depth > 0 ? (is_last ? "└─ " : "├─ ") : "";

	auto checkbox_transform = [](const EntryState& s) -> Element
	{
		bool running = (g_state.state.load() == ServerState::kRunning);
		auto icon = s.state ? "▣ " : "▢ ";
		return hbox({text(icon), text(s.label)}) | (running ? dim : nothing);
	};

	if (!node->is_dir)
	{
		CheckboxOption opt;
		opt.label = node->name;
		opt.checked = &node->checked;
		opt.on_change = on_change;
		opt.transform = checkbox_transform;
		auto cb = Checkbox(opt);
		return Renderer(
			cb, [cb, indent, connector]
			{ return hbox({text(indent + connector), cb->Render()}); });
	}

	auto children_container = Container::Vertical({});
	for (size_t i = 0; i < node->children.size(); i++)
	{
		children_container->Add(
			build_tree_ui_impl(node->children[i], on_change,
							   i == node->children.size() - 1, depth + 1));
	}

	auto dir_icon = [](const EntryState& s) -> Element
	{
		bool running = (g_state.state.load() == ServerState::kRunning);
		return text(s.state ? "▣" : "▢") | (running ? dim : nothing);
	};

	CheckboxOption dir_opt;
	dir_opt.checked = &node->checked;
	dir_opt.on_change = [=]
	{
		node->set_check_recursive(node->checked);
		on_change();
	};
	dir_opt.transform = dir_icon;
	auto dir_cb = Checkbox(dir_opt);

	auto label = std::make_shared<std::string>(node->expanded ? "▾" : "▸");
	ButtonOption btn_opt;
	btn_opt.label = ConstStringRef(label.get());
	btn_opt.on_click = [=]
	{
		node->expanded = !node->expanded;
		*label = node->expanded ? "▾" : "▸";
	};
	btn_opt.transform = [](const EntryState& s) { return text(s.label); };
	auto toggle_btn = Button(btn_opt);

	auto dir_row = Container::Horizontal({dir_cb, toggle_btn});
	auto main_container = Container::Vertical({dir_row, children_container});

	return Renderer(main_container,
					[=]
					{
						auto header =
							hbox({text(indent + connector), dir_cb->Render(),
								  text(" "), toggle_btn->Render(),
								  text(" " + node->name)});
						if (node->expanded)
						{
							return vbox({header, children_container->Render()});
						}
						return header;
					});
}

inline ftxui::Component build_tree_ui(std::shared_ptr<FileNode> node,
									  std::function<void()> on_change)
{
	auto base_tree = build_tree_ui_impl(node, on_change);
	return ftxui::CatchEvent(
		base_tree,
		[](ftxui::Event e)
		{
			if (g_state.state.load(std::memory_order_acquire) ==
				ServerState::kRunning)
			{
				if (e.is_mouse() || e.is_character() ||
					e == ftxui::Event::Return)
				{
					return true;
				}
			}
			return false;
		});
}

inline void gather_config_allowed_files(
	std::shared_ptr<FileNode> node, std::unordered_map<std::string, bool>& map)
{
	if (!node->is_dir && node->checked)
	{
		auto rel = std::filesystem::relative(node->path, NETX_WEB_SRC_DIR);
		map["/" + rel.string()] = true;
	}
	for (auto& child : node->children)
	{
		gather_config_allowed_files(child, map);
	}
}
