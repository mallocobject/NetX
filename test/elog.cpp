// elog 测试：覆盖 LogBlock / SpscQueue / FileAppender / FileManager /
// AsyncLogger 以及 logger.hpp 的宏与级别过滤。
//
// 这些测试有几条共同约定：
//   * 每个用例用独立的临时目录，析构时清理，互不干扰；
//   * 任何会改动全局日志状态的用例都必须走 ScopedLogState，退出时还原，
//     否则后面的用例会继承 set_log_path / set_log_threshold 的副作用；
//   * AsyncLogger 默认 flush_interval 是 3s，测试里一律显式传小值或调用
//     wait_for_done()，避免用例被后台线程的刷新节奏拖慢。

#include "elog/async_logger.hpp"
#include "elog/file_appender.hpp"
#include "elog/file_manager.hpp"
#include "elog/log_block.hpp"
#include "elog/logger.hpp"
#include "elog/spsc_queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

using elog::LogLevel;
using elog::details::FileAppender;
using elog::details::FileManager;
using elog::details::LogBlock;
using elog::details::SpscQueue;

// LOG_xxx 是 logger.hpp 在 namespace elog 内生成的函数模板，不是宏
using elog::LOG_DEBUG;
using elog::LOG_ERROR;
using elog::LOG_FATAL;
using elog::LOG_INFO;
using elog::LOG_TRACE;
using elog::LOG_WARN;

namespace {

namespace fs = std::filesystem;

// 临时目录：构造时创建，析构时尽力清理。
struct TempDir {
    fs::path path;

