#ifndef ELOG_FILE_APPENDER_HPP
#define ELOG_FILE_APPENDER_HPP

#include <cstddef>
#include <filesystem>
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
	void ensure_directory_exists() const;

  private:
	std::filesystem::path path_;
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

inline void FileAppender::ensure_directory_exists() const
{
	const auto parent_path = path_.parent_path();

	if (parent_path.empty())
	{
		// relative path
		return;
	}

	std::error_code ec;

	auto status = std::filesystem::status(parent_path, ec);

	if (ec)
	{
		throw_system_error("Failed to check directory: " +
						   std::string(parent_path));
	}

	if (!std::filesystem::exists(status))
	{
		throw_runtime_error("Directory does not exist: " +
							parent_path.string());
	}

	if (!std::filesystem::is_directory(status))
	{
		throw_runtime_error("Path exists but is not a directory: " +
							parent_path.string());
	}
}
} // namespace details
} // namespace elog

#endif