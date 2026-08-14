#ifndef ELOG_ASYNC_LOGGER_HPP
#define ELOG_ASYNC_LOGGER_HPP

#include "elog/file_manager.hpp"
#include "elog/log_block.hpp"
#include "elog/spsc_queue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <latch>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace elog
{
namespace details
{

struct ProducerCtx
{
	std::mutex cur_mtx;
	LogBlock* cur{new LogBlock()};

	SpscQueue<LogBlock*> full; // 满块:生产者 push,消费者 drain
	SpscQueue<LogBlock*> free; // 空块:消费者 push,生产者 drain

	// 生产者侧的空块缓存(drain free 通道后按需取用)
	SpscQueue<LogBlock*>::Node* free_stash{nullptr};

	LogBlock* take_free()
	{
		using Node = SpscQueue<LogBlock*>::Node;
		Node* stash = free_stash;
		if (!stash)
		{
			stash = free.drain();
		}
		if (!stash)
		{
			return nullptr;
		}
		LogBlock* blk = stash->value;
		free_stash = stash->next;
		delete stash;
		blk->clear(); // 复用前必须清空,否则旧内容会被重复写出
		return blk;
	}
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
		// 注意:不释放 producers_ 及其中的块。进程退出瞬间仍可能有线程
		// 在写日志,释放它们会引入 use-after-free;此处以少量泄漏换安全。
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

	bool wake = false;
	{
		std::lock_guard<std::mutex> lock(ctx->cur_mtx);

		if (ctx->cur->append(msg))
		{
			return; // 快路径:一次 memcpy
		}

		// 当前块写满:发布满块,换一块继续
		ctx->full.push(ctx->cur);

		LogBlock* fresh = ctx->take_free();
		if (!fresh)
		{
			fresh = new LogBlock();
		}
		ctx->cur = fresh;
		ctx->cur->append(msg);
		wake = true;
	}

	if (wake)
	{
		ctx->full.flush();
		cv_.notify_one();
	}
}

inline void AsyncLogger::run(const std::string& dir, const std::string& prefix,
							 size_t roll_size,
							 std::chrono::seconds flush_interval,
							 size_t check_per_count, std::latch& start_latch)
{
	// 先放行构造者:文件初始化失败只降级文件日志,不拖死/不崩进程
	start_latch.count_down();

	std::unique_ptr<FileManager> out_file;
	try
	{
		out_file = std::make_unique<FileManager>(dir, prefix, roll_size,
												 flush_interval,
												 check_per_count);
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "[elog] file logging disabled: %s\n", e.what());
		return;
	}

	bool file_ok = true;

	while (true)
	{
		{
			std::unique_lock<std::mutex> lock(cv_mtx_);
			cv_.wait_for(lock, flush_interval);
		}

		if (done_.load(std::memory_order_acquire))
		{
			break;
		}

		try
		{
			std::vector<ProducerCtx*> snapshot;
			{
				std::lock_guard lock(reg_mtx_);
				snapshot = producers_;
			}

			bool any = false;
			for (auto* ctx : snapshot)
			{
				// 1) 满块批量落盘(FIFO)
				for (auto* n = to_fifo<LogBlock*>(ctx->full.drain()); n;)
				{
					auto* node = n;
					n = n->next;
					out_file->append(node->value->data, node->value->len);
					ctx->free.push(node->value);
					delete node;
					any = true;
				}
				ctx->free.flush();

				// 2) 当前残块:锁内只交换,锁外才写盘
				LogBlock* taken = nullptr;
				{
					std::lock_guard<std::mutex> lock(ctx->cur_mtx);
					if (!ctx->cur->empty())
					{
						taken = ctx->cur;
						ctx->cur = new LogBlock();
					}
				}
				if (taken)
				{
					out_file->append(taken->data, taken->len);
					ctx->free.push(taken);
					ctx->free.flush();
					any = true;
				}
			}

			if (any)
			{
				out_file->flush();
			}
		}
		catch (const std::exception& e)
		{
			// 文件故障:停止文件日志,终端日志不受影响
			std::fprintf(stderr, "[elog] file logging disabled: %s\n",
						 e.what());
			file_ok = false;
			break;
		}
	}

	// 最后一次:尽力排空(不释放任何生产者对象,避免与在途日志线程竞争)
	if (file_ok)
	{
		try
		{
			std::vector<ProducerCtx*> snapshot;
			{
				std::lock_guard lock(reg_mtx_);
				snapshot = producers_;
			}
			for (auto* ctx : snapshot)
			{
				for (auto* n = to_fifo<LogBlock*>(ctx->full.drain()); n;)
				{
					auto* node = n;
					n = n->next;
					out_file->append(node->value->data, node->value->len);
					delete node;
				}

				std::lock_guard<std::mutex> lock(ctx->cur_mtx);
				if (!ctx->cur->empty())
				{
					out_file->append(ctx->cur->data, ctx->cur->len);
				}
			}
			out_file->flush();
		}
		catch (...)
		{
		}
	}
}

} // namespace details
} // namespace elog

#endif