    TempDir() {
        static std::atomic<unsigned> seq{0};
        path = fs::temp_directory_path() /
               std::format(
                   "elog-test-{}-{}",
                   seq.fetch_add(1, std::memory_order_relaxed),
                   std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec); // 清理失败不影响测试结论
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    std::string str() const {
        return path.string();
    }
};

std::string read_all(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

std::vector<fs::path> log_files(const fs::path &dir) {
    std::vector<fs::path> out;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string read_all_logs(const fs::path &dir) {
    std::string all;
    for (const auto &p : log_files(dir)) {
        all += read_all(p);
    }
    return all;
}

std::size_t count_occurrences(std::string_view hay, std::string_view needle) {
    std::size_t n = 0;
    for (auto pos = hay.find(needle); pos != std::string_view::npos;
         pos = hay.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto nl = text.find('\n', pos);
        if (nl == std::string_view::npos) {
            lines.emplace_back(text.substr(pos));
            break;
        }
        lines.emplace_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

// 保存/还原 logger.hpp 里的三个全局量，保证用例之间不串味。
class ScopedLogState {
  public:
    ScopedLogState() {
        saved_file_ = std::move(elog::details::g_log_file);
        saved_callback_ = std::move(elog::details::g_log_callback);
        saved_threshold_ =
            elog::details::g_log_threshold.load(std::memory_order_relaxed);
    }

    ~ScopedLogState() {
        // 赋值会先销毁本用例新建的 logger（析构即排空落盘），再装回原值
        elog::details::g_log_file = std::move(saved_file_);
        elog::details::g_log_callback = std::move(saved_callback_);
        elog::details::g_log_threshold.store(saved_threshold_,
                                             std::memory_order_relaxed);
    }

    ScopedLogState(const ScopedLogState &) = delete;
    ScopedLogState &operator=(const ScopedLogState &) = delete;

  private:
    std::unique_ptr<elog::details::AsyncLogger> saved_file_;
    std::function<void(LogLevel, std::string_view)> saved_callback_;
    LogLevel saved_threshold_{LogLevel::INFO};
};

// 把 drain() 得到的 LIFO 链取成 vector（调用方负责 delete 节点）。
std::vector<int> drain_ints(SpscQueue<int> &q) {
    std::vector<int> got;
    for (auto *n = elog::details::to_fifo<int>(q.drain()); n;) {
        auto *node = n;
        n = n->next;
        got.push_back(node->value);
        delete node;
    }
    return got;
}

} // namespace

// ---------------------------------------------------------------- LogBlock

TEST_CASE("LogBlock 在容量内追加并累计长度", "[elog][log_block]") {
    auto blk = std::make_unique<LogBlock>();

    CHECK(blk->empty());
    CHECK(blk->len == 0);

    CHECK(blk->append("abc"));
    CHECK(blk->append("de"));
    CHECK(blk->len == 5);
    CHECK_FALSE(blk->empty());
    CHECK(std::string_view(blk->data, blk->len) == "abcde");
}

TEST_CASE("LogBlock 恰好填满时成功，再多一个字节就整条拒绝",
          "[elog][log_block]") {
    auto blk = std::make_unique<LogBlock>();
    const std::string full(LogBlock::kCap, 'x');

    CHECK(blk->append(full));
    CHECK(blk->len == LogBlock::kCap);

    CHECK_FALSE(blk->append("y"));
    CHECK(blk->len == LogBlock::kCap); // 长度不能变
    CHECK(blk->data[0] == 'x');
    CHECK(blk->data[LogBlock::kCap - 1] == 'x');
}

TEST_CASE("LogBlock 追加失败时不做部分写入", "[elog][log_block]") {
    auto blk = std::make_unique<LogBlock>();
    REQUIRE(blk->append(std::string(LogBlock::kCap - 2, 'a')));

    // 只剩 2 字节空间，追加 3 字节必须整体失败
    CHECK_FALSE(blk->append("bbb"));
    CHECK(blk->len == LogBlock::kCap - 2);
    // make_unique 是值初始化，未写入的字节保持为 0：没有越界/部分写入
    CHECK(blk->data[LogBlock::kCap - 2] == '\0');
    CHECK(blk->data[LogBlock::kCap - 1] == '\0');
}

TEST_CASE("LogBlock clear 后可重复使用", "[elog][log_block]") {
    auto blk = std::make_unique<LogBlock>();
    REQUIRE(blk->append("first"));

    blk->clear();
    CHECK(blk->empty());
    CHECK(blk->len == 0);

    REQUIRE(blk->append("second"));
    CHECK(blk->len == 6);
    // 只以 len 为准：旧内容残留在 len 之外不会被写出
    CHECK(std::string_view(blk->data, blk->len) == "second");
}

// --------------------------------------------------------------- SpscQueue

TEST_CASE("SpscQueue drain 得到 LIFO 链，to_fifo 反转为 FIFO",
          "[elog][queue]") {
    SpscQueue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);
    q.flush();

    // 原始 drain 顺序是 push 的逆序
    std::vector<int> lifo;
    for (auto *n = q.drain(); n;) {
        auto *node = n;
        n = n->next;
        lifo.push_back(node->value);
        delete node;
    }
    CHECK(lifo == std::vector<int>{3, 2, 1});

    q.push(1);
    q.push(2);
    q.push(3);
    q.flush();
    CHECK(drain_ints(q) == std::vector<int>{1, 2, 3});
}

TEST_CASE("SpscQueue 没有 pending 时 flush 是空操作", "[elog][queue]") {
    SpscQueue<int> q;
    q.flush();
    CHECK(q.drain() == nullptr);
}

TEST_CASE("SpscQueue 消费者未 drain 时连续 flush 不覆盖前一批",
          "[elog][queue]") {
    // 回归：flush() 若直接 store 覆盖 head_，上一批已发布的节点会被整条丢掉
    SpscQueue<int> q;
    q.push(1);
    q.flush();
    q.push(2);
    q.flush(); // 此时消费者一次都没 drain

    CHECK(drain_ints(q) == std::vector<int>{1, 2});
    CHECK(q.drain() == nullptr);
}

TEST_CASE("SpscQueue 跨线程批量交接不丢不重且保持 FIFO",
          "[elog][queue][thread]") {
    SpscQueue<int> q;
    constexpr int kTotal = 200000;

    std::thread producer([&q] {
        for (int i = 0; i < kTotal; ++i) {
            q.push(i);
            if ((i & 1023) == 1023) {
                q.flush();
            }
        }
        q.flush();
    });

    std::vector<int> got;
    got.reserve(kTotal);
    while (static_cast<int>(got.size()) < kTotal) {
        auto batch = drain_ints(q);
        if (batch.empty()) {
            std::this_thread::yield();
            continue;
        }
        got.insert(got.end(), batch.begin(), batch.end());
    }
    producer.join();

    REQUIRE(got.size() == static_cast<std::size_t>(kTotal));
    // 单生产者按序 push、分批发布，消费者按发布顺序拼接 => 整体严格 FIFO
    for (int i = 0; i < kTotal; ++i) {
        if (got[static_cast<std::size_t>(i)] != i) {
            FAIL("FIFO 顺序被破坏：位置 " << i << " 上是 "
                                          << got[static_cast<std::size_t>(i)]);
        }
    }
}

// ------------------------------------------------------------ FileAppender

TEST_CASE("FileAppender 写入内容可在析构后读回", "[elog][file]") {
    TempDir dir;
    const auto path = dir.path / "appender.log";

    {
        FileAppender app(path.string());
        app.append("hello ");
        app.append(std::string("world"));
        CHECK(app.written_bytes() == 11);
    } // 析构里的 fclose 负责 flush

    CHECK(read_all(path) == "hello world");
}

TEST_CASE("FileAppender 忽略空指针与零长度写入", "[elog][file]") {
    TempDir dir;
    FileAppender app((dir.path / "empty.log").string());

    app.append(nullptr, 10);
    app.append("x", 0);
    CHECK(app.written_bytes() == 0);
}

TEST_CASE("FileAppender reset_written_bytes 归零计数", "[elog][file]") {
    TempDir dir;
    FileAppender app((dir.path / "reset.log").string());

    app.append("abcdef");
    CHECK(app.written_bytes() == 6);
    app.reset_written_bytes();
    CHECK(app.written_bytes() == 0);
}

TEST_CASE("FileAppender 打开失败抛 system_error", "[elog][file]") {
    TempDir dir;
    const auto missing = (dir.path / "no_such_dir" / "x.log").string();
    CHECK_THROWS_AS(FileAppender{missing}, std::system_error);
}

// -------------------------------------------------------------- FileManager

TEST_CASE("FileManager 构造即建文件，flush 后内容可见",
          "[elog][file][manager]") {
    TempDir dir;
    auto fm = std::make_unique<FileManager>(dir.str(), "pfx-");

    const auto files = log_files(dir.path);
    REQUIRE(files.size() == 1);
    CHECK(files[0].filename().string().starts_with("pfx-"));
    CHECK(files[0].filename().string().ends_with(".log"));

    fm->append("line1\n");
    fm->append(std::string("line2\n"));
    fm->flush();

    CHECK(read_all_logs(dir.path) == "line1\nline2\n");
}

TEST_CASE("FileManager 超过 roll_size 后滚动且不丢数据",
          "[elog][file][manager]") {
    TempDir dir;
    // roll_size 故意设得很小，20 次 64 字节写入必然触发多次滚动
    FileManager fm(dir.str(), "roll-", /*roll_size=*/128, 3s, 1024);

    CHECK(log_files(dir.path).size() == 1);

    std::vector<std::string> blocks;
    for (int i = 0; i < 20; ++i) {
        std::string block = std::format("block-{:03d}", i);
        block.resize(64, '.');
        blocks.push_back(block);
        fm.append(block);
    }
    fm.flush();

    const auto files = log_files(dir.path);
    CHECK(files.size() > 1); // 确实滚动了

    const std::string all = read_all_logs(dir.path);
    CHECK(all.size() == blocks.size() * 64); // 总字节数不多不少

    for (const auto &block : blocks) {
        CHECK(count_occurrences(all, block) == 1); // 每块恰好出现一次
    }
}

TEST_CASE("FileManager 同一秒内多次滚动生成不同文件", "[elog][file][manager]") {
    TempDir dir;
    FileManager fm(dir.str(), "same-sec-", /*roll_size=*/1, 3s, 1024);

    for (int i = 0; i < 5; ++i) {
        fm.append(std::string(32, static_cast<char>('a' + i)));
    }
    fm.flush();

    const auto files = log_files(dir.path);
    CHECK(files.size() > 1);

    // 文件路径必须两两不同，否则后一次滚动会把内容 append 进同一个文件
    std::vector<std::string> names;
    for (const auto &p : files) {
        names.push_back(p.filename().string());
    }
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}

// -------------------------------------------------------------- AsyncLogger

TEST_CASE("AsyncLogger 排空后所有日志恰好落盘一次", "[elog][async]") {
    TempDir dir;
    constexpr int kCount = 1000;

    elog::details::AsyncLogger logger(
        dir.str(), "async-", 100 * 1024 * 1024, 1s, 1024);
    for (int i = 0; i < kCount; ++i) {
        logger.append_message(std::format("msg-{}\n", i));
    }
    logger.wait_for_done(); // 主动排空，不必等 flush_interval

    const std::string all = read_all_logs(dir.path);
    CHECK(split_lines(all).size() == static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        CHECK(count_occurrences(all, std::format("msg-{}\n", i)) == 1);
    }
}

TEST_CASE("AsyncLogger 跨块写入（超过单个 LogBlock 容量）不丢日志",
          "[elog][async]") {
    TempDir dir;
    // 每条 200 字节，远超 64KiB 的单块容量，必然触发满块发布与块回收
    constexpr int kCount = 2000;
    const std::string payload(199, 'z');

    elog::details::AsyncLogger logger(
        dir.str(), "block-", 100 * 1024 * 1024, 1s, 1024);
    for (int i = 0; i < kCount; ++i) {
        logger.append_message(std::format("{}{:05d}\n", payload, i));
    }
    logger.wait_for_done();

    const std::string all = read_all_logs(dir.path);
    CHECK(split_lines(all).size() == static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        CHECK(count_occurrences(all, std::format("{}{:05d}\n", payload, i)) ==
              1);
    }
}

TEST_CASE("AsyncLogger 多线程并发写入不丢不重", "[elog][async][thread]") {
    TempDir dir;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 2000;

    elog::details::AsyncLogger logger(
        dir.str(), "mt-", 100 * 1024 * 1024, 1s, 1024);

    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&logger, t] {
            for (int i = 0; i < kPerThread; ++i) {
                logger.append_message(std::format("T{:02d}-{:06d}\n", t, i));
            }
        });
    }
    for (auto &th : writers) {
        th.join();
    }
    logger.wait_for_done();

