#pragma once

#include "netx/http/parser.hpp"
#include "netx/http/request.hpp"
#include "netx/net/buffer.hpp"

namespace netx::http::details {
class Session {
  public:
    bool completed() const noexcept {
        return parser_.completed();
    }

    Request &req() noexcept {
        return parser_.req;
    }

    void clear() {
        parser_.clear();
    }

    bool parse(net::details::Buffer &buf);

    Parser::State parser_state() const noexcept {
        return parser_.state;
    }

    Session() = default;
    Session(Session &&) = default;
    ~Session() = default;

  private:
    Parser parser_{};
};

inline bool Session::parse(net::details::Buffer &buf) {
    const auto consumed = parser_.feed(buf.peek_string());
    if (!consumed) {
        return false;
    }

    // feed 只吃掉属于当前报文的字节；流水线里剩下的留给下一次调用
    buf.retrieve(*consumed);
    return true;
}
} // namespace netx::http::details