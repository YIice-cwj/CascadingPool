#include "cascading/thread_cache/thread_cache_manager.h"

namespace cascading {
namespace thread_cache {

// 线程本地存储定义
thread_local thread_cache* thread_cache_manager::tls_cache_ = nullptr;
thread_local bool thread_cache_manager::tls_initialized_ = false;

/**
 * @brief 获取或创建当前线程的缓存
 * @return thread_cache* 当前线程的缓存实例
 */
thread_cache* thread_cache_manager::get_or_create_cache() {
    if (shutdown_.load()) {
        return nullptr;
    }

    if (!tls_initialized_) {
        tls_cache_ = new thread_cache();
        tls_initialized_ = true;

        std::lock_guard<std::mutex> lock(mutex_);
        all_caches_[std::this_thread::get_id()] = tls_cache_;
    }

    return tls_cache_;
}

/**
 * @brief 析构函数
 * @details 清理所有线程缓存
 */
thread_cache_manager::~thread_cache_manager() {
    shutdown();
}

/**
 * @brief 获取管理器单例实例
 * @return thread_cache_manager& 管理器实例引用
 */
thread_cache_manager& thread_cache_manager::get_instance() {
    static thread_cache_manager instance;
    return instance;
}

/**
 * @brief 分配内存
 * @param size 要分配的字节数
 * @return void* 分配的内存指针，失败返回 nullptr
 */
void* thread_cache_manager::allocate(std::size_t size) {
    if (size == 0) {
        return nullptr;
    }

    thread_cache* cache = get_or_create_cache();
    if (cache == nullptr) {
        return arena::arena::get_instance().allocate(size);
    }

    return cache->allocate(size);
}

/**
 * @brief 释放内存
 * @param ptr 要释放的内存指针
 * @param size 内存块大小
 */
void thread_cache_manager::deallocate(void* ptr, std::size_t size) {
    if (ptr == nullptr || size == 0) {
        return;
    }

    thread_cache* cache = get_or_create_cache();
    if (cache == nullptr) {
        arena::arena::get_instance().deallocate(ptr, size);
        return;
    }

    cache->deallocate(ptr, size);
}

/**
 * @brief 批量分配内存
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @param size 每个对象的大小
 * @return int 实际分配的数量
 */
int thread_cache_manager::allocate_batch(void** ptrs, int n, std::size_t size) {
    if (ptrs == nullptr || n == 0 || size == 0) {
        return 0;
    }

    thread_cache* cache = get_or_create_cache();
    if (cache == nullptr) {
        return arena::arena::get_instance().allocate_batch(ptrs, n, size);
    }

    return cache->allocate_batch(ptrs, n, size);
}

/**
 * @brief 批量释放内存
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 * @param size 每个对象的大小
 */
void thread_cache_manager::deallocate_batch(void** ptrs,
                                            int n,
                                            std::size_t size) {
    if (ptrs == nullptr || n == 0 || size == 0) {
        return;
    }

    thread_cache* cache = get_or_create_cache();
    if (cache == nullptr) {
        // 如果管理器已关闭，直接通过 arena 释放
        arena::arena::get_instance().deallocate_batch(ptrs, n, size);
        return;
    }

    cache->deallocate_batch(ptrs, n, size);
}

/**
 * @brief 获取当前线程的缓存实例
 * @return thread_cache* 当前线程的缓存指针，可能为 nullptr
 */
thread_cache* thread_cache_manager::get_current_cache() const {
    return tls_cache_;
}

/**
 * @brief 清理指定线程的缓存
 * @param thread_id 线程 ID
 */
void thread_cache_manager::cleanup_thread_cache(std::thread::id thread_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = all_caches_.find(thread_id);
    if (it != all_caches_.end()) {
        delete it->second;
        all_caches_.erase(it);
    }
}

/**
 * @brief 清理当前线程的缓存
 */
void thread_cache_manager::cleanup_current_thread() {
    std::thread::id current_id = std::this_thread::get_id();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = all_caches_.find(current_id);
        if (it != all_caches_.end()) {
            delete it->second;
            all_caches_.erase(it);
        }
    }

    tls_cache_ = nullptr;
    tls_initialized_ = false;
}

/**
 * @brief 关闭管理器
 * @details 清理所有缓存，之后分配操作将失败
 */
void thread_cache_manager::shutdown() {
    bool expected = false;
    if (!shutdown_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : all_caches_) {
        delete pair.second;
    }
    all_caches_.clear();
}

/**
 * @brief 获取活跃线程缓存数量
 * @return std::size_t 缓存数量
 */
std::size_t thread_cache_manager::get_cache_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_caches_.size();
}

}  // namespace thread_cache
}  // namespace cascading
