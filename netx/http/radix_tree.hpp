#pragma once

#include "netx/http/field_map.hpp"
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace netx {
namespace http {
namespace details {

/// 路由树。
///
/// 两种段：`:name` 捕获一段，`*` 吃掉剩下的所有段（只能出现在末尾）。
/// 静态段优先于参数段 —— 否则 `/users/me` 会被 `/users/:id` 抢走。
template <typename T>
struct RadixTree {
    struct MatchResult {
        T *value = nullptr;
        FieldMap params;
    };

    struct string_hash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string &s) const {
            return std::hash<std::string>{}(s);
        }
    };

    struct Node {
        T value{};
        bool is_leaf{false};

        std::unordered_map<std::string,
                           std::unique_ptr<Node>,
                           string_hash,
                           std::equal_to<>>
            children;

        /// 参数段的名字。挂在**被参数边指向的那个节点**上，而不是父节点上：
        /// 挂父节点时 `/users/:id` 与 `/users/:name` 会互相覆盖，两条路由都
        /// 拿到对方的名字。
        std::string param_name;
        std::unique_ptr<Node> param_node;

        std::unique_ptr<Node> wildcard_node;
    };

    /// 逐段遍历，不分配 —— insert/search 各自只走一遍，没必要先把段拆进
    /// 一个容器。
    /// visit 返回 false 表示提前停下。
    template <typename Visit>
    static void for_each_segment(std::string_view path, Visit &&visit) {
        size_t start = 0;
        while (start < path.size()) {
            size_t end = path.find('/', start);
            if (end == std::string_view::npos) {
                end = path.size();
            }
            if (end > start && !visit(path.substr(start, end - start))) {
                return;
            }
            start = end + 1;
        }
    }

    /// 就地归一化：折叠 "."、".." 和重复的 '/'。
    ///
    /// 结果只会更短（不以 '/' 开头时先补一个），所以能在原缓冲上重写 ——
    /// 原实现建一个 deque<string_view> 再拼一个新字符串，每请求两次分配。
    /// 多出来的 ".." 直接丢弃，不允许逃出根目录。
    static void normalize_in_place(std::string &path) {
        // 超过上限、或含内嵌 NUL 的路径一律折算成根。前者解析层已经挡过，
        // 这里是兜底；后者防的是下游把 url_path 直接拼成文件路径 ——
        // C 接口遇 NUL 会截断，归一化就白做了。
        if (path.size() > kMaxPathLen || path.find('\0') != std::string::npos) {
            path.assign("/");
            return;
        }

        if (path.empty()) {
            path = "/";
            return;
        }
        if (path[0] != '/') {
            path.insert(0, 1, '/');
        }

        constexpr size_t npos = std::string::npos;
        const size_t n = path.size();
        size_t r = 0; // 读指针
        size_t w = 0; // 写指针；恒有 w < r，写不会踩到还没读的字节

        while (r < n) {
            while (r < n && path[r] == '/') {
                ++r;
            }
            if (r == n) {
                break;
            }

            size_t end = path.find('/', r);
            if (end == npos) {
                end = n;
            }
            const std::string_view seg{path.data() + r, end - r};

            if (seg == ".") {
                // 丢掉
            } else if (seg == "..") {
                while (w > 0 && path[w - 1] != '/') {
                    --w; // 退到本段段首
                }
                if (w > 0) {
                    --w; // 连那个 '/' 一起删掉
                }
            } else {
                path[w++] = '/';
                if (w != r) {
                    std::memmove(path.data() + w, path.data() + r, seg.size());
                }
                w += seg.size();
            }

            r = end;
        }

        if (w == 0) {
            path.assign("/");
            return;
        }
        path.resize(w);
    }

    /// 路径长度上限，与解析器的 kMaxUrlPathLen 取同一个值
    inline static constexpr size_t kMaxPathLen = 1024;

    [[nodiscard]] static std::string normalize_path(const std::string &path) {
        std::string out = path;
        normalize_in_place(out);
        return out;
    }

    /// 注册一条路由。返回 false 表示同一位置已经有不同名字的参数段
    /// （`/users/:id` 与 `/users/:name` 冲突）—— 一个节点只能有一个名字。
    template <typename V>
    [[nodiscard]] bool insert(const std::string &path, V &&val) {
        Node *cur = root_.get();
        bool ok = true;

        for_each_segment(path, [&](std::string_view seg) -> bool {
            if (seg[0] == ':') {
                const std::string_view name = seg.substr(1);
                if (cur->param_node == nullptr) {
                    cur->param_node = std::make_unique<Node>();
                } else if (cur->param_node->param_name != name) {
                    ok = false;
                    return false;
                }
                cur->param_node->param_name = std::string(name);
                cur = cur->param_node.get();
                return true;
            }

            if (seg == "*") {
                if (cur->wildcard_node == nullptr) {
                    cur->wildcard_node = std::make_unique<Node>();
                }
                cur = cur->wildcard_node.get();
                return false; // '*' 之后不该再有段
            }

            auto [it, inserted] = cur->children.try_emplace(std::string(seg));
            if (inserted) {
                it->second = std::make_unique<Node>();
            }
            cur = it->second.get();
            return true;
        });

        if (!ok) {
            return false;
        }
        cur->value = std::forward<V>(val);
        cur->is_leaf = true;
        return true;
    }

    [[nodiscard]] MatchResult search(const std::string &path) const {
        MatchResult result;
        Node *cur = root_.get();

        if (path == "/" && cur->is_leaf) {
            result.value = &cur->value;
            return result;
        }

        bool matched = true;
        for_each_segment(path, [&](std::string_view seg) -> bool {
            // 1. 静态段优先
            if (auto it = cur->children.find(seg); it != cur->children.end()) {
                cur = it->second.get();
                return true;
            }
            // 2. 参数段 :name
            if (cur->param_node != nullptr) {
                result.params[cur->param_node->param_name] = seg;
                cur = cur->param_node.get();
                return true;
            }
            // 3. 通配符：吃掉剩下的所有段，遍历到此为止
            if (cur->wildcard_node != nullptr) {
                cur = cur->wildcard_node.get();
            } else {
                matched = false;
            }
            return false;
        });

        if (matched && cur->is_leaf) {
            result.value = &cur->value;
        }
        return result;
    }

    RadixTree() : root_(std::make_unique<Node>()) {
    }

    RadixTree(RadixTree &&) = default;
    ~RadixTree() = default;

  private:
    std::unique_ptr<Node> root_;
};
} // namespace details
} // namespace http
} // namespace netx
