#ifndef ELOG_FILE_MANAGER_HPP
#define ELOG_FILE_MANAGER_HPP

#include "elog/file_appender.hpp"
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace elog
{
namespace details
{

struct FileManager
{
	explicit FileManager(
		std::string dir, std::string prefix,
		size_t roll_size = 100 * 1024 * 1024,
		std::chrono::seconds flush_interval = std::chrono::seconds(3),
		size_t check_per_count = 1024)
		: dir_(std::move(dir)), prefix_(std::move(prefix)),
		  roll_size_(roll_size), flush_interval_(flush_interval),
		  check_per_count_(check_per_count)
	{
		roll_file();
	}

	void append(const std::string& data)
	{
		append(data.data(), data.size());
	}

	void append(const char* data, size_t len);

	void flush()
	{
		file_->flush();
	}

  private:
	void roll_file(std::chrono::system_clock::time_point now =
					   std::chrono::system_clock::now());

  private:
	const std::string dir_;
	const std::string prefix_;
	const size_t roll_size_;
	const std::chrono::seconds flush_interval_;
	const size_t check_per_count_;
	int count_{0};

	std::unique_ptr<FileAppender> file_;
	std::string current_path_;
	int same_second_count_{0};

	std::chrono::time_point<std::chrono::system_clock, std::chrono::days>
		last_day_;
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		last_roll_second_;
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		last_flush_second_;
};

inline void FileManager::append(const char* data, size_t len)
{
	file_->append(data, len);
	if (file_->written_bytes() > roll_size_)
	{
		roll_file();
		file_->reset_written_bytes();
	}
	else
	{
		count_++;
		if (count_ >= check_per_count_)
		{
			count_ = 0;
			auto now = std::chrono::system_clock::now();
			auto now_day = std::chrono::floor<std::chrono::days>(now);
			if (now_day != last_day_)
			{
				roll_file(now);
			}
			else if (now - last_flush_second_ >= flush_interval_)
			{
				last_flush_second_ =
					std::chrono::floor<std::chrono::seconds>(now);
				file_->flush();
			}
		}
	}
}

inline void FileManager::roll_file(std::chrono::system_clock::time_point now)
{
	last_roll_second_ = last_flush_second_ =
		std::chrono::floor<std::chrono::seconds>(now);
	last_day_ = std::chrono::floor<std::chrono::days>(now);

	std::chrono::zoned_time zt{std::chrono::current_zone(),
							   last_roll_second_};

	// 同一秒内多次滚动时加序号后缀,避免反复 append 到同一个文件
	std::string path =
		std::format("{}/{}{:%Y-%m-%dT%H:%M:%S}.log", dir_, prefix_, zt);
	if (path == current_path_)
	{
		path = std::format("{}/{}{:%Y-%m-%dT%H:%M:%S}-{}.log", dir_, prefix_,
						   zt, ++same_second_count_);
	}
	else
	{
		same_second_count_ = 0;
		current_path_ = path;
	}

	file_ = std::make_unique<FileAppender>(path);
}

} // namespace details
} // namespace elog

#endif
