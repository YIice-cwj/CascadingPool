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
 * @brief 保留页树, 键为过期时间，值为 extent 智能指针
 * @details 用于管理保留的模糊页 extent 智能指针，按照过期时间排序。
 *          特点：
 *          1. 不需要合并
 *          2. 插入需要 muzzy 状态
 *          3. 释放物理和虚拟地址但保留已释放的地址记录
 *          4. 分配时复用已释放的地址，如果无法复用则更新为新分配的地址
 */
class retained_tree {
   private:
    using retained_tree_t = std::map<std::size_t, std::set<extent::unique_ptr>>;

   private:
    // 保留页树: 键为过期时间，值为 extent 智能指针集合
    retained_tree_t retained_tree_;

    // 过期时间戳，用于为新插入的保留页分配过期时间
    std::atomic<std::size_t> expire_time_;

    // 互斥锁，用于保护 retained_tree_
    mutable std::mutex retained_mutex_;
    // 统计信息
    tree_info info_;

   public:
    /**
     * @brief 构造函数
     * @param expire_time 过期时间戳，默认值为 1000
     * @details 初始化 retained_tree 类，创建空的保留页树。
     */
    retained_tree(std::size_t expire_time = 1000);

    /**
     * @brief 析构函数
     * @details 清空保留页树，释放所有内存。
     */
    ~retained_tree();

    /**
     * @brief 分配内存
     * @details 从 retained_tree 类中分配指定大小的内存。
     *          优先复用已释放的地址，如果无法复用则更新为新分配的地址。
     * @param size 要分配的内存大小
     * @return 指向分配内存的 extent 结构体
     */
    extent::unique_ptr allocate(std::size_t size);

    /**
     * @brief 插入保留页
     * @details 将一个 extent 结构体插入 retained_tree 类中的保留页树中。
     *          插入时需要为 muzzy 状态，会释放物理和虚拟地址但保留地址记录。
     * @param ext 要插入的 Extent 结构体
     * @return 如果插入成功则返回 true，否则返回 false
     */
    bool insert(extent::unique_ptr ext);

    /**
     * @brief 回收过期保留页
     * @details 从 retained_tree 类中回收所有过期时间小于等于 expire_time
     * 的保留页。
     * @param expire_time 过期时间戳
     * @return 回收的保留页 Extent 结构体向量
     */
    void reclaim(std::size_t expire_time);

    /**
     * @brief 获取过期时间戳
     * @details 返回 retained_tree 类中的过期时间戳。
     * @return 过期时间戳
     */
    std::size_t expire_time() const;

    /**
     * @brief 设置过期时间戳
     * @details 设置 retained_tree 类中的过期时间戳。
     * @param expire_time 过期时间戳
     */
    void set_expire_time(std::size_t expire_time);

    /**
     * @brief 清空保留页树
     * @details 清空 retained_tree 类中的保留页树。
     */
    void clear();

    /**
     * @brief 获取保留页树大小
     * @details 返回 retained_tree 类中的保留页树大小。
     * @return 保留页树大小
     */
    std::size_t size() const;

    /**
     * @brief 检查是否为空
     * @details 判断保留页树是否为空。
     * @return 如果为空则返回 true，否则返回 false
     */
    bool empty() const;

    /**
     * @brief 获取统计信息
     * @details 返回 retained_tree 类的统计信息。
     * @return 统计信息
     */
    const tree_info& info() const { return info_; }
};

}  // namespace extent_tree
}  // namespace cascading
