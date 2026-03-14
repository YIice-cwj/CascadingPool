#pragma once
#include <cstddef>
#include "cascading/thread_cache/thread_cache_manager.h"

namespace cascading {

/**
 * @brief 从线程缓存分配内存
 * @param size 要分配的字节数
 * @return void* 分配的内存指针，失败返回 nullptr
 */
void* allocate(std::size_t size);

/**
 * @brief 释放内存到线程缓存
 * @param ptr 要释放的内存指针
 * @param size 内存块大小
 */
void deallocate(void* ptr, std::size_t size);

/**
 * @brief 批量分配内存
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @param size 每个对象的大小
 * @return int 实际分配的数量
 */
int allocate_batch(void** ptrs, int n, std::size_t size);

/**
 * @brief 批量释放内存
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 * @param size 每个对象的大小
 */
void deallocate_batch(void** ptrs, int n, std::size_t size);

/**
 * @brief 初始化内存池
 * @return bool 初始化成功返回 true
 */
bool initialize();

/**
 * @brief 关闭内存池
 * @details 清理所有线程缓存，释放资源
 */
void shutdown();

/**
 * @brief 获取当前线程缓存数量
 * @return std::size_t 活跃线程缓存数量
 */
std::size_t get_thread_cache_count();

/**
 * @brief 清理当前线程的缓存
 */
void cleanup_current_thread();

}  // namespace cascading
