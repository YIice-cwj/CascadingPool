#pragma once
#include <atomic>
#include "cascading/extent_tree/dirty_tree.h"
#include "cascading/extent_tree/in_use_tree.h"
#include "cascading/extent_tree/muzzy_tree.h"
#include "cascading/extent_tree/retained_tree.h"
#include "cascading/utils/timed_task.h"

namespace cascading {
namespace extent_tree {

class extent_manager {
   public:
    static constexpr std::size_t MAX_BLOCK_SIZE = 32 * 1024;
    static constexpr std::size_t DIRTY_RECLAIM_INTERVAL = 30000;
    static constexpr std::size_t MUZZY_RECLAIM_INTERVAL = 60000;
    static constexpr std::size_t RETAINED_RECLAIM_INTERVAL = 300000;
    static constexpr std::size_t RECLAIM_RECYCLER_INTERVAL = 10000;
    static constexpr std::size_t PREALLOC_MIN_SIZE = 4 * 1024;
    static constexpr std::size_t PREALLOC_MAX_SIZE = 32 * 1024;
    static constexpr std::size_t PREALLOC_COUNT_PER_SIZE = 8;

   private:
    std::unique_ptr<in_use_tree> in_use_tree_;
    std::unique_ptr<cascading::extent_tree::dirty_tree> dirty_tree_;
    std::unique_ptr<cascading::extent_tree::muzzy_tree> muzzy_tree_;
    std::unique_ptr<cascading::extent_tree::retained_tree> retained_tree_;
    std::vector<extent::unique_ptr> reclaim_extents_;
    utils::timed_task reclaim_recycler_task_;

   private:
    /**
     * @brief 从系统分配一块内存
     * @details 从系统分配一块内存，
     * 并将其状态设置为 in_use。
     * @param size 要分配的内存大小
     * @return void* 指向分配内存的指针
     */
    void* allocate_system(std::size_t size);

    /**
     * @brief 预分配内存块到 dirty_tree
     * @details 初始化时预分配常用大小的内存块，避免运行时系统调用开销
     */
    void preallocate_medium_blocks();

    /**
     * @brief 回收脏页
     * @details 回收 dirty_tree_ 中过期的脏页。
     * @param expire_time 过期时间戳
     */
    void dirty_reclaim(std::size_t expire_time);

    /**
     * @brief 回收过期模糊页
     * @details 从 muzzy_tree 类中回收所有过期时间小于等于 expire_time
     * 的模糊页。 只有完全合并成完整的页才能回收。
     * @param expire_time 过期时间戳
     */
    void muzzy_reclaim(std::size_t expire_time);

    /**
     * @brief 根据内存压力更新过期时间
     * @details 根据当前内存压力，
     * 更新 dirty_tree_、 muzzy_tree_、 retained_tree_ 中的过期时间。
     */
    void update_expire_time();

   private:
    /**
     * @brief 禁用拷贝构造函数
     * @details 防止通过拷贝构造函数创建新的实例。
     */
    extent_manager(const extent_manager&) = delete;

    /**
     * @brief 禁用赋值运算符
     * @details 防止通过赋值运算符赋值。
     */
    extent_manager& operator=(const extent_manager&) = delete;

    /**
     * @brief 构造函数
     * @details 创建一个 extent_manager
     * 类的实例，初始化脏页树、模糊页树、保留页树。
     */
    extent_manager();

   public:
    /**
     * @brief 析构函数
     * @details 清空脏页树、模糊页树、保留页树。
     */
    ~extent_manager();

    /**
     * @brief 分配一块内存
     * @details 从 extent_manager 中分配一块内存，
     * 并将其标记为已使用。
     * @return void* 指向分配内存的指针
     */
    void* allocate(std::size_t size);

    /**
     * @brief
     * @brief 释放一块内存
     * @details 释放 extent_manager 中指向 addr 的内存，
     * 并将其标记为未使用。
     * @param addr 要释放的内存地址
     * @param size 要释放的内存大小
     */
    void deallocate(void* addr, std::size_t size);

    /**
     * @brief 回收过期内存
     * @details 回收 dirty_tree_、 muzzy_tree_、 retained_tree_ 中过期的内存。
     */
    void reclaim();

    /**
     * @brief 获取内存压力
     * @details 获取当前内存压力，
     * 包括脏页树、模糊页树、保留页树中的内存数量。
     * @return std::size_t 内存压力值（0~1之间的浮点数）
     */
    double get_memory_pressure();

    /**
     * @brief 获取 dirty_tree 的过期时间
     * @return std::size_t 过期时间（毫秒）
     */
    std::size_t get_dirty_expire_time() const {
        return dirty_tree_->expire_time();
    }

    /**
     * @brief 获取 muzzy_tree 的过期时间
     * @return std::size_t 过期时间（毫秒）
     */
    std::size_t get_muzzy_expire_time() const {
        return muzzy_tree_->expire_time();
    }

    /**
     * @brief 获取 retained_tree 的过期时间
     * @return std::size_t 过期时间（毫秒）
     */
    std::size_t get_retained_expire_time() const {
        return retained_tree_->expire_time();
    }

    /**
     * @brief 获取单例实例
     * @details 获取 extent_manager 类的单例实例。
     * @return extent_manager& 单例实例引用
     */
    static extent_manager& get_instance();

    /**
     * @brief 静态分配内存
     * @details 通过单例实例分配内存的便捷静态方法
     * @param size 要分配的内存大小
     * @return void* 指向分配内存的指针
     */
    static void* static_allocate(std::size_t size);

    /**
     * @brief 静态释放内存
     * @details 通过单例实例释放内存的便捷静态方法
     * @param addr 要释放的内存地址
     * @param size 要释放的内存大小
     */
    static void static_deallocate(void* addr, std::size_t size);

    /**
     * @brief 静态回收内存
     * @details 通过单例实例回收内存的便捷静态方法
     */
    static void static_reclaim();
};

}  // namespace extent_tree
}  // namespace cascading
