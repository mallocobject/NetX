#pragma once

namespace netx
{
namespace core
{
namespace details
{
template <typename T = void> struct NonVoidHelper
{
	using Type = T;
};

template <> struct NonVoidHelper<void>
{
	using Type = NonVoidHelper;
};
} // namespace details
} // namespace core
} // namespace netx