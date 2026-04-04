#ifndef NETX_META_RADIX_TREE_HPP
#define NETX_META_RADIX_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace meta
{
template <typename T> struct RadixTree
{
	struct RadixNode
	{
		T value;
		std::unordered_map<std::string, std::unique_ptr<RadixNode>> next;
	};

	void insert(const std::string& path, const T& val);
	void insert(const std::string& path, T&& val);
	T search(const std::string& path);

	RadixTree() : root(std::make_unique<RadixNode>())
	{
	}

	RadixTree(RadixTree&&) = delete;
	~RadixTree() = default;

  private:
	std::string get_sub_path(const std::string& path, std::size_t* start);
	std::string normalize_path(const std::string& path);

  private:
	std::unique_ptr<RadixNode> root;
};

template <typename T>
inline void RadixTree<T>::insert(const std::string& path, const T& val)
{
	if (path == "/")
	{
		root->value = val;
		return;
	}

	std::size_t start = (path[0] == '/') ? 1 : 0;
	RadixNode* cur = root.get();

	while (true)
	{
		std::string s = get_sub_path(path, &start);
		if (s.empty() && start == std::string::npos)
		{
			break; // error
		}

		if (cur->next.find(s) == cur->next.end())
		{
			cur->next.try_emplace(s, std::make_unique<RadixNode>());
		}

		cur = cur->next[s].get();
	}

	cur->value = val;
}

template <typename T>
inline void RadixTree<T>::insert(const std::string& path, T&& val)
{
	if (path == "/")
	{
		root->value = std::move(val);
		return;
	}

	std::size_t start = (path[0] == '/') ? 1 : 0;
	RadixNode* cur = root.get();

	while (true)
	{
		std::string s = get_sub_path(path, &start);
		if (s.empty() && start == std::string::npos)
		{
			break; // error
		}

		if (cur->next.find(s) == cur->next.end())
		{
			cur->next.try_emplace(s, std::make_unique<RadixNode>());
		}

		cur = cur->next[s].get();
	}

	cur->value = std::move(val);
}

template <typename T> T RadixTree<T>::search(const std::string& path)
{
	if (path == "/")
	{
		return root->value;
	}

	std::string normalized_path = normalize_path(path);

	size_t start = (normalized_path[0] == '/') ? 1 : 0;
	RadixNode* cur = root.get();

	while (true)
	{
		std::string s = get_sub_path(normalized_path, &start);
		if (s.empty() && start == std::string::npos)
		{
			break;
		}

		if (cur->next.contains(s))
		{
			cur = cur->next[s].get();
		}
		else if (cur->next.contains("*"))
		{
			return cur->next["*"]->value;
		}
		else
		{
			return nullptr;
		}
	}

	return cur->value;
}

template <typename T>
std::string RadixTree<T>::get_sub_path(const std::string& path,
									   std::size_t* start)
{
	// because *start maybe arive 1
	if (*start == std::string::npos || *start >= path.size())
	{
		*start = std::string::npos;
		return "";
	}

	size_t begin = *start;
	size_t end = path.find('/', begin);

	if (end != std::string::npos)
	{
		*start = end + 1;
		return path.substr(begin, end - begin);
	}

	*start = std::string::npos;
	return path.substr(begin);
}

template <typename T>
std::string RadixTree<T>::normalize_path(const std::string& path)
{
	std::stack<std::string> stk;
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
			std::string seg(path.data() + start, seg_len);
			if (seg == ".")
			{
			}
			else if (seg == "..")
			{
				if (!stk.empty())
				{
					stk.pop();
				}
			}
			else
			{
				stk.push(std::move(seg));
			}
			start = i + 1;
		}
	}

	std::string res;
	res.reserve(path.size() + 8);
	while (!stk.empty())
	{
		res += stk.top() + '/';
		stk.pop();
	}
	return res.empty() ? "/" : (std::reverse(res.begin(), res.end()), res);
}
} // namespace meta
} // namespace netx

#endif