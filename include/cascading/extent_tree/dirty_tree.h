#pragma once
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include "cascading/extent_tree/extent.h"
#include "cascading/extent_tree/tree_info.h"

namespace cascading {
namespace extent_tree {

/**
 * @brief 脏页树，键为内存地址，值为 extent 智能指针
 * @details 用于存储所有当前被标记为脏页的 extent 智能指针。
 */
class dirty_tree {
   private:
    // 脏页树，键为内存地址，值为 extent 智能指针
    using dirty_tree_t = std::map<void*, extent::unique_ptr>;
    // 过期索引树，键为过期时间戳，值为地址集合（双向索引）
    using dirty_expire_index_t = std::map<std::size_t, std::set<void*>>;

   private:
    // 脏页树，键为内存地址，值为 extent::unique_ptr 智能指针
    dirty_tree_t dirty_tree_;
    // 过期索引树，键为过期时间戳，值为地址集合（双向索引）
    dirty_expire_index_t dirty_expire_index_;
    // 过期时间戳，用于为新插入的脏页分配过期时间
    std::atomic<std::size_t> expire_time_;
    // 互斥锁，用于保护 dirty_tree_、 dirty_expire_index_
    mutable std::mutex dirty_mutex_;
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
    extent::unique_ptr split_extent(dirty_tree_t::iterator& it,
                                    std::size_t size);

    /**
     * @brief 检查两个脏页是否可以合并
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
     * @details 更新 dirty_expire_index_ 中过期时间戳为 expire_time 的地址集合，
     * 移除 addr 地址对应的脏页。
     * @param ext 要更新过期索引的 Extent 结构体
     */
    void update_expire_index(extent::unique_ptr& ext);

   public:
    /**
     * @brief 构造函数
     * @param expire_time 过期时间戳，默认值为 1000
     * @details 初始化 dirty_tree 类，创建空的脏页树和过期索引树。
     */
    dirty_tree(std::size_t expire_time = 1000);

    /**
     * @brief 析构函数
     * @details 清空脏页树和过期索引树，释放所有内存。
     */
    ~dirty_tree();

    /**
     * @brief 分配内存(页对齐)
     * @details 从 dirty_tree 类中分配指定大小的内存。
     * @param size 要分配的内存大小
     * @return 指向分配内存的指针
     */
    extent::unique_ptr allocate(std::size_t size);

    /**
     * @brief 插入脏页
     * @details 将一个 extent 结构体插入 dirty_tree 类中的脏页树中。
     * @param extent 要插入的 Extent 结构体
     * @return 如果插入成功则返回 true，否则返回 false
     */
    bool insert(extent::unique_ptr ext);

    /**
     * @brief 回收过期脏页
     * @details 从 dirty_tree 类中回收所有过期时间小于等于 expire_time 的脏页。
     * @param expire_time 过期时间戳
     * @return 回收的脏页 Extent 结构体向量
     */
    std::vector<extent::unique_ptr> reclaim(std::size_t expire_time);

    /**
     * @brief 合并相邻脏页
     * @details 合并 dirty_tree 类中所有相邻的脏页，以减少内存碎片。
     * @return 如果合并成功则返回 true，否则返回 false
     */
    void merge();

    /**
     * @brief 获取过期时间戳
     * @details 返回 dirty_tree 类中的过期时间戳。
     * @return 过期时间戳
     */
    std::size_t expire_time() const;

    /**
     * @brief 设置过期时间戳
     * @details 设置 dirty_tree 类中的过期时间戳。
     * @param expire_time 过期时间戳
     * @return 过期时间戳
     */
    void set_expire_time(std::size_t expire_time);

    /**
     * @brief 清空脏页树
     * @details 清空 dirty_tree 类中的脏页树和过期索引树。
     */
    void clear();

    /**
     * @brief 获取脏页树大小
     * @details 返回 dirty_tree 类中的脏页树大小。
     * @return 脏页树大小
     */
    std::size_t size() const;

    /**
     * @brief 检查是否为空
     * @details 判断脏页树是否为空。
     * @return 如果为空则返回 true，否则返回 false
     */
    bool empty() const;

    /**
     * @brief 获取统计信息
     * @details 返回 dirty_tree 类的统计信息。
     * @return 统计信息
     */
    const tree_info& info() const { return info_; }
};

}  // namespace extent_tree
}  // namespace cascading