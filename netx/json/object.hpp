#ifndef NETX_JSON_OBJECT_HPP
#define NETX_JSON_OBJECT_HPP

#include "netx/meta/ordered_map.hpp"
#include <format>
#include <iterator>
#include <string>
#include <variant>
#include <vector>
namespace netx
{
namespace json
{
namespace meta = netx::meta;

struct Object;

using Null = std::nullptr_t;
using Boolean = bool;
using Integer = int64_t;
using Float = double;
using String = std::string;
using List = std::vector<Object>;
using Dict = meta::OrderedMap<std::string, Object>;

struct Object
{
	std::variant<Null, Boolean, Integer, Float, String, List, Dict> inner;

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

	std::string to_styled_string() const;
};

template <class... Fs> struct overloaded : public Fs...
{
	using Fs::operator()...;
};

template <typename... Funcs> overloaded(Funcs...) -> overloaded<Funcs...>;

inline std::string Object::to_styled_string() const
{
	std::string res;
	auto out = std::back_inserter(res);
	std::visit(
		overloaded{
			[&out](Null) { std::format_to(out, "null"); }, [&out](Boolean val)
			{ std::format_to(out, "{}", val ? "true" : "false"); },
			[&out](Integer val) { std::format_to(out, "{:d}", val); },
			[&out](Float val) { std::format_to(out, "{:g}", val); },
			[&out](const String& val) { std::format_to(out, "\"{:s}\"", val); },
			[&out](const List& val)
			{
				std::format_to(out, "[");
				if (!val.empty())
				{
					auto it = val.begin();
					std::format_to(out, "{}", it->to_styled_string());
					for (++it; it != val.end(); ++it)
					{
						std::format_to(out, ", {}", it->to_styled_string());
					}
				}
				std::format_to(out, "]");
			},
			[&out](const Dict& val)
			{
				std::format_to(out, "{{");
				if (!val.empty())
				{
					auto it = val.begin();
					std::format_to(out, "\"{}\": {}", it.first(),
								   it.second().to_styled_string());
					for (++it; it != val.end(); ++it)
					{
						std::format_to(out, ", \"{}\": {}", it.first(),
									   it.second().to_styled_string());
					}
				}
				std::format_to(out, "}}");
			}} // namespace json
		,
		inner);

	return res;
}

inline Object to_object(Null n)
{
	return {n};
}
inline Object to_object(Boolean b)
{
	return {b};
}
inline Object to_object(Integer i)
{
	return {i};
}
inline Object to_object(Float f)
{
	return {f};
}
inline Object to_object(const std::string& s)
{
	return {s};
}
inline Object to_object(const List& l)
{
	return {l};
}
inline Object to_object(const Dict& d)
{
	return {d};
}
} // namespace json
} // namespace netx

#endif