    const std::string all = read_all_logs(dir.path);
    auto lines = split_lines(all);

    std::vector<std::string> expected;
    expected.reserve(static_cast<std::size_t>(kThreads) * kPerThread);
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kPerThread; ++i) {
            expected.push_back(std::format("T{:02d}-{:06d}", t, i));
        }
    }

    // 行数 + 排序后逐行相等 => 每条恰好出现一次，没有丢失也没有重复
    REQUIRE(lines.size() == expected.size());
    std::sort(lines.begin(), lines.end());
    std::sort(expected.begin(), expected.end());
    CHECK(lines == expected);
}

TEST_CASE("AsyncLogger 每个生产者的日志保持有序", "[elog][async][thread]") {
    TempDir dir;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 3000;

    elog::details::AsyncLogger logger(
        dir.str(), "order-", 100 * 1024 * 1024, 1s, 1024);

    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&logger, t] {
            for (int i = 0; i < kPerThread; ++i) {
                logger.append_message(std::format("W{}-{:06d}\n", t, i));
            }
        });
    }
    for (auto &th : writers) {
        th.join();
    }
    logger.wait_for_done();

    auto lines = split_lines(read_all_logs(dir.path));
    REQUIRE(lines.size() == static_cast<std::size_t>(kThreads) * kPerThread);

    // 单条日志线程内部的先后顺序就是写入顺序，落盘后必须仍然递增
    for (int t = 0; t < kThreads; ++t) {
        const std::string prefix = std::format("W{}-", t);
        int expect = 0;
        for (const auto &line : lines) {
            if (!line.starts_with(prefix)) {
                continue;
            }
            if (line != std::format("W{}-{:06d}", t, expect)) {
                FAIL("线程 " << t << " 的日志乱序：期望 " << expect << "，实际 "
                             << line);
            }
            ++expect;
        }
        CHECK(expect == kPerThread);
    }
}

