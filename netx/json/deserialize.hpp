#ifndef NETX_JSON_DESERIALIZE_HPP
#define NETX_JSON_DESERIALIZE_HPP

#include "netx/json/ctre.hpp"
#include "netx/meta/ordered_map.hpp"
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>
namespace netx
{
namespace json
{

namespace meta = netx::meta;

struct Object;

using Dict = meta::OrderedMap<std::string, Object>;
using List = std::vector<Object>;

struct Object
{
	std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, List,
				 Dict>
		inner;

	template <typename T> bool is() const
	{
		return std::holds_alternative<T>(inner);
	}

	template <typename T> const T& get() const
	{
		return std::get<T>(inner);
	}

	template <typename T> T& get()
	{
		return std::get<T>(inner);
	}
};

template <typename T> std::optional<T> try_parse_num(std::string_view str)
{
	T value;
	if (str[0] == '+')
	{
		str = str.substr(1);
	}

	auto [ptr, ec] =
		std::from_chars(str.data(), str.data() + str.size(), value);

	if (ec == std::errc() && ptr == str.data() + str.size())
	{
		return value;
	}

	return std::nullopt;
}

inline char unescaped_char(char c)
{
	switch (c)
	{
	case 'n':
		return '\n';
	case 'r':
		return '\r';
	case '0':
		return '\0';
	case 't':
		return '\t';
	case 'v':
		return '\v';
	case 'f':
		return '\f';
	case 'b':
		return '\b';
	case 'a':
		return '\a';
	default:
		return c;
	}
}

inline std::pair<Object, std::size_t> parse(std::string_view json)
{
	if (json.empty())
	{
		return {Object{std::nullptr_t{}}, 0};
	}
	else if (std::size_t offset = json.find_first_not_of(" \n\r\0\t\v\f\b\a");
			 offset != 0 && offset != std::string_view::npos)
	{
		auto [obj, eaten] = parse(json.substr(offset));
		return {obj, eaten + offset};
	}
	else if (json.substr(0, 4) == "null")
	{
		return {Object{std::nullptr_t{}}, 4};
	}
	else if (json.substr(0, 4) == "true")
	{
		return {Object{true}, 4};
	}
	else if (json.substr(0, 5) == "false")
	{
		return {Object{false}, 5};
	}
	else if ('0' <= json[0] && json[0] <= '9' || json[0] == '+' ||
			 json[0] == '-')
	{
		if (auto num_re =
				ctre::search<"[+\\-]?[0-9]+(\\.[0-9]*)?([eE][+\\-]?[0-9]+)?">(
					json))
		{
			std::string_view strv(num_re.view());
			if (auto num = try_parse_num<std::int64_t>(strv))
			{
				return {Object{num.value()}, strv.size()};
			}
			if (auto num = try_parse_num<double>(strv))
			{
				return {Object{num.value()}, strv.size()};
			}
		}
	}
	else if (json[0] == '"')
	{
		std::string str;
		enum class Phase : std::uint8_t
		{
			kRaw,
			kEscaped,
		} phase = Phase::kRaw;
		std::size_t idx;
		for (idx = 1; idx < json.size(); idx++)
		{
			char ch = json[idx];
			if (phase == Phase::kRaw)
			{
				if (ch == '\\')
				{
					phase = Phase::kEscaped;
				}
				else if (ch == '"')
				{
					idx += 1;
					break;
				}
				else
				{
					str += ch;
				}
			}
			else if (phase == Phase::kEscaped)
			{
				str += unescaped_char(ch);
				phase = Phase::kRaw;
			}
		}
		return {Object{str}, idx};
	}
	else if (json[0] == '[')
	{
		List res;
		std::size_t idx;
		for (idx = 1; idx < json.size();)
		{
			if (json[idx] == ']')
			{
				idx++;
				break;
			}

			auto [obj, eaten] = parse(json.substr(idx));
			if (eaten == 0)
			{
				idx = 0;
				break;
			}
			res.push_back(std::move(obj));
			idx += eaten;

			while (idx < json.size() &&
				   std::isspace(static_cast<std::uint8_t>(json[idx])))
			{
				idx++;
			}

			if (json[idx] == ',')
			{
				idx++;
			}
		}
		return {Object{res}, idx};
	}
	else if (json[0] == '{')
	{
		Dict res;
		std::size_t idx;
		for (idx = 1; idx < json.size();)
		{
			if (json[idx] == '}')
			{
				idx++;
				break;
			}

			auto [keyobj, key_eaten] = parse(json.substr(idx));
			if (key_eaten == 0)
			{
				idx = 0;
				break;
			}
			idx += key_eaten;
			if (!std::holds_alternative<std::string>(keyobj.inner))
			{
				idx = 0;
				break;
			}
			std::string key = std::move(std::get<std::string>(keyobj.inner));

			if (json[idx] == ':')
			{
				idx += 1;
			}

			auto [valobj, valeaten] = parse(json.substr(idx));
			if (valeaten == 0)
			{
				idx = 0;
				break;
			}
			idx += valeaten;

			res.try_emplace(std::move(key), std::move(valobj));

			while (idx < json.size() &&
				   std::isspace(static_cast<std::uint8_t>(json[idx])))
			{
				idx++;
			}

			if (json[idx] == ',')
			{
				idx++;
			}
		}
		return {Object{res}, idx};
	}

	return {Object{std::nullptr_t{}}, 0};
}

template <class... Fs> struct overloaded : public Fs...
{
	using Fs::operator()...;
};

template <typename... Funcs> overloaded(Funcs...) -> overloaded<Funcs...>;

inline void dump_json(const Object& obj)
{
	std::visit(overloaded{[&](std::nullptr_t val) { std::cout << "null"; },
						  [&](bool val) { std::cout << std::boolalpha << val; },
						  [&](std::int64_t val) { std::cout << val; },
						  [&](double val) { std::cout << val; },
						  [&](const std::string& val)
						  { std::cout << '"' << val << '"'; },
						  [&](const List& val)
						  {
							  std::cout << '[';
							  if (!val.empty())
							  {
								  auto it = val.begin();
								  dump_json(*it);
								  for (++it; it != val.end(); ++it)
								  {
									  std::cout << ", ";
									  dump_json(*it);
								  }
							  }
							  std::cout << ']';
						  },
						  [&](const Dict& val)
						  {
							  std::cout << '{';
							  if (!val.empty())
							  {
								  auto it = val.begin();
								  std::cout << '"' << it.first() << '"' << ": ";
								  dump_json(it.second());
								  for (++it; it != val.end(); ++it)
								  {
									  std::cout << ", " << '"' << it.first()
												<< '"' << ": ";
									  dump_json(it.second());
								  }
							  }
							  std::cout << '}';
						  }} // namespace json
			   ,
			   obj.inner);
} // namespace netx
} // namespace json
} // namespace netx

#endif