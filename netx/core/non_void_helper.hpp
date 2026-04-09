#pragma once

namespace netx
{
namespace core
{
template <typename T = void> struct NonVoidHelper
{
	using Type = T;
};

template <> struct NonVoidHelper<void>
{
	using Type = NonVoidHelper;
};
} // namespace core
} // namespace netx