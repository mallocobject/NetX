#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace netx {
namespace http {

/// 有序的字段表（header / query 之类）。
///
/// 用 vector 而不是 unordered_map，理由有三：
///   - HTTP 的字段顺序不保证，但输出时保持插入顺序才可复现、可测试，
///     unordered_map 的遍历顺序是实现定义的；
///   - 一张报文的字段只有个位数到几十个，线性扫描比哈希更快，也不分配
///     节点；
///   - 保留重复键的可能（同名 header 是合法的）。
///
/// 键按调用方给的原文存；header 名的大小写归一由解析器负责（它统一转小写），
/// 这里不再重复做。
class FieldMap {
  public:
    using Item = std::pair<std::string, std::string>;
    using iterator = std::vector<Item>::iterator;
    using const_iterator = std::vector<Item>::const_iterator;

    /// 存在就返回它，不存在就追加一个空值项
    std::string &operator[](std::string_view key) {
        if (auto it = find(key); it != items_.end()) {
            return it->second;
        }
        items_.emplace_back(std::string{key}, std::string{});
        return items_.back().second;
    }

    /// 不存在时抛 std::out_of_range（与标准容器一致）
    [[nodiscard]] std::string_view at(std::string_view key) const {
        auto it = find(key);
        if (it == items_.end()) {
            throw std::out_of_range{"FieldMap::at: no such key"};
        }
        return it->second;
    }

    /// 不存在时返回空串 —— 比 at() 更适合"有默认值"的读取场景
    [[nodiscard]] std::string_view get(std::string_view key) const {
        auto it = find(key);
        return (it == items_.end()) ? std::string_view{} : it->second;
    }

    [[nodiscard]] bool contains(std::string_view key) const {
        return find(key) != items_.end();
    }

    [[nodiscard]] size_t count(std::string_view key) const {
        return contains(key) ? 1 : 0;
    }

    [[nodiscard]] iterator find(std::string_view key) {
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (it->first == key) {
                return it;
            }
        }
        return items_.end();
    }

    [[nodiscard]] const_iterator find(std::string_view key) const {
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (it->first == key) {
                return it;
            }
        }
        return items_.end();
    }

    [[nodiscard]] size_t size() const noexcept {
        return items_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return items_.empty();
    }

    void clear() noexcept {
        items_.clear();
    }

    [[nodiscard]] iterator begin() noexcept {
        return items_.begin();
    }
    [[nodiscard]] iterator end() noexcept {
        return items_.end();
    }
    [[nodiscard]] const_iterator begin() const noexcept {
        return items_.begin();
    }
    [[nodiscard]] const_iterator end() const noexcept {
        return items_.end();
    }

  private:
    std::vector<Item> items_;
};
} // namespace http
} // namespace netx
