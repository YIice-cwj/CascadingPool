#pragma once
#include <array>
#include <cstddef>
#include "cascading/arena/arena.h"
#include "cascading/thread_cache/cache_bin.h"
#include "cascading/utils/timed_task.h"

namespace cascading {

namespace thread_cache {

/**
 * @brief 线程本地缓存分配器
 * @details 每个线程独立拥有一个实例，无锁操作
 *          缓存未命中时从 arena 批量获取/释放
 */
class thread_cache {
   private:
    // 每个大小类一个缓存槽
    std::array<cache_bin, arena::size_class_table::CLASS_COUNT> bins_;
    // 指向 arena 的指针
    arena::arena& arena_;

   private:
    /**
     * @brief 禁止拷贝构造
     */
    thread_cache(const thread_cache&) = delete;
    /**
     * @brief 禁止拷贝赋值
     */
    thread_cache& operator=(const thread_cache&) = delete;
    /**
     * @brief 禁止移动构造
     */
    thread_cache(thread_cache&&) = delete;
    /**
     * @brief 禁止移动赋值
     */
    thread_cache& operator=(thread_cache&&) = delete;

    /**
     * @brief 从arena批量填充缓存
     */
    void fill_cache(std::size_t class_index, int n);

    /**
     * @brief 刷新指定大小类的缓存到 arena
     * @param class_index 大小类索引
     * @param n 刷新的数量，0 表示全部
     */
    void flush_arena(std::size_t class_index, int n);

   public:
    /**
     * @brief 构造函数
     */
    thread_cache();

    /**
     * @brief 析构函数
     * @details 销毁时将所有缓存刷新到 arena
     */
    ~thread_cache();

    /**
     * @brief 分配一个对象
     * @param size 要分配的对象大小
     * @return void* 指向分配的对象的指针，失败返回 nullptr
     */
    void* allocate(std::size_t size);

    /**
     * @brief 释放一个对象
     * @param ptr 指向要释放的对象的指针
     * @param size 对象大小（用于计算大小类）
     */
    void deallocate(void* ptr, std::size_t size);

    /**
     * @brief 批量分配对象
     * @param ptrs 输出数组，存储分配的指针
     * @param n 请求分配的数量
     * @param size 每个对象的大小
     * @return int 实际分配的数量
     */
    int allocate_batch(void** ptrs, int n, std::size_t size);

    /**
     * @brief 批量释放对象
     * @param ptrs 指向要释放的对象指针数组
     * @param n 释放的数量
     * @param size 每个对象的大小
     * @return int 实际释放的数量
     */
    void deallocate_batch(void** ptrs, int n, std::size_t size);
};

}  // namespace thread_cache
}  // namespace cascading
