#ifndef NETX_JSON_SERIALIZE_HPP
#define NETX_JSON_SERIALIZE_HPP

#include "netx/json/object.hpp"
#include <concepts>
#include <string>
#include <type_traits>
namespace netx
{
namespace json
{
#define REFLECT__PP_FOREACH_1(f, _1) f(_1)
#define REFLECT__PP_FOREACH_2(f, _1, _2) f(_1) f(_2)
#define REFLECT__PP_FOREACH_3(f, _1, _2, _3) f(_1) f(_2) f(_3)
#define REFLECT__PP_FOREACH_4(f, _1, _2, _3, _4) f(_1) f(_2) f(_3) f(_4)
#define REFLECT__PP_FOREACH_5(f, _1, _2, _3, _4, _5)                           \
	f(_1) f(_2) f(_3) f(_4) f(_5)
#define REFLECT__PP_FOREACH_6(f, _1, _2, _3, _4, _5, _6)                       \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6)
#define REFLECT__PP_FOREACH_7(f, _1, _2, _3, _4, _5, _6, _7)                   \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7)
#define REFLECT__PP_FOREACH_8(f, _1, _2, _3, _4, _5, _6, _7, _8)               \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8)
#define REFLECT__PP_FOREACH_9(f, _1, _2, _3, _4, _5, _6, _7, _8, _9)           \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9)
#define REFLECT__PP_FOREACH_10(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10)     \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10)
#define REFLECT__PP_FOREACH_11(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11)                                            \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11)
#define REFLECT__PP_FOREACH_12(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12)                                       \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12)
#define REFLECT__PP_FOREACH_13(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13)                                  \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13)
#define REFLECT__PP_FOREACH_14(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14)                             \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14)
#define REFLECT__PP_FOREACH_15(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15)                        \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15)
#define REFLECT__PP_FOREACH_16(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16)                   \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16)
#define REFLECT__PP_FOREACH_17(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17)              \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17)
#define REFLECT__PP_FOREACH_18(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18)         \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18)
#define REFLECT__PP_FOREACH_19(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19)    \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19)
#define REFLECT__PP_FOREACH_20(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20)                                            \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20)
#define REFLECT__PP_FOREACH_21(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21)                                       \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21)
#define REFLECT__PP_FOREACH_22(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22)                                  \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)
#define REFLECT__PP_FOREACH_23(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23)                             \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23)
#define REFLECT__PP_FOREACH_24(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23, _24)                        \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24)
#define REFLECT__PP_FOREACH_25(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23, _24, _25)                   \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25)
#define REFLECT__PP_FOREACH_26(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23, _24, _25, _26)              \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26)
#define REFLECT__PP_FOREACH_27(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23, _24, _25, _26, _27)         \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26) f(_27)
#define REFLECT__PP_FOREACH_28(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23, _24, _25, _26, _27, _28)    \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)
#define REFLECT__PP_FOREACH_29(                                                \
	f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,  \
	_17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29)           \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26) f(_27) f(_28) f(_29)
#define REFLECT__PP_FOREACH_30(                                                \
	f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,  \
	_17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30)      \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26) f(_27) f(_28) f(_29) f(_30)
#define REFLECT__PP_FOREACH_31(                                                \
	f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,  \
	_17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31) \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26) f(_27) f(_28) f(_29) f(_30) f(_31)
#define REFLECT__PP_FOREACH_32(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,     \
							   _11, _12, _13, _14, _15, _16, _17, _18, _19,    \
							   _20, _21, _22, _23, _24, _25, _26, _27, _28,    \
							   _29, _30, _31, _32)                             \
	f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) \
		f(_13) f(_14) f(_15) f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)  \
			f(_23) f(_24) f(_25) f(_26) f(_27) f(_28) f(_29) f(_30) f(_31)     \
				f(_32)
#define REFLECT__PP_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11,   \
							   _12, _13, _14, _15, _16, _17, _18, _19, _20,    \
							   _21, _22, _23, _24, _25, _26, _27, _28, _29,    \
							   _30, _31, _32, N, ...)                          \
	N
