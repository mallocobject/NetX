#pragma once

#include "netx/http/request.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace netx {
namespace http {
namespace details {

/// HTTP/1.x 请求解析器。
///
/// 按"状态 + 整段扫描"实现：每个状态在自己的输入里找结束符，把中间那一段
/// 一次性追加上去。原实现逐字节 `+=` 到 std::string（perf 里
/// string::push_back 占 3.6%、consume 占 2.5%），而且每加几个字符就可能触发
/// 一次扩容。
///
/// 一次 feed 最多解析一个完整报文 —— 报文边界就是调用方需要的边界：流水线
/// 里剩下的字节属于下一个请求，必须留在缓冲区里。
struct Parser {
    // 各字段的长度上限。客户端能用超长字段把服务端内存吃光，所以 method、
    // version、header 键值、查询串都要有上限，不能只有 url_path 和 body。
    inline static constexpr size_t kMaxMethodLen = 16; // 最长标准方法 CONNECT
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

    /// 扫描一段数据。
    ///
    /// 返回吃掉的字节数：报文已经结束（kComplete）时可能小于 chunk.size()，
    /// 剩下的属于下一个请求。返回 nullopt 表示语法错误，调用方应当回 400
    /// 并关闭连接。
    std::optional<size_t> feed(std::string_view chunk);

