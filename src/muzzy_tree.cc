#include "cascading/extent_tree/muzzy_tree.h"
#include "cascading/utils/compiler.h"

namespace cascading {
namespace extent_tree {

/**
 * @brief 分割 extent 结构体
 * @details 将一个 extent 结构体分割为两个 extent 结构体，一个大小为
 * size，另一个大小为原大小减去 size。
 * @param it 指向要分割的 extent 结构体的迭代器
 * @param size 要分割的大小
 * @return 指向新创建的 extent 结构体的智能指针
 */
extent::unique_ptr muzzy_tree::split_extent(muzzy_tree_t::iterator& it,
                                            std::size_t size) {
    if (it->second->type.load(std::memory_order_acquire) ==
            extent::page_type::original &&
        it->second->original_size == 0) {
        it->second->type.store(extent::page_type::primary,
                               std::memory_order_release);
        it->second->original_size = it->second->size;
    }

    it->second->size -= size;
    extent::unique_ptr ext = std::make_unique<extent>(
        static_cast<char*>(it->second->addr) + it->second->size, size,
        extent::state_page::in_use, extent::page_type::split);
    return ext;
}

/**
 * @brief 检查两个模糊页是否可以合并
 * @return true 如果可以合并
 */
bool muzzy_tree::check_merge(const extent::unique_ptr& ext1,
                             const extent::unique_ptr& ext2) {
    // 检查 ext1 是否为原始页
    if (ext1->type.load(std::memory_order_acquire) ==
        extent::page_type::original) {
        return false;
    }

    // 检查 ext2 是否紧跟 ext1 后面
    if (ext2->addr != static_cast<char*>(ext1->addr) + ext1->size) {
        return false;
    }

    // 检查 ext1 和 ext2 是否都是主页
    const auto ext1_type = ext1->type.load(std::memory_order_acquire);
    const auto ext2_type = ext2->type.load(std::memory_order_acquire);
    if (ext1_type == extent::page_type::primary &&
        ext2_type == extent::page_type::primary) {
        return false;
    }

    return true;
}

/**
 * @brief 尝试恢复原始类型
 * @details 如果一个 extent 结构体的大小等于其原始大小，且类型为 primary，
 * 则将其类型恢复为 original。
 * @param ext 要尝试恢复原始类型的 Extent 结构体
 */
void muzzy_tree::try_restore_original_type(extent::unique_ptr& ext) {
    if (ext->type.load(std::memory_order_acquire) ==
            extent::page_type::primary &&
        ext->size == ext->original_size) {
        ext->type.store(extent::page_type::original, std::memory_order_release);
    }
}

/**
 * @brief 更新过期索引
 * @details 更新 muzzy_expire_index_ 中过期时间戳为 expire_time 的地址集合，
 * 移除 addr 地址对应的模糊页。
 * @param ext 要更新过期索引的 Extent 结构体
 */
void muzzy_tree::update_expire_index(extent::unique_ptr& ext) {
    auto it_expire = muzzy_expire_index_.find(ext->expire_time);
    if (it_expire != muzzy_expire_index_.end()) {
        it_expire->second.erase(ext->addr);
        if (it_expire->second.empty()) {
            muzzy_expire_index_.erase(it_expire);
        }
    }
}

/**
 * @brief 构造函数
 * @details 初始化模糊页树、过期索引树、过期时间戳为 0。
 * @param expire_time 过期时间戳
 */
muzzy_tree::muzzy_tree(std::size_t expire_time)
    : muzzy_tree_(),
      muzzy_expire_index_(),
      expire_time_(expire_time),
      muzzy_mutex_(),
      info_() {}

/**
 * @brief 析构函数
 * @details 清空模糊页树、过期索引树。
 */
muzzy_tree::~muzzy_tree() {
    clear();
}

/**
 * @brief 分配内存
 * @details 从 muzzy_tree 类中分配指定大小的内存，需要重新申请物理内存。
 * @param size 要分配的内存大小
 * @return 指向分配内存的指针
 */
extent::unique_ptr muzzy_tree::allocate(std::size_t size) {
    info_.inc_allocate_count();
    if (size == 0 || muzzy_tree_.empty() || size < PAGE_SIZE) {
        info_.inc_allocate_fail_count();
        return nullptr;
    }

    // 对齐到页大小
    size = align_to_page(size);

    std::lock_guard<std::mutex> lock(muzzy_mutex_);

    // 遍历模糊页树，查找第一个大小大于等于请求大小的内存块
    muzzy_tree_t::iterator result_it = muzzy_tree_.end();
    extent::unique_ptr result = nullptr;
    for (auto it = muzzy_tree_.begin(); it != muzzy_tree_.end(); ++it) {
        if (it->second->size >= size) {
            result_it = it;
            break;
        }
    }

    // 如果没有找到合适的内存块，则返回 nullptr
    if (result_it == muzzy_tree_.end()) {
        info_.inc_miss_count();
        info_.inc_allocate_fail_count();
        return nullptr;
    }

    info_.inc_hit_count();

    // 如果当前内存块大小大于等于请求大小 + 页大小， 则进行分页
    if (result_it->second->size >= size + PAGE_SIZE) {
        info_.inc_split_count();
        result = split_extent(result_it, size);
    } else {
        result = std::move(result_it->second);
        muzzy_tree_.erase(result_it);
    }

    // 申请物理内存
    if (commit_memory(result->addr, result->size) != 0) {
        insert(std::move(result));
        return nullptr;
    }

    update_expire_index(result);
    info_.add_alloc_total(result->size);
    info_.sub_byte_total(result->size);
    result->state.store(extent::state_page::in_use, std::memory_order_release);
    return result;
}

/**
 * @brief 插入模糊页
 * @param ext extent 结构体
 * @details 向模糊页树中插入一个新的模糊页，同时更新过期索引树。
 *          插入时需要为 dirty 状态，并释放物理地址。
 * @return 如果插入成功则返回 true，否则返回 false
 */
bool muzzy_tree::insert(extent::unique_ptr ext) {
    info_.inc_insert_count();
    if (ext == nullptr) {
        info_.inc_insert_fail_count();
        return false;
    }

    // 检查内存块状态是否为 dirty 或 muzzy
    extent::state_page old_state = ext->state.load(std::memory_order_acquire);

    if (old_state == extent::state_page::dirty) {
        // 从 dirty 转换为 muzzy，需要释放物理内存
        if (!ext->state.compare_exchange_strong(
                old_state, extent::state_page::muzzy, std::memory_order_release,
                std::memory_order_acquire)) {
            info_.inc_insert_fail_count();
            return false;
        }

        // 释放物理内存
        if (decommit_memory(ext->addr, ext->size) != 0) {
            ext->state.store(extent::state_page::dirty,
                             std::memory_order_release);
            info_.inc_insert_fail_count();
            return false;
        }
    } else if (old_state == extent::state_page::muzzy) {
    } else {
        info_.inc_insert_fail_count();
        return false;
    }

    std::lock_guard<std::mutex> lock(muzzy_mutex_);

    // 设置过期时间
    ext->expire_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count() +
                       expire_time();

    // 插入内存块到模糊页树
    auto [it, inserted] = muzzy_tree_.emplace(ext->addr, std::move(ext));
    if (!inserted) {
        info_.inc_insert_fail_count();
        return false;
    }
    info_.add_byte_total(it->second->size);
    muzzy_expire_index_[it->second->expire_time].insert(it->first);

    return true;
}

/**
 * @brief 回收过期模糊页
 * @details 从 muzzy_tree 类中回收所有过期时间小于等于 expire_time 的模糊页。
 *          只有完全合并成完整的页才能回收。
 * @param expire_time 过期时间戳
 * @return 回收的模糊页 Extent 结构体向量
 */
std::vector<extent::unique_ptr> muzzy_tree::reclaim(std::size_t expire_time) {
    info_.inc_reclaim_count();
    merge();
    std::lock_guard<std::mutex> lock(muzzy_mutex_);
    if (muzzy_expire_index_.empty()) {
        return {};
    }

    std::vector<extent::unique_ptr> result;
    result.reserve(muzzy_tree_.size());

    auto it = muzzy_expire_index_.begin();
    while (it != muzzy_expire_index_.end() && it->first <= expire_time) {
        for (auto& addr : it->second) {
            auto tree_it = muzzy_tree_.find(addr);
            if (tree_it != muzzy_tree_.end() &&
                tree_it->second->type == extent::page_type::original) {
                info_.sub_byte_total(tree_it->second->size);
                result.push_back(std::move(tree_it->second));
                muzzy_tree_.erase(tree_it);
                muzzy_expire_index_[it->first].erase(addr);
            }
        }

        if (it->second.empty()) {
            it = muzzy_expire_index_.erase(it);
        } else {
            ++it;
        }
    }

    return result;
}

/**
 * @brief 尝试合并所有相邻的模糊页
 * @details 合并 muzzy_tree 类中所有相邻的模糊页，以减少内存碎片。
 */
void muzzy_tree::merge() {
    std::lock_guard<std::mutex> lock(muzzy_mutex_);
    if (muzzy_tree_.empty()) {
        return;
    }
    auto it = muzzy_tree_.begin();
    while (it != muzzy_tree_.end()) {
        auto next_it = it;
        ++next_it;
        if (next_it != muzzy_tree_.end() &&
            check_merge(it->second, next_it->second)) {
            info_.inc_merge_count();
            it->second->size += next_it->second->size;

            // 如果合并后恢复为原始大小，重置为原始页类型
            try_restore_original_type(it->second);

            // 更新过期索引
            update_expire_index(it->second);
            update_expire_index(next_it->second);

            // 更新过期时间为两者中较晚的
            it->second->expire_time =
                std::max(it->second->expire_time, next_it->second->expire_time);
            muzzy_expire_index_[it->second->expire_time].insert(it->first);

            muzzy_tree_.erase(next_it);

        } else {
            ++it;
        }
    }
}

/**
 * @brief 获取过期时间戳
 * @details 返回 muzzy_tree 类中的过期时间戳。
 * @return 过期时间戳
 */
std::size_t muzzy_tree::expire_time() const {
    return expire_time_.load(std::memory_order_acquire);
}

/**
 * @brief 设置过期时间戳
 * @details 设置 muzzy_tree 类中的过期时间戳。
 * @param expire_time 过期时间戳
 */
void muzzy_tree::set_expire_time(std::size_t expire_time) {
    expire_time_.store(expire_time, std::memory_order_release);
}

/**
 * @brief 清空模糊页树
 * @details 清空 muzzy_tree 类中的模糊页树、过期索引树。
 */
void muzzy_tree::clear() {
    std::lock_guard<std::mutex> lock(muzzy_mutex_);
    muzzy_tree_.clear();
    muzzy_expire_index_.clear();
}

/**
 * @brief 获取模糊页树大小
 * @details 返回 muzzy_tree 类中的模糊页树大小。
 * @return 模糊页树大小
 */
std::size_t muzzy_tree::size() const {
    std::lock_guard<std::mutex> lock(muzzy_mutex_);
    return muzzy_tree_.size();
}

/**
 * @brief 检查是否为空
 * @details 判断模糊页树是否为空。
 * @return 如果为空则返回 true，否则返回 false
 */
bool muzzy_tree::empty() const {
    std::lock_guard<std::mutex> lock(muzzy_mutex_);
    return muzzy_tree_.empty();
}

}  // namespace extent_tree
}  // namespace cascading
