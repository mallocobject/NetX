#ifndef ELOG_ASYNC_LOGGER_HPP
#define ELOG_ASYNC_LOGGER_HPP

#include "elog/buffer.hpp"
#include "elog/file_manager.hpp"
#include "lock_free_queue.hpp"
#include <atomic>
#include <chrono>
#include <latch>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace elog
{
namespace details
{

using BlockQueue = LockFreeQueue<LogBlock*>;

struct ProducerCtx
{
	LogBlock* cur{new LogBlock()};
	BlockQueue full_queue; // producer push, consumer pop
	BlockQueue free_queue; // consumer push, producer pop
};

struct AsyncLogger
{
	explicit AsyncLogger(
		const std::string& dir, const std::string& prefix,
		size_t roll_size = 100 * 1024 * 1024,
		std::chrono::seconds flush_interval = std::chrono::seconds(3),
		size_t check_per_count = 1024)
	{
		std::latch start_latch{1};
		thread_ = std::jthread(
			[&] {
				run(dir, prefix, roll_size, flush_interval, check_per_count,
					start_latch);
			});
		start_latch.wait();
	}

	AsyncLogger(AsyncLogger&&) = delete;

	~AsyncLogger()
	{
		if (!done_.load(std::memory_order_acquire))
		{
			do_done();
		}
	}

	void append_message(std::string_view msg);

	void wait_for_done()
	{
		if (!done_.load(std::memory_order_acquire))
		{
			do_done();
		}
	}

  private:
	ProducerCtx* register_producer();

	void run(const std::string& dir, const std::string& prefix,
			 size_t roll_size, std::chrono::seconds flush_interval,
			 size_t check_per_count, std::latch& start_latch);

	void do_done()
	{
		done_.store(true, std::memory_order_release);
		pending_.fetch_add(1, std::memory_order_release);
		pending_.notify_one();
		if (thread_.joinable())
		{
			thread_.join();
		}

		// consumer 已退出，安全清理所有 ctx
		for (auto* ctx : producers_)
		{
			delete ctx;
		}
		producers_.clear();
	}

  private:
	std::atomic<bool> done_{false};
	std::atomic<uint32_t> pending_{0};

	std::mutex reg_mtx_;
	std::vector<ProducerCtx*> producers_;

	std::jthread thread_;
};

inline ProducerCtx* AsyncLogger::register_producer()
{
	auto* ctx = new ProducerCtx();
	std::lock_guard lock(reg_mtx_);
	producers_.push_back(ctx);
	return ctx;
}

inline void AsyncLogger::append_message(std::string_view msg)
{
	if (done_.load(std::memory_order_acquire))
	{
		return;
	}

	thread_local ProducerCtx* ctx = register_producer();
	thread_local uint32_t count = 0;

	bool need_write = !ctx->cur->append(msg);

	if (need_write || ++count >= 256)
	{
		count = 0;
		ctx->full_queue.push(ctx->cur);

		LogBlock* free_blk = nullptr;
		if (ctx->free_queue.pop(free_blk))
		{
			free_blk->clear();
			ctx->cur = free_blk;
		}
		else
		{
			ctx->cur = new LogBlock();
		}

		if (need_write)
		{
			ctx->cur->append(msg);
		}

		if (pending_.fetch_add(1, std::memory_order_release) == 0)
		{
			pending_.notify_one();
		}
	}
}

inline void AsyncLogger::run(const std::string& dir, const std::string& prefix,
							 size_t roll_size,
							 std::chrono::seconds flush_interval,
							 size_t check_per_count, std::latch& start_latch)
{
	FileManager out_file(dir, prefix, roll_size, flush_interval,
						 check_per_count);
	start_latch.count_down();

	while (true)
	{
		uint32_t n = pending_.load(std::memory_order_acquire);
		if (n == 0 && !done_.load(std::memory_order_acquire))
			pending_.wait(0);

		pending_.store(0, std::memory_order_release);

		std::vector<ProducerCtx*> snapshot;
		{
			std::lock_guard lock(reg_mtx_);
			snapshot = producers_;
		}

		bool any = false;
		for (auto* ctx : snapshot)
		{
			LogBlock* blk = nullptr;
			while (ctx->full_queue.pop(blk))
			{
				out_file.append(blk->data, blk->len);
				ctx->free_queue.push(blk);
				any = true;
			}
		}

		if (any)
		{
			out_file.flush();
		}

		if (done_.load(std::memory_order_acquire))
		{
			// 最后一次：drain full_queue + cur 残留
			// 此时所有生产者线程已 join，cur 不会再被写入，安全访问
			for (auto* ctx : snapshot)
			{
				LogBlock* blk = nullptr;
				while (ctx->full_queue.pop(blk))
				{
					out_file.append(blk->data, blk->len);
				}
				if (!ctx->cur->empty())
				{
					out_file.append(ctx->cur->data, ctx->cur->len);
				}
			}
			break;
		}
	}

	out_file.flush();
}

} // namespace details
} // namespace elog

#endif
