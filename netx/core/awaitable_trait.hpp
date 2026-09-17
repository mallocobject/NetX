#pragma once

#include "netx/core/expected.hpp"
#include "netx/core/non_void_helper.hpp"
#include "netx/core/task.hpp"

#include <type_traits>
#include <utility>

namespace netx::core::details {
template <typename A>
decltype(auto) await_resume_of(A &&a) {
    if constexpr (requires { std::forward<A>(a).await_resume(); }) {
        return std::forward<A>(a).await_resume();
    } else {
        return std::forward<A>(a).operator co_await().await_resume();
    }
}

/// awaitable 的结果类型（可能带 Expected 包裹），以及剥掉包裹后的值类型。
template <typename A>
struct AwaitableTrait {
    using RetType =
        std::decay_t<decltype(await_resume_of(std::declval<A &>()))>;
    using ValueType = RetType;
};

template <typename T>
struct AwaitableTrait<Task<T>> {
    using RetType = T;
    using ValueType = T;
};

template <typename T>
struct AwaitableTrait<Task<Expected<T>>> {
    using RetType = Expected<T>;
    using ValueType = T;
};

template <typename T>
struct AwaitableTrait<Expected<T>> {
    using RetType = Expected<T>;
    using ValueType = T;
};

template <typename T>
using NonVoidRetType =
    typename NonVoidHelper<typename AwaitableTrait<T>::RetType>::Type;

template <typename T>
using UnpackedRetType =
    typename NonVoidHelper<typename AwaitableTrait<T>::ValueType>::Type;
} // namespace netx::core::details
