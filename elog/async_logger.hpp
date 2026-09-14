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

struct AsyncLogger;

struct ProducerCtx
{
	// 反向指回注册它的 logger。thread_local 缓存是"每线程一份",与实例
	// 无关,必须校验归属,否则 set_log_path 换实例后本线程仍会把日志投进
	// 旧实例的通道(新实例的消费者看不到,那些块再也不会被回收)。
	const AsyncLogger* owner{nullptr};

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
	// dir/prefix 必须按值接收、按值捕获:调用方传入的实参往往是临时
	// std::string(或 getenv 得到的 const char*),在构造函数返回后立即销毁,
	// 而后台线程仍要读取它们。按引用捕获会造成 stack-use-after-scope。
	explicit AsyncLogger(
		std::string dir, std::string prefix,
		size_t roll_size = 100 * 1024 * 1024,
		std::chrono::seconds flush_interval = std::chrono::seconds(3),
		size_t check_per_count = 1024)
	{
		std::latch start_latch{1};
		thread_ = std::jthread(
			[this, dir = std::move(dir), prefix = std::move(prefix), roll_size,
			 flush_interval, check_per_count, &start_latch] {
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

	// 由消费者线程在退出前调用:此后不再接收新日志。
	// 必须与 done_(关闭请求)分开:消费者可能因文件故障自行退出,那时
	// done_ 仍为 false,若不停止接收,生产者投递的块无人 drain,
	// 内存会随日志量无界增长。
	void stop_accepting() noexcept
	{
		accepting_.store(false, std::memory_order_release);
	}

  private:
	std::atomic<bool> done_{false};
	std::atomic<bool> accepting_{true};
	std::mutex cv_mtx_;
	std::condition_variable cv_;

	std::mutex reg_mtx_;
	std::vector<ProducerCtx*> producers_;

	std::jthread thread_;
};

inline ProducerCtx* AsyncLogger::register_producer()
{
	auto* ctx = new ProducerCtx();
	ctx->owner = this;
	std::lock_guard lock(reg_mtx_);
	producers_.push_back(ctx);
	return ctx;
}

inline void AsyncLogger::append_message(std::string_view msg)
{
	if (!accepting_.load(std::memory_order_acquire))
	{
		return;
	}

	thread_local ProducerCtx* ctx = nullptr;
	if (ctx == nullptr || ctx->owner != this)
	{
		ctx = register_producer();
	}

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
	// 先放行构造者:文件初始化失败只降级文件日志,不拖死/不崩进程。
	// 因为 dir/prefix 已按值捕获,此刻放行不再有悬垂引用风险。
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
	}

	if (!out_file)
	{
		// 消费者不会存在了:必须立刻停止接收。否则生产者持续投递,
		// 而没有人 drain,内存会随日志量无界增长——这里曾是主要泄漏点。
		stop_accepting();
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

	// 先停止接收新日志再排空,避免边排空边有新块进来;仍然不释放任何
	// 生产者对象,以免与在途的日志线程竞争。
	stop_accepting();

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
