#pragma once

#include "netx/http/parser.hpp"
#include "netx/http/request.hpp"
#include "netx/net/buffer.hpp"
namespace netx
{
namespace http
{
namespace details
{
struct Session
{
	bool completed() const noexcept
	{
		return parser_.completed();
	}

	Request& req() noexcept
	{
		return parser_.req;
	}

	void clear()
	{
		parser_.clear();
	}

	bool parse(net::details::Buffer& buf);

	Parser::State parser_state() const noexcept
	{
		return parser_.state;
	}

	Session() = default;
	Session(Session&&) = default;
	~Session() = default;

  private:
	Parser parser_{};
};

inline bool Session::parse(net::details::Buffer& buf)
{
	while (buf.readable_bytes() > 0)
	{
		if (parser_.state == Parser::State::kBody)
		{
			size_t n = std::min(buf.readable_bytes(), parser_.body_remaining());
			parser_.append_body(buf.peek(), n);

			buf.retrieve(n);

			if (parser_.body_remaining() == 0)
			{
				parser_.state = Parser::State::kComplete;
				return true;
			}
			continue;
		}

		char c = *buf.peek();
		if (!parser_.consume(c))
		{
			return false;
		}

		buf.retrieve(1);

		if (parser_.completed())
		{
			return true;
		}
	}
	return true;
}
} // namespace details
} // namespace http
} // namespace netx