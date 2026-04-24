#ifndef ELOG_ASYNC_LOGGER_HPP
#define ELOG_ASYNC_LOGGER_HPP

#include "elog/file_manager.hpp"
#include "elog/log_block.hpp"
#include "lock_free_queue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
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
	std::mutex cur_mtx;
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

		cv_.notify_one();

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
	std::mutex cv_mtx_;
	std::condition_variable cv_;

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

	std::lock_guard<std::mutex> lock(ctx->cur_mtx);

	if (!ctx->cur->append(msg))
	{
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

		ctx->cur->append(msg);

		cv_.notify_one();
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
		std::unique_lock<std::mutex> lock(cv_mtx_);
		cv_.wait_for(lock, std::chrono::seconds(flush_interval));
		lock.unlock();

		if (done_.load(std::memory_order_acquire))
		{
			break;
		}

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

			std::lock_guard<std::mutex> lock(ctx->cur_mtx);
			if (!ctx->cur->empty())
			{
				LogBlock* cur_blk = ctx->cur;

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

				out_file.append(cur_blk->data, cur_blk->len);
				ctx->free_queue.push(cur_blk);
				any = true;
			}
		}

		if (any)
		{
			out_file.flush();
		}
	}

	// 最后一次：drain 所有数据
	std::vector<ProducerCtx*> snapshot;
	{
		std::lock_guard lock(reg_mtx_);
		snapshot = producers_;
	}

	for (auto* ctx : snapshot)
	{
		LogBlock* blk = nullptr;
		while (ctx->full_queue.pop(blk))
		{
			out_file.append(blk->data, blk->len);
			delete blk;
		}

		std::lock_guard<std::mutex> lock(ctx->cur_mtx);
		if (!ctx->cur->empty())
		{
			out_file.append(ctx->cur->data, ctx->cur->len);
		}
		delete ctx->cur;
	}

	out_file.flush();
}

} // namespace details
} // namespace elog

#endif
