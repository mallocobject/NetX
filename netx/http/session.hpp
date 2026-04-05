#ifndef NETX_HTTP_SESSION_HPP
#define NETX_HTTP_SESSION_HPP

#include "netx/http/parser.hpp"
#include "netx/http/request.hpp"
#include "netx/net/buffer.hpp"
namespace netx
{
namespace http
{
namespace net = netx::net;
struct Session
{
	bool completed() const noexcept
	{
		return parser_.completed();
	}

	HttpRequest* req() noexcept
	{
		return parser_.req();
	}

	void clear()
	{
		parser_.clear();
	}

	bool parse(net::Buffer* buf);

	HttpParser::State parser_state() const noexcept
	{
		return parser_.state();
	}

	Session() = default;
	Session(Session&&) = delete;
	~Session() = default;

  private:
	HttpParser parser_{};
};

inline bool Session::parse(net::Buffer* buf)
{
	while (buf->readableBytes() > 0)
	{
		if (parser_.state() == HttpParser::State::kBody)
		{
			size_t n = std::min(buf->readableBytes(), parser_.body_remaining());
			parser_.append_body(buf->peek(), n);

			buf->retrieve(n);

			if (parser_.body_remaining() == 0)
			{
				parser_.set_state(HttpParser::State::kComplete);
				return true;
			}
			continue;
		}

		char c = *buf->peek();
		if (!parser_.consume(c))
		{
			return false;
		}

		buf->retrieve(1);

		if (parser_.completed())
		{
			return true;
		}
	}
	return true;
}
} // namespace http
} // namespace netx

#endif