TEST_CASE("AsyncLogger 关闭不被 flush_interval 拖延", "[elog][async]") {
    TempDir dir;
    // AsyncLogger 的 flush_interval 是整秒，1s 已是最小值
    constexpr int kRounds = 6;
    constexpr auto kInterval = 1s;
    constexpr auto kSlow = 500ms;

    int slow_rounds = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < kRounds; ++i) {
        const auto round_begin = std::chrono::steady_clock::now();
        {
            // flush_interval 故意设大：只要关闭通知没丢，析构就不该等它
            elog::details::AsyncLogger logger(dir.str(),
                                              std::format("shutdown{}-", i),
                                              100 * 1024 * 1024,
                                              kInterval,
                                              1024);
            logger.append_message("bye\n");
        } // 析构 = 请求关闭 + join
        if (std::chrono::steady_clock::now() - round_begin > kSlow) {
            ++slow_rounds;
        }
    }
    const auto total = std::chrono::steady_clock::now() - begin;

    INFO("6 轮构造+析构共 "
         << std::chrono::duration_cast<std::chrono::milliseconds>(total).count()
         << " ms，其中被 flush_interval 拖延的有 " << slow_rounds << " 轮");
    CHECK(slow_rounds == 0);
}

TEST_CASE("日志目录不存在时降级为不写文件且不抛异常", "[elog][async]") {
    TempDir dir;
    ScopedLogState guard;

    const auto missing = (dir.path / "missing" / "sub").string();
    elog::details::g_log_callback = [](LogLevel, std::string_view) {}; // 静音

    CHECK_NOTHROW(elog::set_log_path(missing, "degraded-", 1024, 1s, 1));
    CHECK_NOTHROW(LOG_INFO("这条日志只应触发降级，不应崩溃"));
    // 消费者已退出并停止接收新日志：析构/等待都不能卡住
    CHECK_NOTHROW(elog::details::g_log_file->wait_for_done());
    CHECK_FALSE(fs::exists(dir.path / "missing"));
}

