#pragma once

#include "netx/http/request.hpp"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <string>
namespace netx {
namespace http {
namespace details {
struct Parser {
    // 各字段的长度上限。原来只有 url_path 和 body 有上限，method 与
    // header 完全没有 —— 客户端发 20 万字节的 header key、1 万个 header
    // 行，服务端都会照单全收，内存随请求线性增长。
    inline static constexpr size_t kMaxMethodLen = 16;      // 最长标准方法 CONNECT
    inline static constexpr size_t kMaxVersionLen = 16;
    inline static constexpr size_t kMaxUrlPathLen = 1024;
    inline static constexpr size_t kMaxQueryKeyLen = 1024;
    inline static constexpr size_t kMaxQueryValueLen = 4096;
    inline static constexpr size_t kMaxHeaderKeyLen = 256;
    inline static constexpr size_t kMaxHeaderValueLen = 4096;
    inline static constexpr size_t kMaxHeaderCount = 100;
    inline static constexpr size_t kMaxBodyLen = 10 * 1024 * 1024;

    enum class State : std::uint8_t {
        kStart,
        kMethod,
        kPath,
        kQueryKey,
        kQueryValue,
        kFragment,
        kVersion,
        kExpectLfAfterStatusLine,
        kHeaderKey,
        kHeaderValue,
        kExpectLfAfterHeader,
        kExpectDoubleLf,
        kBody,
        kComplete,
        kError
    };

    size_t body_remaining() const noexcept {
        assert(state == State::kBody);
        return body_remaining_;
    }

    bool completed() const noexcept {
        return state == State::kComplete;
    }

    void clear() {
        state = State::kStart;
        tmp_key_.clear();
        tmp_value_.clear();
        body_remaining_ = 0;

        req.clear();
    }

    void append_body(const char *data, size_t len) {
        req.body.append(data, len);
        body_remaining_ -= len;
    }

    bool parse(const std::string &data) {
        for (char c : data) {
            if (!consume(c)) {
                return false;
            }
        }

        return true;
    }

    bool consume(char c);

    Parser() = default;
    Parser(Parser &&) = default;
    ~Parser() = default;

    State state{Parser::State::kStart};
    Request req{};

  private:
    std::string tmp_key_;
    std::string tmp_value_;
    size_t body_remaining_{0};
};

inline bool Parser::consume(char c) {
    switch (state) {
    case State::kStart:
        // ctype 系列只接受 EOF 或 unsigned char 范围内的值：直接传 char
        // 时，>= 0x80 的字节会变成负数，是未定义行为
        if (std::isalpha(static_cast<unsigned char>(c))) {
            req.method += c;
            state = State::kMethod;
        } else if (c != '\r' && c != '\n') {
            return false;
        }
        break;

    case State::kMethod:
        if (c == ' ') {
            state = State::kPath;
        } else if (c == '\r' || c == '\n' || req.method.size() >= kMaxMethodLen) {
            // 请求行是 "METHOD SP PATH SP VERSION CRLF"。method 里出现 CR/LF
            // 说明这行根本不是请求行；再加上长度上限，才不会让一段没有空格
            // 的垃圾把连接拖住。
            return false;
        } else {
            req.method += c;
        }
        break;

    case State::kPath:
        if (req.url_path.size() > kMaxUrlPathLen) {
            return false;
        }

        if (c == '?') {
            state = State::kQueryKey;
        } else if (c == '#') {
            state = State::kFragment;
        } else if (c == ' ') {
            state = State::kVersion;
        } else {
            req.url_path += c;
        }
        break;

    case State::kQueryKey:
        if (c == '=') {
            state = State::kQueryValue;
        } else if (c == ' ') {
            state = State::kVersion;
        } else if (tmp_key_.size() >= kMaxQueryKeyLen) {
            return false;
        } else {
            tmp_key_ += c;
        }
        break;

    case State::kQueryValue:
        if (c == '&') {
            req.query_params[tmp_key_] = tmp_value_;
            tmp_key_.clear();
            tmp_value_.clear();
            state = State::kQueryKey;
        } else if (c == '#') {
            req.query_params[tmp_key_] = tmp_value_;
            tmp_key_.clear();
            tmp_value_.clear();
            state = State::kFragment;
        } else if (c == ' ') {
            req.query_params[tmp_key_] = tmp_value_;
            tmp_key_.clear();
            tmp_value_.clear();
            state = State::kVersion;
        } else if (tmp_value_.size() >= kMaxQueryValueLen) {
            return false;
        } else {
            tmp_value_ += c;
        }
        break;

    case State::kFragment:
        if (c == ' ') {
            state = State::kVersion;
        }
        break;

    case State::kVersion:
        if (c == '\r') {
            state = State::kExpectLfAfterStatusLine;
        } else if (req.version.size() >= kMaxVersionLen) {
            return false;
        } else {
            req.version += c;
        }
        break;

    case State::kExpectLfAfterStatusLine:
        if (c == '\n') {
            state = State::kHeaderKey;
        } else {
            return false;
        }
        break;

    case State::kHeaderKey:
        if (c == ':') {
            state = State::kHeaderValue;
        } else if (c == '\r') {
            state = State::kExpectDoubleLf;
        } else if (tmp_key_.size() >= kMaxHeaderKeyLen) {
            return false;
        } else {
            tmp_key_ +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        break;

    case State::kHeaderValue:
        if (c == ' ' && tmp_value_.empty()) {
            break;
        }
        if (c == '\r') {
            // header 条数也要有上限：一条请求里塞 1 万个 header 同样能把
            // 内存吃光
            if (req.header_params.size() >= kMaxHeaderCount) {
                return false;
            }

            req.header_params[tmp_key_] = tmp_value_;
            tmp_key_.clear();
            tmp_value_.clear();
            state = State::kExpectLfAfterHeader;
        } else if (tmp_value_.size() >= kMaxHeaderValueLen) {
            return false;
        } else {
            tmp_value_ += c;
        }
        break;

    case State::kExpectLfAfterHeader:
        if (c == '\n') {
            state = State::kHeaderKey;
        } else {
            return false;
        }
        break;

    case State::kExpectDoubleLf:
        if (c == '\n') {
            // 检查是否有 Content-Length 决定是否需要解析 Body
            if (auto it = req.header_params.find("content-length");
                it != req.header_params.end()) {
                try {
                    body_remaining_ = std::stoul(it->second);
                    if (body_remaining_ > kMaxBodyLen) {
                        return false;
                    }

                    req.body.reserve(body_remaining_);
                    state =
                        (body_remaining_ > 0) ? State::kBody : State::kComplete;
                } catch (...) {
                    return false; // 非法数字
                }
            } else {
                state = State::kComplete;
            }
        } else {
            return false;
        }
        break;

    case State::kBody:
        req.body += c;
        if (--body_remaining_ == 0) {
            state = State::kComplete;
        }
        break;

    default:
        return false;
    }

    return true;
}
} // namespace details
} // namespace http
} // namespace netx