#pragma once

#include "netx/http/request.hpp"
#include <cassert>
#include <cstdint>
#include <string>
namespace netx
{
namespace http
{
namespace details
{
struct Parser
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

	std::size_t body_remaining() const noexcept
	{
		assert(state == State::kBody);
		return body_remaining_;
	}

	bool completed() const noexcept
	{
		return state == State::kComplete;
	}

	void clear()
	{
		state = State::kStart;
		tmp_key_.clear();
		tmp_value_.clear();
		body_remaining_ = 0;

		req.clear();
	}

	void append_body(const char* data, std::size_t len)
	{
		req.body.append(data, len);
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

	Parser() = default;
	Parser(Parser&&) = default;
	~Parser() = default;

	State state{Parser::State::kStart};
	Request req{};

  private:
	std::string tmp_key_;
	std::string tmp_value_;
	std::size_t body_remaining_{0};
};

inline bool Parser::consume(char c)
{
	switch (state)
	{
	case State::kStart:
		if (std::isalpha(c))
		{
			req.method += c;
			state = State::kMethod;
		}
		else if (c != '\r' && c != '\n')
		{
			return false;
		}
		break;

	case State::kMethod:
		if (c == ' ')
		{
			state = State::kPath;
		}
		else
		{
			req.method += c;
		}
		break;

	case State::kPath:
		if (req.url_path.size() > 1024)
		{
			return false;
		}

		if (c == '?')
		{
			state = State::kQueryKey;
		}
		else if (c == '#')
		{
			state = State::kFragment;
		}
		else if (c == ' ')
		{
			state = State::kVersion;
		}
		else
		{
			req.url_path += c;
		}
		break;

	case State::kQueryKey:
		if (c == '=')
		{
			state = State::kQueryValue;
		}
		else if (c == ' ')
		{
			state = State::kVersion;
		}
		else
		{
			tmp_key_ += c;
		}
		break;

	case State::kQueryValue:
		if (c == '&')
		{
			req.query_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state = State::kQueryKey;
		}
		else if (c == '#')
		{
			req.query_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state = State::kFragment;
		}
		else if (c == ' ')
		{
			req.query_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state = State::kVersion;
		}
		else
		{
			tmp_value_ += c;
		}
		break;

	case State::kFragment:
		if (c == ' ')
		{
			state = State::kVersion;
		}
		break;

	case State::kVersion:
		if (c == '\r')
		{
			state = State::kExpectLfAfterStatusLine;
		}
		else
		{
			req.version += c;
		}
		break;

	case State::kExpectLfAfterStatusLine:
		if (c == '\n')
		{
			state = State::kHeaderKey;
		}
		else
		{
			return false;
		}
		break;

	case State::kHeaderKey:
		if (c == ':')
		{
			state = State::kHeaderValue;
		}
		else if (c == '\r')
		{
			state = State::kExpectDoubleLf;
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

			req.header_params[tmp_key_] = tmp_value_;
			tmp_key_.clear();
			tmp_value_.clear();
			state = State::kExpectLfAfterHeader;
		}
		else
		{
			tmp_value_ += c;
		}
		break;

	case State::kExpectLfAfterHeader:
		if (c == '\n')
		{
			state = State::kHeaderKey;
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
			if (auto it = req.header_params.find("content-length");
				it != req.header_params.end())
			{
				try
				{
					body_remaining_ = std::stoul(it->second);
					if (body_remaining_ > 10 * 1024 * 1024)
					{
						return false; // 限制Body最大10MB
					}

					req.body.reserve(body_remaining_);
					state =
						(body_remaining_ > 0) ? State::kBody : State::kComplete;
				}
				catch (...)
				{
					return false; // 非法数字
				}
			}
			else
			{
				state = State::kComplete;
			}
		}
		else
		{
			return false;
		}
		break;

	case State::kBody:
		req.body += c;
		if (--body_remaining_ == 0)
		{
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