TEST_CASE("单条超过 LogBlock 容量的消息不会拖垮后续日志", "[elog][async]") {
    // 未修复的缺陷，这里**刻意不断言它的行为**：
    // append_message 换块后第二次 append 的返回值被忽略，导致超过 64KiB 的
    // 单条消息被静默丢弃（不截断、不报错、也不分片）。
    //
    // 断言"大消息必须消失"会把缺陷固化成期望行为 —— 将来真修好了，测试反而
    // 变红，等于用测试抵制修复。所以下面只锁定"丢弃它但不牵连别的日志"这条
    // 健壮性属性；缺陷本身留在注释里，等修好后这里再补上"必须完整落盘"。
    TempDir dir;
    const std::string huge(LogBlock::kCap + 1024, 'X');

    elog::details::AsyncLogger logger(
        dir.str(), "huge-", 100 * 1024 * 1024, 1s, 1024);
    logger.append_message("BEFORE\n");
    logger.append_message(huge);
    logger.append_message("AFTER\n");
    logger.wait_for_done();

    const std::string all = read_all_logs(dir.path);
    INFO("落盘内容长度: " << all.size());
    CHECK(all.find("BEFORE") != std::string::npos);
    CHECK(all.find("AFTER") != std::string::npos);
}

// ------------------------------------------------------------ logger 宏/级别

TEST_CASE("日志级别字符串与枚举可以往返转换", "[elog][logger]") {
    using elog::details::log_level_from_string;
    using elog::details::log_level_to_string;

    CHECK(log_level_to_string(LogLevel::TRACE) == "TRACE");
    CHECK(log_level_to_string(LogLevel::DEBUG) == "DEBUG");
    CHECK(log_level_to_string(LogLevel::INFO) == "INFO");
    CHECK(log_level_to_string(LogLevel::WARN) == "WARN");
    CHECK(log_level_to_string(LogLevel::ERROR) == "ERROR");
    CHECK(log_level_to_string(LogLevel::FATAL) == "FATAL");

    for (auto lv : {LogLevel::TRACE,
                    LogLevel::DEBUG,
                    LogLevel::INFO,
                    LogLevel::WARN,
                    LogLevel::ERROR,
                    LogLevel::FATAL}) {
        CHECK(log_level_from_string(log_level_to_string(lv)) == lv);
    }

    // 未知/非法取值回落到 INFO
    CHECK(log_level_from_string("nonsense") == LogLevel::INFO);
    CHECK(log_level_from_string("") == LogLevel::INFO);
}

