#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include "cascading/extent_tree/extent.h"
#include "cascading/extent_tree/tree_info.h"

namespace cascading {
namespace extent_tree {

/**
 * @brief 模糊页树，键为内存地址，值为 extent 智能指针
 * @details 用于存储所有当前被标记为模糊页的 extent 智能指针。
 *         与 dirty_tree 的区别：
 *         1. 插入时需要为 dirty 状态，并释放物理地址
 *         2. 分配时会重新申请物理地址
 *         3. 回收时需要完全合并成完整的页才能回收
 */
class muzzy_tree {
   private:
    // 模糊页树，键为内存地址，值为 extent 智能指针
    using muzzy_tree_t = std::map<void*, extent::unique_ptr>;
    // 模糊页过期索引，键为过期时间戳，值为内存地址集合
    using muzzy_expire_index_t = std::map<std::size_t, std::set<void*>>;

   private:
    // 模糊页树，键为内存地址，值为 extent::unique_ptr 智能指针
    muzzy_tree_t muzzy_tree_;
    // 过期索引树，键为过期时间戳，值为地址集合（双向索引）
    muzzy_expire_index_t muzzy_expire_index_;
    // 过期时间戳，用于为新插入的模糊页分配过期时间
    std::atomic<std::size_t> expire_time_;
    // 互斥锁，用于保护 muzzy_tree_、 muzzy_expire_index_
    mutable std::mutex muzzy_mutex_;
    // 统计信息
    tree_info info_;

   private:
    /**
     * @brief 分割 extent 结构体
     * @details 将一个 extent 结构体分割为两个 extent 结构体，一个大小为
     * size，另一个大小为原大小减去 size。
     * @param it 指向要分割的 extent 结构体的迭代器
     * @param size 要分割的大小
     * @return 指向新创建的 extent 结构体的智能指针
     */
    extent::unique_ptr split_extent(muzzy_tree_t::iterator& it,
                                    std::size_t size);

    /**
     * @brief 检查两个模糊页是否可以合并
     * @return true 如果可以合并
     */
    bool check_merge(const extent::unique_ptr& ext1,
                     const extent::unique_ptr& ext2);

    /**
     * @brief 尝试恢复原始类型
     * @details 如果一个 extent 结构体的大小等于其原始大小，且类型为 primary，
     * 则将其类型恢复为 original。
     * @param ext 要尝试恢复原始类型的 Extent 结构体
     */
    void try_restore_original_type(extent::unique_ptr& ext);

    /**
     * @brief 更新过期索引
     * @details 更新 muzzy_expire_index_ 中过期时间戳为 expire_time
     * 的地址集合， 移除 addr 地址对应的模糊页。
     * @param ext 要更新过期索引的 Extent 结构体
     */
    void update_expire_index(extent::unique_ptr& ext);

   public:
    /**
     * @brief 构造函数
     * @details 初始化模糊页树、过期索引树、过期时间戳为 1000。
     * @param expire_time 过期时间戳
     */
    muzzy_tree(std::size_t expire_time = 1000);

    /**
     * @brief 析构函数
     * @details 清空模糊页树、过期索引树。
     */
    ~muzzy_tree();

    /**
     * @brief 分配内存
     * @details 从 muzzy_tree 类中分配指定大小的内存，需要重新申请物理内存。
     * @param size 要分配的内存大小
     * @return 指向分配内存的指针
     */
    extent::unique_ptr allocate(std::size_t size);

    /**
     * @brief 插入模糊页
     * @param ext extent 结构体
     * @details 向模糊页树中插入一个新的模糊页，同时更新过期索引树。
     *          插入时需要为 dirty 状态，并释放物理地址。
     * @return 如果插入成功则返回 true，否则返回 false
     */
    bool insert(extent::unique_ptr ext);

    /**
     * @brief 回收过期模糊页
     * @details 从 muzzy_tree 类中回收所有过期时间小于等于 expire_time
     * 的模糊页。 只有完全合并成完整的页才能回收。
     * @param expire_time 过期时间戳
     * @return 回收的模糊页 Extent 结构体向量
     */
    std::vector<extent::unique_ptr> reclaim(std::size_t expire_time);

    /**
     * @brief 合并相邻模糊页
     * @details 合并 muzzy_tree 类中所有相邻的模糊页，以减少内存碎片。
     */
    void merge();

    /**
     * @brief 获取过期时间戳
     * @details 返回 muzzy_tree 类中的过期时间戳。
     * @return 过期时间戳
     */
    std::size_t expire_time() const;

    /**
     * @brief 设置过期时间戳
     * @details 设置 muzzy_tree 类中的过期时间戳。
     * @param expire_time 过期时间戳
     */
    void set_expire_time(std::size_t expire_time);

    /**
     * @brief 清空模糊页树
     * @details 清空 muzzy_tree 类中的模糊页树、过期索引树。
     */
    void clear();

    /**
     * @brief 获取模糊页树大小
     * @details 返回 muzzy_tree 类中的模糊页树大小。
     * @return 模糊页树大小
     */
    std::size_t size() const;

    /**
     * @brief 检查是否为空
     * @details 判断模糊页树是否为空。
     * @return 如果为空则返回 true，否则返回 false
     */
    bool empty() const;

    /**
     * @brief 获取统计信息
     * @details 返回 muzzy_tree 类的统计信息。
     * @return 统计信息
     */
    const tree_info& info() const { return info_; }
};

}  // namespace extent_tree
}  // namespace cascading
