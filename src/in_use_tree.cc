#include "../include/cascading/extent_tree/in_use_tree.h"

namespace cascading {
namespace extent_tree {

/**
 * @brief 构造函数
 * @details 初始化 in_use_tree 类，创建空的已使用页树。
 */
in_use_tree::in_use_tree() : in_use_tree_(), in_use_mutex_(), info_() {}

/**
 * @brief 析构函数
 * @details 清空已使用页树，释放所有内存。
 */
in_use_tree::~in_use_tree() {
    clear();
}

/**
 * @brief 插入正在使用的页
 * @details 将一个 extent 结构体插入 in_use_tree 中的已使用页树。
 *          要求 extent 的状态必须为 in_use。
 * @param ext 要插入的 Extent 结构体
 * @return 如果插入成功则返回 true，否则返回 false
 */
bool in_use_tree::insert(extent::unique_ptr ext) {
    info_.inc_insert_count();
    if (ext == nullptr) {
        info_.inc_insert_fail_count();
        return false;
    }

    // 检查内存块状态是否为 InUse
    extent::state_page old_state = ext->state.load(std::memory_order_acquire);
    if (old_state == extent::state_page::in_use) {
        info_.inc_insert_fail_count();
        return false;
    }

    if (ext->state.compare_exchange_strong(
            old_state, extent::state_page::in_use, std::memory_order_release,
            std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(in_use_mutex_);

    if (in_use_tree_.find(ext->addr) != in_use_tree_.end()) {
        info_.inc_insert_fail_count();
        return false;
    }

    auto [it, inserted] = in_use_tree_.emplace(ext->addr, std::move(ext));
    if (!inserted) {
        info_.inc_insert_fail_count();
        return false;
    }

    info_.add_byte_total(it->second->size);

    return true;
}

/**
 * @brief 移除已使用页
 * @details 从 in_use_tree 中移除指定地址的 extent。
 * @param addr 要移除的页地址
 * @return 被移除的 extent 智能指针，如果不存在则返回 nullptr
 */
extent::unique_ptr in_use_tree::remove(void* addr) {
    if (!addr) {
        info_.inc_deallocate_count();
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(in_use_mutex_);

    auto it = in_use_tree_.find(addr);
    if (it == in_use_tree_.end()) {
        return nullptr;
    }

    extent::unique_ptr result = std::move(it->second);
    in_use_tree_.erase(it);

    info_.sub_byte_total(result->size);
    info_.inc_deallocate_count();

    return result;
}

/**
 * @brief 查找已使用页
 * @details 根据地址查找对应的 extent。
 * @param addr 要查找的页地址
 * @return 指向 extent 的指针，如果不存在则返回 nullptr
 */
extent* in_use_tree::find(void* addr) const {
    if (!addr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(in_use_mutex_);

    auto it = in_use_tree_.find(addr);
    if (it != in_use_tree_.end()) {
        return it->second.get();
    }

    return nullptr;
}

/**
 * @brief 检查地址是否存在
 * @details 检查指定地址是否存在于已使用页树中。
 * @param addr 要检查的页地址
 * @return 如果存在则返回 true，否则返回 false
 */
bool in_use_tree::contains(void* addr) const {
    if (!addr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(in_use_mutex_);
    return in_use_tree_.find(addr) != in_use_tree_.end();
}

/**
 * @brief 清空已使用页树
 * @details 清空 in_use_tree 中的所有 extent。
 */
void in_use_tree::clear() {
    std::lock_guard<std::mutex> lock(in_use_mutex_);

    if (in_use_tree_.empty()) {
        return;
    }

    in_use_tree_.clear();
}

/**
 * @brief 获取已使用页树大小
 * @details 返回 in_use_tree 中的已使用页数量。
 * @return 已使用页数量
 */
std::size_t in_use_tree::size() const {
    std::lock_guard<std::mutex> lock(in_use_mutex_);
    return in_use_tree_.size();
}

/**
 * @brief 检查是否为空
 * @details 判断已使用页树是否为空。
 * @return 如果为空则返回 true，否则返回 false
 */
bool in_use_tree::empty() const {
    std::lock_guard<std::mutex> lock(in_use_mutex_);
    return in_use_tree_.empty();
}

}  // namespace extent_tree
}  // namespace cascading
