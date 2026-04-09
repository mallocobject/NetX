#pragma once

#include "netx/core/non_void_helper.hpp"
namespace netx
{
namespace core
{
template <typename T> struct Task;
template <typename T> struct Expected;

template <typename A> struct AwaitableTrait
{
	using RetType = A;
};

template <typename T> struct AwaitableTrait<Task<T>>
{
	using RetType = T;
};

template <typename T> struct AwaitableTrait<Task<Expected<T>>>
{
	using RetType = Expected<T>;
	using ValueType = T;
};

template <typename T>
using NonVoidRetType =
	typename NonVoidHelper<typename AwaitableTrait<T>::RetType>::Type;

template <typename T>
using UnpackedRetType =
	typename NonVoidHelper<typename AwaitableTrait<T>::ValueType>::Type;

} // namespace core
} // namespace netx