#ifndef NETX_RPC_CLIENT_HPP
#define NETX_RPC_CLIENT_HPP

#include "elog/logger.hpp"
#include "netx/async/check_error.hpp"
#include "netx/async/scheduled_task.hpp"
#include "netx/async/task.hpp"
#include "netx/net/buffer.hpp"
#include "netx/net/inet_addr.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include "netx/rpc/serialize_traits.hpp"
#include <atomic>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace rpc
{
// RpcClient client("127.0.0.1", 8080);
// User result = client.call<User>("Echo", User{123, "Alice", {1,2}});

namespace async = netx::async;
namespace net = netx::net;
class RpcClient
{
	struct CallContext
	{
		// exclude header
		net::Buffer response_buffer{};
		std::exception_ptr exception;
		async::CoroHandle* coro_to_resume{nullptr};
	};

	struct RpcAwaiter
	{
		CallContext& ctx;

		bool await_ready() const noexcept
		{
			return false;
		}

		template <typename P>
		void await_suspend(std::coroutine_handle<P> coro) noexcept
		{
			coro.promise().setState(async::Handle::State::kSuspend);
			ctx.coro_to_resume = &coro.promise();
		}

		void await_resume() const
		{
			if (ctx.exception) [[unlikely]]
			{
				std::rethrow_exception(ctx.exception);
			}
		}
	};

	struct WriterWaitAwaiter
	{
		RpcClient* client;

		bool await_ready() const noexcept
		{
			return !client->write_queue_.empty();
		}

		template <typename P>
		void await_suspend(std::coroutine_handle<P> coro) noexcept
		{
			coro.promise().setState(async::Handle::State::kSuspend);
			client->writer_waiter_ = &coro.promise();
		}

		void await_resume() const noexcept
		{
		}
	};

  public:
	explicit RpcClient(const net::InetAddr& sock_addr)
		: stream_(async::checkError(net::Socket::socket(nullptr)), sock_addr)
	{
		while (!net::Socket::connect(stream_.read_fd(), sock_addr, nullptr))
		{
		}

		// background_reader_ = co_spawn(readLoop());
		static async::ScheduledTask<async::Task<>> background_reader(
			co_spawn(readLoop()));
		static async::ScheduledTask<async::Task<>> background_writer(
			co_spawn(writeLoop()));
	}

	explicit RpcClient(const std::string& ip, std::uint16_t port)
		: RpcClient(net::InetAddr{ip, port})
	{
	}

	RpcClient(RpcClient&& other) = delete;

	void close()
	{
		stream_.close();
	}

	template <typename Ret, typename... Args>
	async::Task<Ret> call(const std::string& method_name, Args&&... args);

  private:
	async::Task<> readLoop();
	async::Task<> writeLoop();

  private:
	net::Stream stream_;
	std::atomic<std::uint64_t> next_request_id_{1};
	std::unordered_map<std::uint64_t, CallContext*> pending_calls_;
	// ScheduledTask<Task<>> background_reader_;

	std::queue<net::Buffer> write_queue_;
	async::CoroHandle* writer_waiter_{nullptr};
};

template <typename Ret, typename... Args>
async::Task<Ret> RpcClient::call(const std::string& method_name, Args&&... args)
{
	uint64_t req_id = next_request_id_.fetch_add(1, std::memory_order_acq_rel);

	net::Buffer req_buf;
	std::size_t size_before = req_buf.readableBytes();
	SerializeTraits<std::string>::serialize(&req_buf, method_name);
	using ArgsType = std::tuple<std::remove_cvref_t<Args>...>;
	ArgsType args_tuple{std::forward<Args>(args)...};
	SerializeTraits<ArgsType>::serialize(&req_buf, args_tuple);

	RpcHeader h{.magic = kMagic,
				.version = kVersion,
				.flags = 1,
				.header_len = kRpcHeaderWireLength,
				.body_len = static_cast<std::uint32_t>(req_buf.readableBytes() -
													   size_before),
				.request_id = req_id,
				.reserved = 0};
	req_buf.prependRpcHeader(h);

	write_queue_.push(std::move(req_buf));

	if (writer_waiter_)
	{
		writer_waiter_->schedule();
		writer_waiter_ = nullptr;
	}

	CallContext ctx;
	pending_calls_.try_emplace(req_id, &ctx);

	co_await RpcAwaiter{ctx};

	using RetTupleType = std::tuple<Ret>;
	RetTupleType ret_tuple;
	DeserializeTraits<RetTupleType>::deserialize(&ctx.response_buffer,
												 &ret_tuple);

	co_return std::get<0>(ret_tuple);
}

inline async::Task<> RpcClient::readLoop()
{
	try
	{
		while (true)
		{
			while (stream_.read_buffer()->readableBytes() <
				   sizeof(RpcHeaderWire))
			{
				if (!co_await stream_.read())
				{
					throw std::runtime_error(
						"Server closed connection while reading header.");
				}
			}

			RpcHeader h = stream_.read_buffer()->peekRpcHeader();
			std::size_t total_len = h.header_len + h.body_len;

			while (stream_.read_buffer()->readableBytes() < total_len)
			{
				if (!co_await stream_.read())
				{
					throw std::runtime_error(
						"Server closed connection while reading body.");
				}
			}

			stream_.read_buffer()->retrieve(h.header_len); // retrieve header

			net::Buffer payload;
			payload.append(stream_.read_buffer()->peek(), h.body_len);
			stream_.read_buffer()->retrieve(h.body_len); // retrieve body

			if (auto it = pending_calls_.find(h.request_id);
				it != pending_calls_.end())
			{
				CallContext* ctx = it->second;
				ctx->response_buffer = std::move(payload);

				pending_calls_.erase(it);

				if (ctx->coro_to_resume)
				{
					ctx->coro_to_resume->schedule();
				}
			}
			else
			{
				::elog::LOG_ERROR("Received unknown request_id: {}",
								  h.request_id);
			}
		}
	}
	catch (const std::exception& e)
	{
		::elog::LOG_ERROR("{}", e.what());

		for (auto& [id, ctx] : pending_calls_)
		{
			ctx->exception = std::current_exception();
			if (ctx->coro_to_resume)
			{
				ctx->coro_to_resume->schedule();
			}
		}
		pending_calls_.clear();
	}
}

inline async::Task<> RpcClient::writeLoop()
{
	try
	{
		while (true)
		{
			// 如果队列为空，把自己挂起，直到 call() 里把它唤醒
			if (write_queue_.empty())
			{
				co_await WriterWaitAwaiter{this};
			}

			while (!write_queue_.empty())
			{
				net::Buffer buf = std::move(write_queue_.front());
				write_queue_.pop();

				stream_.write_buffer()->append(buf.peek(), buf.readableBytes());

				bool write_success = co_await stream_.write();
				if (!write_success)
				{
					throw std::runtime_error(
						"Server closed connection while writing.");
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		::elog::LOG_ERROR("writeLoop exit: {}", e.what());
		for (auto& [id, ctx] : pending_calls_)
		{
			ctx->exception = std::current_exception();
			if (ctx->coro_to_resume)
			{
				ctx->coro_to_resume->schedule();
			}
		}
		pending_calls_.clear();
	}
}
} // namespace rpc
} // namespace netx

#endif