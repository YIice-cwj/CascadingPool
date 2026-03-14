#include "cascading/extent_tree/retained_tree.h"
#include <chrono>
#include "cascading/utils/compiler.h"

namespace cascading {
namespace extent_tree {

/**
 * @brief 构造函数
 * @param expire_time 过期时间戳，默认值为 1000
 * @details 初始化 retained_tree 类，创建空的保留页树。
 */
retained_tree::retained_tree(std::size_t expire_time)
    : retained_tree_(), expire_time_(expire_time), retained_mutex_(), info_() {}

/**
 * @brief 析构函数
 * @details 清空保留页树，释放所有内存。
 */
retained_tree::~retained_tree() {
    clear();
}

/**
 * @brief 分配内存
 * @details 从 retained_tree 类中分配指定大小的内存。
 *          优先复用已释放的地址，如果无法复用则更新为新分配的地址。
 * @param size 要分配的内存大小
 * @return 指向分配内存的 extent 结构体
 */
extent::unique_ptr retained_tree::allocate(std::size_t size) {
    info_.inc_allocate_count();
    if (size == 0 || retained_tree_.empty() || size < PAGE_SIZE) {
        info_.inc_allocate_fail_count();
        return nullptr;
    }

    size = align_to_page(size);

    std::lock_guard<std::mutex> lock(retained_mutex_);
    extent::unique_ptr result = nullptr;
    for (auto it = retained_tree_.begin(); it != retained_tree_.end(); ++it) {
        auto& extent_set = it->second;
        for (auto set_it = extent_set.begin(); set_it != extent_set.end();
             ++set_it) {
            if ((*set_it)->size >= size) {
                result = std::move(extent_set.extract(set_it).value());
                if (extent_set.empty()) {
                    retained_tree_.erase(it);
                }
                break;
            }
        }
        if (result != nullptr) {
            break;
        }
    }

    if (result == nullptr) {
        info_.inc_miss_count();
        info_.inc_allocate_fail_count();
        return nullptr;
    }

    info_.inc_hit_count();

    void* addr = reuse_memory(result->addr, size);
    if (addr == result->addr) {
        result->state.store(extent::state_page::in_use,
                            std::memory_order_release);
        info_.sub_byte_total(result->size);
        info_.add_alloc_total(size);
        return result;
    }

    result->addr = addr;
    result->size = size;
    info_.sub_byte_total(size);
    info_.add_alloc_total(size);
    return result;
}

/**
 * @brief 插入保留页
 * @details 将一个 extent 结构体插入 retained_tree 类中的保留页树中。
 *          插入时需要为 muzzy 状态，会释放物理和虚拟地址但保留地址记录。
 * @param ext 要插入的 Extent 结构体
 * @return 如果插入成功则返回 true，否则返回 false
 */
bool retained_tree::insert(extent::unique_ptr ext) {
    info_.inc_insert_count();
    if (ext == nullptr) {
        info_.inc_insert_fail_count();
        return false;
    }

    // 检查内存块状态是否为 muzzy
    extent::state_page old_state = ext->state.load(std::memory_order_acquire);
    if (old_state == extent::state_page::muzzy) {
        // 尝试将状态转换为 retained
        if (!ext->state.compare_exchange_strong(
                old_state, extent::state_page::retained,
                std::memory_order_release, std::memory_order_acquire)) {
            info_.inc_insert_fail_count();
            return false;
        }

        // 释放物理内存
        if (release_memory(ext->addr, ext->size) != 0) {
            ext->state.store(extent::state_page::muzzy,
                             std::memory_order_release);
            info_.inc_insert_fail_count();
            return false;
        }
    } else if (old_state == extent::state_page::retained) {
    } else {
        info_.inc_insert_fail_count();
        return false;
    }

    ext->expire_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count() +
                       expire_time();

    // 将状态转换为 retained
    ext->state.store(extent::state_page::retained, std::memory_order_release);

    // 插入到保留页树
    auto [it, inserted] =
        retained_tree_[ext->expire_time].insert(std::move(ext));
    info_.add_byte_total((*it)->size);

    return true;
}

/**
 * @brief 回收过期保留页
 * @details 从 retained_tree 类中回收所有过期时间小于等于 expire_time
 * 的保留页。
 * @param expire_time 过期时间戳
 */
void retained_tree::reclaim(std::size_t expire_time) {
    info_.inc_reclaim_count();
    std::lock_guard<std::mutex> lock(retained_mutex_);
    for (auto it = retained_tree_.begin(); it != retained_tree_.end();) {
        if (it->first <= expire_time) {
            for (const auto& ext : it->second) {
                info_.sub_byte_total(ext->size);
            }
            it = retained_tree_.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * @brief 获取过期时间戳
 * @details 返回 retained_tree 类中的过期时间戳。
 * @return 过期时间戳
 */
std::size_t retained_tree::expire_time() const {
    return expire_time_.load(std::memory_order_acquire);
}

/**
 * @brief 设置过期时间戳
 * @details 设置 retained_tree 类中的过期时间戳。
 * @param expire_time 过期时间戳
 */
void retained_tree::set_expire_time(std::size_t expire_time) {
    expire_time_.store(expire_time, std::memory_order_release);
}

/**
 * @brief 清空保留页树
 * @details 清空 retained_tree 类中的保留页树。
 */
void retained_tree::clear() {
    std::lock_guard<std::mutex> lock(retained_mutex_);
    retained_tree_.clear();
}

/**
 * @brief 获取保留页树大小
 * @details 返回 retained_tree 类中的保留页树大小。
 * @return 保留页树大小
 */
std::size_t retained_tree::size() const {
    std::lock_guard<std::mutex> lock(retained_mutex_);
    std::size_t total = 0;
    for (const auto& [expire_time, extent_set] : retained_tree_) {
        total += extent_set.size();
    }
    return total;
}

/**
 * @brief 检查是否为空
 * @details 判断保留页树是否为空。
 * @return 如果为空则返回 true，否则返回 false
 */
bool retained_tree::empty() const {
    std::lock_guard<std::mutex> lock(retained_mutex_);
    return retained_tree_.empty();
}

}  // namespace extent_tree
}  // namespace cascading