    /// 喂完一整段，只看合不合法。
    bool parse(const std::string &data) {
        return feed(data).has_value();
    }

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

inline std::optional<size_t> Parser::feed(std::string_view chunk) {
    constexpr size_t npos = std::string_view::npos;
    const size_t n = chunk.size();
    size_t i = 0;

    // 追加一段并对累计长度设限
    const auto append_capped =
        [](std::string &dst, std::string_view sv, size_t cap) {
            if (dst.size() + sv.size() > cap) {
                return false;
            }
            dst.append(sv);
            return true;
        };

    while (i < n) {
        switch (state) {
        case State::kStart:
            // 报文之间允许夹前导 CR/LF；第一个可见字节必须是方法名的字母
            while (i < n && (chunk[i] == '\r' || chunk[i] == '\n')) {
                ++i;
            }
            if (i == n) {
                return i;
            }
            if (!std::isalpha(static_cast<unsigned char>(chunk[i]))) {
                return std::nullopt;
            }
            state = State::kMethod;
            break;

        case State::kMethod: {
            const size_t end = chunk.find_first_of(" \r\n", i);
            const size_t stop = (end == npos) ? n : end;
            if (!append_capped(
                    req.method, chunk.substr(i, stop - i), kMaxMethodLen)) {
                return std::nullopt;
            }
            i = stop;
            if (i == n) {
                return i;
            }
            if (chunk[i] != ' ') {
                // 请求行是 "METHOD SP PATH SP VERSION CRLF"：这里出现
                // CR/LF 说明整行根本不是请求行
                return std::nullopt;
            }
            ++i;
            state = State::kPath;
            break;
        }

        case State::kPath: {
            const size_t end = chunk.find_first_of("?# ", i);
            const size_t stop = (end == npos) ? n : end;
            if (!append_capped(
                    req.url_path, chunk.substr(i, stop - i), kMaxUrlPathLen)) {
                return std::nullopt;
            }
            i = stop;
            if (i == n) {
                return i;
            }
            const char c = chunk[i++];
            if (c == '?') {
                state = State::kQueryKey;
            } else if (c == '#') {
                state = State::kFragment;
            } else {
                state = State::kVersion;
            }
            break;
        }

        case State::kQueryKey: {
            const size_t end = chunk.find_first_of("= ", i);
            const size_t stop = (end == npos) ? n : end;
            if (!append_capped(
                    tmp_key_, chunk.substr(i, stop - i), kMaxQueryKeyLen)) {
                return std::nullopt;
            }
            i = stop;
            if (i == n) {
                return i;
            }
            // 没有 '=' 就直接进版本号，这对键值不作数
            state = (chunk[i] == '=') ? State::kQueryValue : State::kVersion;
            ++i;
            break;
        }

        case State::kQueryValue: {
            const size_t end = chunk.find_first_of("&# ", i);
            const size_t stop = (end == npos) ? n : end;
            if (!append_capped(
                    tmp_value_, chunk.substr(i, stop - i), kMaxQueryValueLen)) {
                return std::nullopt;
            }
            i = stop;
            if (i == n) {
                return i;
            }
            req.query_params[tmp_key_] = tmp_value_;
            tmp_key_.clear();
            tmp_value_.clear();
            const char c = chunk[i++];
            if (c == '&') {
                state = State::kQueryKey;
            } else if (c == '#') {
                state = State::kFragment;
            } else {
                state = State::kVersion;
            }
            break;
        }

        case State::kFragment: {
            // 片段内容整段丢掉，只等分隔版本号的那个空格
            const size_t sp = chunk.find(' ', i);
            if (sp == npos) {
                return n;
            }
            i = sp + 1;
            state = State::kVersion;
            break;
        }

        case State::kVersion: {
            const size_t cr = chunk.find('\r', i);
            const size_t stop = (cr == npos) ? n : cr;
            if (!append_capped(
                    req.version, chunk.substr(i, stop - i), kMaxVersionLen)) {
                return std::nullopt;
            }
            i = stop;
            if (i == n) {
                return i;
            }
            ++i; // 吃掉 '\r'
            state = State::kExpectLfAfterStatusLine;
            break;
        }

        case State::kExpectLfAfterStatusLine:
            if (chunk[i] != '\n') {
                return std::nullopt;
            }
            ++i;
            state = State::kHeaderKey;
            break;

        case State::kHeaderKey: {
            const size_t end = chunk.find_first_of(":\r", i);
            const size_t stop = (end == npos) ? n : end;
            const size_t before = tmp_key_.size();
            if (!append_capped(
                    tmp_key_, chunk.substr(i, stop - i), kMaxHeaderKeyLen)) {
                return std::nullopt;
            }
            // 先整段追加再就地转小写：header 名大小写不敏感，逐字节 tolower
            // 又回到一个字符一次调用的老路上了
            for (size_t k = before; k < tmp_key_.size(); ++k) {
                tmp_key_[k] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(tmp_key_[k])));
            }
            i = stop;
            if (i == n) {
                return i;
            }
            const char c = chunk[i++];
            // ':' 进值；'\r' 说明这一行是空的，头部到此结束
            state = (c == ':') ? State::kHeaderValue : State::kExpectDoubleLf;
            break;
        }

        case State::kHeaderValue: {
            if (tmp_value_.empty()) {
                // 值前面的空白整段跳过
                const size_t j = chunk.find_first_not_of(" \t", i);
                if (j == npos) {
                    return n;
                }
                i = j;
            }
            const size_t cr = chunk.find('\r', i);
            const size_t stop = (cr == npos) ? n : cr;
            if (!append_capped(tmp_value_,
                               chunk.substr(i, stop - i),
                               kMaxHeaderValueLen)) {
                return std::nullopt;
            }
            i = stop;
            if (i == n) {
                return i;
            }
            if (req.header_params.size() >= kMaxHeaderCount) {
                return std::nullopt;
            }
            req.header_params[tmp_key_] = tmp_value_;
            tmp_key_.clear();
            tmp_value_.clear();
            ++i; // 吃掉 '\r'
            state = State::kExpectLfAfterHeader;
            break;
        }

        case State::kExpectLfAfterHeader:
            if (chunk[i] != '\n') {
                return std::nullopt;
            }
            ++i;
            state = State::kHeaderKey;
            break;

        case State::kExpectDoubleLf: {
            if (chunk[i] != '\n') {
                return std::nullopt;
            }
            ++i;

            auto it = req.header_params.find("content-length");
            if (it == req.header_params.end()) {
                state = State::kComplete;
                break;
            }

            // from_chars 比 stoul 合适：不抛异常、不看 locale，而且能顺带
            // 确认整个值都被消费掉了（"5abc" 必须算非法）
            unsigned long long len = 0;
            const char *first = it->second.data();
            const char *last = first + it->second.size();
            const auto [ptr, ec] = std::from_chars(first, last, len);
            if (ec != std::errc{} || ptr != last || len > kMaxBodyLen) {
                return std::nullopt;
            }

            body_remaining_ = static_cast<size_t>(len);
            req.body.reserve(body_remaining_);
            state = (body_remaining_ > 0) ? State::kBody : State::kComplete;
            break;
        }

        case State::kBody: {
            const size_t take = std::min(n - i, body_remaining_);
            req.body.append(chunk.substr(i, take));
            body_remaining_ -= take;
            i += take;
            if (body_remaining_ == 0) {
                state = State::kComplete;
            }
            break;
        }

        case State::kComplete:
        case State::kError:
            // 报文已结束：剩下的字节属于下一个请求，交给调用方处置
            return i;
        }
    }

    return i;
}
} // namespace details
} // namespace http
} // namespace netx
