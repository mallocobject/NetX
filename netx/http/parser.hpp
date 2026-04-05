#ifndef NETX_HTTP_PARSER_HPP
#define NETX_HTTP_PARSER_HPP

#include "netx/http/request.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
namespace netx
{
namespace http
{
struct HttpParser
{
	enum class State : std::uint8_t
	{
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

	State state() const noexcept
	{
		return state_;
	}

	void set_state(State state) noexcept
	{
		state_ = state;
	}

	std::size_t body_remaining() const noexcept
	{
		assert(state_ == State::kBody);
		return body_remaining_;
	}

	bool completed() const noexcept
	{
		return state_ == State::kComplete;
	}

	HttpRequest* req() noexcept
	{
		return &req_;
	}

	void clear()
	{
		state_ = State::kStart;
		tmp_key_.clear();
		tmp_value_.clear();
		body_remaining_ = 0;

		req_.clear();
	}

	void append_body(const char* data, std::size_t len)
	{
		req_.body.append(data, len);
		body_remaining_ -= len;
	}

	bool parse(const std::string& data)
	{
		for (char c : data)
		{
			if (!consume(c))
			{
				return false;
			}
		}

		return true;
	}

	bool consume(char c);

	HttpParser() = default;
	HttpParser(HttpParser&&) = delete;
	~HttpParser() = default;

  private:
	State state_{HttpParser::State::kStart};
	std::string tmp_key_;
	std::string tmp_value_;
	std::size_t body_remaining_{0};

	HttpRequest req_{};
};

inline bool HttpParser::consume(char c)
{
	switch (state_)
	{
	case State::kStart:
		if (std::isalpha(c))
		{
			req_.method += c;
			state_ = State::kMethod;
		}
		else if (c != '\r' && c != '\n')
		{
			return false;
		}
		break;

	case State::kMethod:
		if (c == ' ')
		{
			state_ = State::kPath;
		}
		else
		{
			req_.method += c;
		}
		break;

	case State::kPath:
		if (req_.path.size() > 1024)
		{
			return false;
		}

		if (c == '?')
		{
			state_ = State::kQueryKey;
		}
		else if (c == '#')
		{
			state_ = State::kFragment;
		}
		else if (c == ' ')
		{
			state_ = State::kVersion;
		}
		else
		{
			req_.path += c;
		}
		break;

	case State::kQueryKey:
		if (c == '=')
		{
			state_ = State::kQueryValue;
		}
		else if (c == ' ')
		{
			state_ = State::kVersion;
		}
		else
		{
			tmp_key_ += c;
		}
		break;

	case State::kQueryValue:
		if (c == '&')
		{
			req_.query_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state_ = State::kQueryKey;
		}
		else if (c == '#')
		{
			req_.query_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state_ = State::kFragment;
		}
		else if (c == ' ')
		{
			req_.query_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state_ = State::kVersion;
		}
		else
		{
			tmp_value_ += c;
		}
		break;

	case State::kFragment:
		if (c == ' ')
		{
			state_ = State::kVersion;
		}
		break;

	case State::kVersion:
		if (c == '\r')
		{
			state_ = State::kExpectLfAfterStatusLine;
		}
		else
		{
			req_.version += c;
		}
		break;

	case State::kExpectLfAfterStatusLine:
		if (c == '\n')
		{
			state_ = State::kHeaderKey;
		}
		else
		{
			return false;
		}
		break;

	case State::kHeaderKey:
		if (c == ':')
		{
			state_ = State::kHeaderValue;
		}
		else if (c == '\r')
		{
			state_ = State::kExpectDoubleLf;
		}
		else
		{
			tmp_key_ += std::tolower(c);
		}
		break;

	case State::kHeaderValue:
		if (c == ' ' && tmp_value_.empty())
		{
			break;
		}
		if (c == '\r')
		{

			req_.header_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state_ = State::kExpectLfAfterHeader;
		}
		else
		{
			tmp_value_ += c;
		}
		break;

	case State::kExpectLfAfterHeader:
		if (c == '\n')
		{
			state_ = State::kHeaderKey;
		}
		else
		{
			return false;
		}
		break;

	case State::kExpectDoubleLf:
		if (c == '\n')
		{
			// 检查是否有 Content-Length 决定是否需要解析 Body
			if (auto it = req_.header_params.find("content-length");
				it != req_.header_params.end())
			{
				body_remaining_ = std::stol(it->second);
				req_.body.reserve(body_remaining_);
				state_ =
					(body_remaining_ > 0) ? State::kBody : State::kComplete;
			}
			else
			{
				state_ = State::kComplete;
			}
		}
		else
		{
			return false;
		}
		break;

	case State::kBody:
		req_.body += c;
		if (--body_remaining_ == 0)
		{
			state_ = State::kComplete;
		}
		break;

	default:
		return false;
	}

	return true;
}
} // namespace http
} // namespace netx

#endif