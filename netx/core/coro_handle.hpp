#pragma once

#include "netx/core/event_loop.hpp"
#include "netx/core/handle.hpp"

namespace netx::core::details {
struct CoroHandle : public Handle {
    /// 交给当前线程的事件循环执行
    void schedule() {
        EventLoop::loop().call_soon(*this);
    }

    /// 请求取消：未开始的不会被执行；挂起中的会以 Error::Cancelled 恢复。
    /// 取消会沿"我正在等谁"这条边向下传递到整条等待链。
    void request_cancel() {
        EventLoop::loop().cancel(*this);
    }

    /// 强制回到事件循环，绕开 call_soon 里"已取消的 handle 不再入队"的守卫。
    void wake() {
        EventLoop::loop().wake(*this);
    }

    /// 放弃调度：撤销一切挂起记录但**不**唤醒，供即将销毁协程帧的持有者调用。
    /// 用 request_cancel() 会把挂起中的协程重新排进就绪队列，紧接着
    /// destroy() 就会让那条记录指向已释放的帧。
    void detach() noexcept {
        EventLoop::loop().detach(*this);
    }
};
} // namespace netx::core::details