TEST_CASE("阈值以下不输出、以上全部输出", "[elog][logger]") {
    ScopedLogState guard;
    elog::details::g_log_file = nullptr;

    std::vector<std::pair<LogLevel, std::string>> got;
    elog::details::g_log_callback = [&got](LogLevel lv, std::string_view msg) {
        got.emplace_back(lv, std::string(msg));
    };

    elog::set_log_threshold(LogLevel::WARN);

    LOG_TRACE("trace");
    LOG_DEBUG("debug");
    LOG_INFO("info");
    LOG_WARN("warn");
    LOG_ERROR("error");
    LOG_FATAL("fatal");

    REQUIRE(got.size() == 3);
    CHECK(got[0].first == LogLevel::WARN);
    CHECK(got[1].first == LogLevel::ERROR);
    CHECK(got[2].first == LogLevel::FATAL);
    // 仅回调路径下拿到的是原始消息，不含时间戳等前缀
    CHECK(got[0].second == "warn");
    CHECK(got[1].second == "error");
    CHECK(got[2].second == "fatal");
}

TEST_CASE("阈值为 FATAL 时其余级别一条都不输出", "[elog][logger]") {
    ScopedLogState guard;
    elog::details::g_log_file = nullptr;

    int calls = 0;
    elog::details::g_log_callback = [&calls](LogLevel, std::string_view) {
        ++calls;
    };

    elog::set_log_threshold(LogLevel::FATAL);

    LOG_TRACE("t");
    LOG_DEBUG("d");
    LOG_INFO("i");
    LOG_WARN("w");
    LOG_ERROR("e");
    CHECK(calls == 0);

    LOG_FATAL("f");
    CHECK(calls == 1);
}

TEST_CASE("日志参数按 std::format 规则展开", "[elog][logger]") {
    ScopedLogState guard;
    elog::details::g_log_file = nullptr;

    std::string seen;
    elog::details::g_log_callback = [&seen](LogLevel, std::string_view msg) {
        seen = std::string(msg);
    };

    elog::set_log_threshold(LogLevel::TRACE);
    LOG_INFO("value={} name={} hex={:#x}", 42, "netx", 255);
    CHECK(seen == "value=42 name=netx hex=0xff");
}

TEST_CASE("文件日志带级别、源位置与消息", "[elog][logger][file]") {
    TempDir dir;
    ScopedLogState guard;

    // 只关心落盘内容；给个空回调把终端那路 println 静音
    elog::details::g_log_callback = [](LogLevel, std::string_view) {};
    elog::set_log_path(dir.str(), "unit-", 100 * 1024 * 1024, 1s, 1);
    elog::set_log_threshold(LogLevel::TRACE);

    const int line = __LINE__ + 1;
    LOG_INFO("value={} name={}", 42, "netx");
    elog::details::g_log_file->wait_for_done();

    const std::string all = read_all_logs(dir.path);
    INFO("落盘内容: " << all);

    CHECK(all.find("<INFO>") != std::string::npos);
    CHECK(all.find("value=42 name=netx") != std::string::npos);
    // 形如 "elog.cpp:123 " —— 文件名与行号来自 std::source_location
    CHECK(all.find(std::format("elog.cpp:{} ", line)) != std::string::npos);
    CHECK(all.find("()-> ") != std::string::npos);
}

TEST_CASE("低于阈值的日志既不落盘也不回调", "[elog][logger][file]") {
    TempDir dir;
    ScopedLogState guard;

    int calls = 0;
    elog::details::g_log_callback = [&calls](LogLevel, std::string_view) {
        ++calls;
    };
    elog::set_log_path(dir.str(), "filtered-", 100 * 1024 * 1024, 1s, 1);
    elog::set_log_threshold(LogLevel::ERROR);

    LOG_TRACE("TRACE-MARKER");
    LOG_DEBUG("DEBUG-MARKER");
    LOG_INFO("INFO-MARKER");
    LOG_WARN("WARN-MARKER");
    CHECK(calls == 0);

    LOG_ERROR("kept");
    elog::details::g_log_file->wait_for_done();

    CHECK(calls == 1);
    const std::string all = read_all_logs(dir.path);
    CHECK(all.find("kept") != std::string::npos);
    for (const char *marker :
         {"TRACE-MARKER", "DEBUG-MARKER", "INFO-MARKER", "WARN-MARKER"}) {
        CHECK(all.find(marker) == std::string::npos);
    }
    CHECK(all.find("<WARN>") == std::string::npos);
    CHECK(all.find("<INFO>") == std::string::npos);
}
