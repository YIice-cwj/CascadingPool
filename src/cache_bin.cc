#include "cascading/thread_cache/cache_bin.h"

namespace cascading {
namespace thread_cache {

/**
 * @brief 从缓存分配一个对象
 * @return void* 分配的指针，缓存空返回 nullptr
 */
void* cache_bin::allocate() {
    if (ncached == 0) {
        return nullptr;
    }

    return ptrs[--ncached];
}

/**
 * @brief 释放对象到缓存
 * @param ptr 要缓存的指针
 * @return bool 是否成功缓存，缓存满返回 false
 */
bool cache_bin::deallocate(void* ptr) {
    if (ncached == TCACHE_SIZE) {
        return false;
    }
    ptrs[ncached++] = ptr;
    return true;
}

/**
 * @brief 批量从缓存分配对象
 * @param ptrs 批量缓存的指针数组
 * @param n 要批量缓存的指针数量
 * @return int 缓存成功数量
 */
int cache_bin::allocate_batch(void** ptrs, int n) {
    int count = 0;
    for (int i = 0; i < n; ++i) {
        void* ptr = allocate();
        if (ptr == nullptr) {
            break;
        }
        ptrs[i] = ptr;
        ++count;
    }
    return count;
}

/**
 * @brief 批量释放对象到缓存
 * @param ptrs 批量缓存的指针数组
 * @param n 要批量缓存的指针数量
 * @return int 缓存成功数量
 */
void cache_bin::deallocate_batch(void** ptrs, int n) {
    for (int i = 0; i < n && ncached < TCACHE_SIZE; ++i) {
        if (ptrs[i] != nullptr) {
            ptrs[ncached++] = ptrs[i];
        }
    }
}

/**
 * @brief 获取需要填充的数量
 * @return std::size_t 如果需要填充缓存则返回填充数量，否则返回 0
 */
std::size_t cache_bin::need_fill() const {
    return TCACHE_FLUSH_BATCH - ncached;
}

/**
 * @brief 检查缓存是否为空
 * @return bool 如果缓存为空则返回 true
 */
bool cache_bin::empty() const {
    return ncached == 0;
}

/**
 * @brief 检查是否需要填充缓存
 * @return std::size_t 如果需要填充缓存则返回填充数量，否则返回 0
 */
bool cache_bin::is_fill() const {
    return ncached < TCACHE_FLUSH_BATCH;
}

/**
 * @brief 检查缓存是否已满
 * @return bool 如果缓存已满则返回 true
 */
bool cache_bin::full() const {
    return ncached == TCACHE_SIZE;
}

}  // namespace thread_cache
}  // namespace cascading
