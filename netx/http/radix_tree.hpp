#pragma once

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace netx
{
namespace http
{
namespace details
{
template <typename T> struct RadixTree
{
	struct MatchResult
	{
		T* value = nullptr;
		std::unordered_map<std::string, std::string> params;
	};

	struct string_hash
	{
		using is_transparent = void;
		size_t operator()(std::string_view sv) const
		{
			return std::hash<std::string_view>{}(sv);
		}
		size_t operator()(const std::string& s) const
		{
			return std::hash<std::string>{}(s);
		}
	};

	struct Node
	{
		T value{};
		bool is_leaf{false};

		std::unordered_map<std::string, std::unique_ptr<Node>, string_hash,
						   std::equal_to<>>
			children;

		std::string param_name;
		std::unique_ptr<Node> param_node;

		std::unique_ptr<Node> wildcard_node;
	};

	template <typename V> void insert(const std::string& path, V&& val);
	MatchResult search(const std::string& path) const;
	static std::string normalize_path(const std::string& path);

	RadixTree() : root_(std::make_unique<Node>())
	{
	}

	RadixTree(RadixTree&&) = default;
	~RadixTree() = default;

  private:
	std::vector<std::string_view> split(std::string_view path) const;

  private:
	std::unique_ptr<Node> root_;
};

template <typename T>
template <typename V>
void RadixTree<T>::insert(const std::string& path, V&& val)
{
	Node* cur = root_.get();
	auto segments = split(path);

	for (auto seg : segments)
	{
		if (seg.empty())
			continue;

		if (seg[0] == ':') // 参数路径
		{
			if (!cur->param_node)
			{
				cur->param_node = std::make_unique<Node>();
			}
			cur->param_name = seg.substr(1);
			cur = cur->param_node.get();
		}
		else if (seg == "*") // 通配符
		{
			if (!cur->wildcard_node)
			{
				cur->wildcard_node = std::make_unique<Node>();
			}
			cur = cur->wildcard_node.get();
			break;
		}
		else // 静态路径
		{
			auto [it, inserted] = cur->children.try_emplace(std::string(seg));

			if (inserted)
			{
				it->second = std::make_unique<Node>();
			}

			cur = it->second.get();
		}
	}
	cur->value = std::forward<V>(val);
	cur->is_leaf = true;
}

template <typename T>
RadixTree<T>::MatchResult RadixTree<T>::search(const std::string& path) const
{
	MatchResult result;
	Node* cur = root_.get();

	if (path == "/" && cur->is_leaf)
	{
		result.value = &cur->value;
		return result;
	}

	auto segments = split(path);
	for (std::size_t i = 0; i < segments.size(); ++i)
	{
		auto seg = segments[i];

		// 1. 优先匹配静态路径
		if (auto it = cur->children.find(seg); it != cur->children.end())
		{
			cur = it->second.get();
		}
		// 2. 匹配参数路径 :id
		else if (cur->param_node)
		{
			result.params[cur->param_name] = seg;
			cur = cur->param_node.get();
		}
		// 3. 匹配通配符 *
		else if (cur->wildcard_node)
		{
			cur = cur->wildcard_node.get();
			break;
		}
		else
		{
			return {}; // 未找到
		}
	}

	if (cur->is_leaf)
	{
		result.value = &cur->value;
	}
	return result;
}

template <typename T>
std::vector<std::string_view> RadixTree<T>::split(std::string_view path) const
{
	std::vector<std::string_view> segs;
	std::size_t start = 0;
	while (true)
	{
		std::size_t end = path.find('/', start);
		if (end == std::string::npos)
		{
			if (start < path.size())
			{
				segs.push_back(path.substr(start));
			}
			break;
		}
		if (end > start)
		{
			segs.push_back(path.substr(start, end - start));
		}
		start = end + 1;
	}
	return segs;
}

template <typename T>
std::string RadixTree<T>::normalize_path(const std::string& path)
{
	if (path.size() > 1024)
	{
		return "/";
	}

	if (path.find('\0') != std::string::npos)
	{
		return "/";
	}

	std::deque<std::string_view> stk;
	std::size_t start = 0;
	for (std::size_t i = 0; i <= path.size(); i++)
	{
		if (i == path.size() || path[i] == '/')
		{
			std::size_t seg_len = i - start;
			if (seg_len == 0)
			{
				start = i + 1;
				continue;
			}
			std::string_view seg_view(path.data() + start, seg_len);
			if (seg_view == ".")
			{
			}
			else if (seg_view == "..")
			{
				if (!stk.empty())
				{
					stk.pop_back();
				}
			}
			else
			{
				stk.push_back(seg_view);
			}
			start = i + 1;
		}
	}

	std::string res;
	res.reserve(path.size() + 8);
	while (!stk.empty())
	{
		res += '/';
		res += stk.front();
		stk.pop_front();
	}
	return res.empty() ? "/" : res;
}
} // namespace details
} // namespace http
} // namespace netx