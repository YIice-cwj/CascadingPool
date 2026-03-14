#pragma once
#include <cstddef>
#include <cstdint>
#include "cascading/arena/size_class_table.h"

namespace cascading {
namespace thread_cache {

// 线程缓存配置常量
constexpr std::size_t TCACHE_SIZE = 32;         // 每个大小类的缓存槽位数
constexpr std::size_t TCACHE_FILL_BATCH = 16;   // 从 arena 批量填充的数量
constexpr std::size_t TCACHE_FLUSH_BATCH = 16;  // 刷新到 arena 的阈值

/**
 * @brief 单个大小类的缓存槽
 * @details 固定大小的数组缓存，使用简单的栈结构（LIFO）
 *          LIFO 有利于缓存局部性，最近释放的内存最可能被再次使用
 */
struct cache_bin {
    // 缓存的指针数组
    void* ptrs[TCACHE_SIZE] = {};
    // 当前缓存数量
    uint16_t ncached = 0;

    cache_bin() : ptrs{}, ncached(0) {}

    /**
     * @brief 从缓存分配一个对象
     * @return void* 分配的指针，缓存空返回 nullptr
     */
    void* allocate();

    /**
     * @brief 释放对象到缓存
     * @param ptr 要缓存的指针
     * @return bool 是否成功缓存，缓存满返回 false
     */
    bool deallocate(void* ptr);

    /**
     * @brief 批量从缓存分配对象
     * @param ptrs 批量缓存的指针数组
     * @param n 要批量缓存的指针数量
     * @return int 缓存成功数量
     */
    int allocate_batch(void** ptrs, int n);

    /**
     * @brief 批量释放对象到缓存
     * @param ptrs 批量缓存的指针数组
     * @param n 要批量缓存的指针数量
     */
    void deallocate_batch(void** ptrs, int n);

    /**
     * @brief 获取需要填充的数量
     * @return std::size_t 如果需要填充缓存则返回填充数量，否则返回 0
     */
    std::size_t need_fill() const;

    /**
     * @brief 检查缓存是否为空
     * @return bool 如果缓存为空则返回 true
     */
    bool empty() const;

    /**
     * @brief 检查是否需要填充缓存
     * @return std::size_t 如果需要填充缓存则返回填充数量，否则返回 0
     */
    bool is_fill() const;

    /**
     * @brief 检查缓存是否已满
     * @return bool 如果缓存已满则返回 true
     */
    bool full() const;
};

}  // namespace thread_cache
}  // namespace cascading