#define REFLECT__PP_NARGS(...)                                                 \
	REFLECT__PP_NARGS_IMPL(__VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24,    \
						   23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, \
						   10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#define REFLECT__PP_EXPAND_2(...) __VA_ARGS__
#define REFLECT__PP_EXPAND(...) REFLECT__PP_EXPAND_2(__VA_ARGS__)
#define REFLECT__PP_CONCAT_2(x, y) x##y
#define REFLECT__PP_CONCAT(x, y) REFLECT__PP_CONCAT_2(x, y)
#define REFLECT__PP_FOREACH(f, ...)                                            \
	REFLECT__PP_EXPAND(REFLECT__PP_CONCAT(                                     \
		REFLECT__PP_FOREACH_, REFLECT__PP_NARGS(__VA_ARGS__))(f, __VA_ARGS__))

template <class T> struct reflect_trait
{
	static constexpr bool has_member()
	{
		return false;
	}
};

#define REFLECT__TYPE_PER_MEMBER_PTR(x) func(#x, &This::x);

#define REFLECT_TYPE(Type, ...)                                                \
	template <> struct netx::json::reflect_trait<Type>                         \
	{                                                                          \
		using This = Type;                                                     \
		static constexpr bool has_member()                                     \
		{                                                                      \
			return true;                                                       \
		};                                                                     \
		template <class Func>                                                  \
		static constexpr void foreach_member_ptr(Func&& func)                  \
		{                                                                      \
			REFLECT__PP_FOREACH(REFLECT__TYPE_PER_MEMBER_PTR, __VA_ARGS__)     \
		}                                                                      \
	};

#define REFLECT__TYPE_TEMPLATED_FIRST(T, ...) T
#define REFLECT__TYPE_TEMPLATED_REST(T, ...) __VA_ARGS__

#define REFLECT_TYPE_TEMPLATED(TypeArgs, ...)                                  \
	template <REFLECT__PP_EXPAND(REFLECT__TYPE_TEMPLATED_REST TypeArgs)>       \
	struct netx::json::reflect_trait<REFLECT__PP_EXPAND(                       \
		REFLECT__TYPE_TEMPLATED_FIRST TypeArgs)>                               \
	{                                                                          \
		using This =                                                           \
			REFLECT__PP_EXPAND(REFLECT__TYPE_TEMPLATED_FIRST TypeArgs);        \
		static constexpr bool has_member()                                     \
		{                                                                      \
			return true;                                                       \
		}                                                                      \
		template <class Func>                                                  \
		static constexpr void foreach_member_ptr(Func&& func)                  \
		{                                                                      \
			REFLECT__PP_FOREACH(REFLECT__TYPE_PER_MEMBER_PTR, __VA_ARGS__)     \
		}                                                                      \
	};

template <typename T>
concept IsReflected = reflect_trait<std::remove_cvref_t<T>>::has_member();

template <typename T>
concept IsIterable = requires(T t) {
	{ t.begin() } -> std::input_iterator;
	{ t.end() } -> std::input_iterator;
} && !std::same_as<std::remove_cvref_t<T>, std::string>;

inline Object to_json_object(Null v)
{
	return to_object(v);
}
inline Object to_json_object(Boolean v)
{
	return to_object(v);
}
inline Object to_json_object(const String& v)
{
	return to_object(v);
}

template <typename T>
	requires(std::is_integral_v<T> &&
			 !std::same_as<std::remove_cvref_t<T>, bool>)
inline Object to_json_object(T v)
{
	return to_object(static_cast<Integer>(v));
}

template <typename T>
	requires std::is_floating_point_v<T>
inline Object to_json_object(T v)
{
	return to_object(static_cast<Float>(v));
}

template <IsReflected T> Object to_json_object(const T& object);
template <IsIterable T> Object to_json_object(const T& val);
inline Object to_json_object(const Object& obj);

template <IsReflected T> Object to_json_object(const T& object)
{
	Dict dict;
	using Trait = reflect_trait<std::remove_cvref_t<T>>;

	Trait::foreach_member_ptr(
		[&](const char* key, auto member_ptr)
		{ dict.try_emplace(key, to_json_object(object.*member_ptr)); });

	return Object{dict};
}

template <IsIterable T> Object to_json_object(const T& val)
{
	List list;
	for (const auto& item : val)
	{
		list.push_back(to_json_object(item));
	}
	return Object{list};
}

inline Object to_json_object(const Object& obj)
{
	return obj;
}

template <typename T> std::string serialize(const T& object)
{
	return to_json_object(object).to_styled_string();
}
} // namespace json
} // namespace netx

#endif