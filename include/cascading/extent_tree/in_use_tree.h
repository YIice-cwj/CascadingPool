#pragma once
#include <mutex>
#include <unordered_map>
#include "cascading/extent_tree/extent.h"
#include "cascading/extent_tree/tree_info.h"

namespace cascading {
namespace extent_tree {

/**
 * @brief 已使用页树, 键为页地址，值为 extent 智能指针
 * @details 维护已使用的页，根据页地址索引。用于追踪当前正在使用的内存页。
 */
class in_use_tree {
   private:
    using in_use_tree_t = std::unordered_map<void*, extent::unique_ptr>;

   private:
    in_use_tree_t in_use_tree_;
    mutable std::mutex in_use_mutex_;
    tree_info info_;

   public:
    /**
     * @brief 构造函数
     * @details 初始化 in_use_tree 类，创建空的已使用页树。
     */
    in_use_tree();

    /**
     * @brief 析构函数
     * @details 清空已使用页树，释放所有内存。
     */
    ~in_use_tree();

    /**
     * @brief 插入正在使用的页
     * @details 将一个 extent 结构体插入 in_use_tree 中的已使用页树。
     * @param ext 要插入的 Extent 结构体
     * @return 如果插入成功则返回 true，否则返回 false
     */
    bool insert(extent::unique_ptr ext);

    /**
     * @brief 移除已使用页
     * @details 从 in_use_tree 中移除指定地址的 extent。
     * @param addr 要移除的页地址
     * @return 被移除的 extent 智能指针，如果不存在则返回 nullptr
     */
    extent::unique_ptr remove(void* addr);

    /**
     * @brief 查找已使用页
     * @details 根据地址查找对应的 extent。
     * @param addr 要查找的页地址
     * @return 指向 extent 的指针，如果不存在则返回 nullptr
     */
    extent* find(void* addr) const;

    /**
     * @brief 检查地址是否存在
     * @details 检查指定地址是否存在于已使用页树中。
     * @param addr 要检查的页地址
     * @return 如果存在则返回 true，否则返回 false
     */
    bool contains(void* addr) const;

    /**
     * @brief 清空已使用页树
     * @details 清空 in_use_tree 中的所有 extent。
     */
    void clear();

    /**
     * @brief 获取已使用页树大小
     * @details 返回 in_use_tree 中的已使用页数量。
     * @return 已使用页数量
     */
    std::size_t size() const;

    /**
     * @brief 检查是否为空
     * @details 判断已使用页树是否为空。
     * @return 如果为空则返回 true，否则返回 false
     */
    bool empty() const;

    /**
     * @brief 获取统计信息
     * @details 返回 in_use_tree 的统计信息。
     * @return 统计信息
     */
    const tree_info& info() const { return info_; }
};

}  // namespace extent_tree
}  // namespace cascading
