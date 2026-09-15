#pragma once

namespace netx::core::details {
template <typename T = void>
struct NonVoidHelper {
    using Type = T;
};

template <>
struct NonVoidHelper<void> {
    using Type = NonVoidHelper;
};
} // namespace netx::core::details