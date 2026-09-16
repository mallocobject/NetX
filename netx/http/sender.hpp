#pragma once

#include "elog/logger.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/response.hpp"
#include "netx/net/stream.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace netx {
namespace http {
namespace details {

/// 把 Response 写到流上。
///
/// 普通响应走"head + body 一次 writev"；文件响应走 mmap，映射结果按路径缓存，
/// 同一文件只映射一次。
class Sender {
  public:
    static core::Task<core::Expected<>> send(net::details::Stream &stream,
                                             Response &res) {
        if (res.type == ResponseType::kFile) {
            co_return co_await send_file(stream, res);
        }

        // head 与 body 一次 writev 发出去。分成两次 write 是两次系统调用，
        // 而 Stream::write 在缓冲为空时是直写 fd 的，不会自动合并。
        const std::string head = res.to_head_string();
        const std::array<std::string_view, 2> parts{head, res.body};
        co_return co_await stream.write_many(parts);
    }

  private:
    using MappedFile = std::pair<std::shared_ptr<void>, size_t>;

    /// 单次写出的分片大小
    inline static constexpr size_t kSendChunk = 128 * 1024;

    static core::Task<core::Expected<>> send_not_found(
        net::details::Stream &stream, Response &res, const std::string &path) {
        elog::LOG_DEBUG("send_file: not found {}", path);

        res.with_status(404).with_body("<h1>404 Not Found</h1>");
        const std::string head = res.to_head_string();
        const std::array<std::string_view, 2> parts{head, res.body};
        co_return co_await stream.write_many(parts);
    }

    /// 已映射文件的缓存。
    ///
    /// 单例而不是两个函数各写一个 static 局部变量 —— 那样查缓存和写缓存会
    /// 落在两张不同的表上，等于永远命不中。
    ///
    /// 注意这里没有淘汰策略：进程生命周期内每个访问过的文件都会一直被映射
    /// 住。做成有上限的 LRU 之前，请先确认调用方在意长期驻留的内存。
    struct FileCache {
        std::unordered_map<std::string, MappedFile> map;
        std::shared_mutex mutex;

        static FileCache &instance() {
            static FileCache cache;
            return cache;
        }
    };

    /// 取出缓存里映射好的文件。返回的是拷贝 —— 不能返回指向表内部的指针：
    /// 锁一放，别的线程插入就可能触发 rehash 让它悬垂。
    static std::optional<MappedFile> cached(const std::string &path) {
        auto &cache = FileCache::instance();
        std::shared_lock lock(cache.mutex);

        auto it = cache.map.find(path);
        if (it == cache.map.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /// 把映射结果放进缓存。已经有别人放进去时返回已有的那份，并释放本次映射。
    static MappedFile
    cache_or_take(const std::string &path, void *mapped, size_t size) {
        auto &cache = FileCache::instance();
        std::unique_lock lock(cache.mutex);

        auto [it, inserted] = cache.map.try_emplace(
            path,
            std::shared_ptr<void>{mapped,
                                  [size](void *p) { ::munmap(p, size); }},
            size);
        if (!inserted) {
            ::munmap(mapped, size);
        }
        return it->second;
    }

    static core::Task<core::Expected<>> send_file(net::details::Stream &stream,
                                                  Response &res) {
        const std::string &path = res.file;

        // 已经映射过就直接用，不必重开、重映射
        if (auto hit = cached(path); hit.has_value()) {
            co_return co_await mmap_write(stream, res, hit->first, hit->second);
        }

        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd == -1) {
            co_return co_await send_not_found(stream, res, path);
        }

        struct stat st {};
        if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
            ::close(fd);
            co_return co_await send_not_found(stream, res, path);
        }

        const size_t size = static_cast<size_t>(st.st_size);
        if (size == 0) {
            // mmap 长度为 0 会直接 EINVAL，空文件回一个空 body
            ::close(fd);
            res.with_status(200).with_header("Content-Length", "0");
            co_return co_await stream.write(res.to_head_string());
        }

        void *mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (mapped == MAP_FAILED) {
            elog::LOG_ERROR("send_file: mmap failed {}", path);
            co_return core::details::make_error_to_unexpected(
                core::details::Error::ResourceExhausted);
        }

        // 缓存里可能已经有别的线程放进去了；用哪一份由 cache_or_take 决定，
        // 不这样收敛的话就得递归回自己再查一次
        const MappedFile entry = cache_or_take(path, mapped, size);
        co_return co_await mmap_write(stream, res, entry.first, entry.second);
    }

    /// 直接把映射好的文件写到流上：head 之后是分片发送，全过程中不做整块拷贝
    static core::Task<core::Expected<>>
    mmap_write(net::details::Stream &stream,
               Response &res,
               const std::shared_ptr<void> &holder,
               size_t total_size) {
        const auto *ptr = static_cast<const char *>(holder.get());
        size_t remaining = total_size;

        res.with_status(200).with_header("Content-Length",
                                         std::to_string(remaining));

        const std::string head = res.to_head_string();
        const std::string_view first{ptr, std::min(remaining, kSendChunk)};
        const std::array<std::string_view, 2> parts{head, first};
        co_await co_await stream.write_many(parts);

        remaining -= first.size();
        ptr += first.size();

        while (remaining > 0) {
            const size_t to_send = std::min(remaining, kSendChunk);
            co_await co_await stream.write(std::string_view{ptr, to_send});

            ptr += to_send;
            remaining -= to_send;
        }

        co_return {};
    }
};
} // namespace details
} // namespace http
} // namespace netx
