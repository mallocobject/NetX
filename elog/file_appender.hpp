#ifndef ELOG_FILE_APPENDER_HPP
#define ELOG_FILE_APPENDER_HPP

#include <cstddef>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>

namespace elog
{
namespace details
{

inline void throw_system_error(std::string operation)
{
	throw std::system_error(errno, std::system_category(), operation.c_str());
}

inline void throw_runtime_error(std::string message)
{
	throw std::runtime_error(message);
}

struct FileAppender
{
	explicit FileAppender(std::string path) : path_(std::move(path))
	{
		file_ = ::fopen(path_.c_str(), "ab");
		if (!file_)
		{
			throw_system_error("Failed to open log file");
		}
	}

	~FileAppender()
	{
		if (file_)
		{
			// fclose 会顺带 flush,避免滚动/退出时丢失缓冲日志
			::fclose(file_);
		}
	}

	FileAppender(const FileAppender&) = delete;
	FileAppender& operator=(const FileAppender&) = delete;

	size_t written_bytes() const noexcept
	{
		return written_bytes_;
	}

	void reset_written_bytes() noexcept
	{
		written_bytes_ = 0;
	}

	void append(const std::string& data)
	{
		append(data.data(), data.size());
	}

	void append(const char* data, size_t len);

	void flush();

  private:
	std::string path_;
	FILE* file_{nullptr};
	size_t written_bytes_{0};
};

inline void FileAppender::append(const char* data, size_t len)
{
	if (!data || !file_ || len == 0)
	{
		return;
	}

	const size_t written = fwrite_unlocked(data, 1, len, file_);

	if (written != len)
	{
		if (ferror(file_))
		{
			throw_system_error("Failed to write log file");
		}
		throw_runtime_error("Partial write to log file");
	}

	written_bytes_ += len;
}

inline void FileAppender::flush()
{
	if (!file_)
	{
		return;
	}

	if (fflush(file_) != 0)
	{
		throw_system_error("Failed to flush log file");
	}
}

} // namespace details
} // namespace elog

#endif
