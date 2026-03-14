#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "cascading/thread_cache/thread_cache.h"

namespace cascading {
namespace thread_cache {

/**
 * @brief 线程缓存管理器 - 单例模式
 * @details 管理所有线程的 thread_cache 实例
 *          使用线程本地存储(TLS)为每个线程提供独立的缓存
 *          负责线程缓存的生命周期管理
 */
class thread_cache_manager {
   private:
    // 线程本地存储的 thread_cache 实例
    static thread_local thread_cache* tls_cache_;
    // 标记 TLS 是否已初始化
    static thread_local bool tls_initialized_;
    // 保护 all_caches_ 的互斥锁
    mutable std::mutex mutex_;
    // 所有创建的 thread_cache 实例（用于清理）
    std::unordered_map<std::thread::id, thread_cache*> all_caches_;

    // 是否已关闭（防止析构后继续分配）
    std::atomic<bool> shutdown_{false};

   private:
    /**
     * @brief 私有构造函数
     */
    thread_cache_manager() = default;

    /**
     * @brief 禁止拷贝构造
     */
    thread_cache_manager(const thread_cache_manager&) = delete;
    /**
     * @brief 禁止拷贝赋值
     */
    thread_cache_manager& operator=(const thread_cache_manager&) = delete;
    /**
     * @brief 禁止移动构造
     */
    thread_cache_manager(thread_cache_manager&&) = delete;
    /**
     * @brief 禁止移动赋值
     */
    thread_cache_manager& operator=(thread_cache_manager&&) = delete;

    /**
     * @brief 获取或创建当前线程的缓存
     * @return thread_cache* 当前线程的缓存实例
     */
    thread_cache* get_or_create_cache();

   public:
    /**
     * @brief 析构函数
     * @details 清理所有线程缓存
     */
    ~thread_cache_manager();

    /**
     * @brief 获取管理器单例实例
     * @return thread_cache_manager& 管理器实例引用
     */
    static thread_cache_manager& get_instance();

    /**
     * @brief 分配内存
     * @param size 要分配的字节数
     * @return void* 分配的内存指针，失败返回 nullptr
     */
    void* allocate(std::size_t size);

    /**
     * @brief 释放内存
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
     * @brief 获取当前线程的缓存实例
     * @return thread_cache* 当前线程的缓存指针，可能为 nullptr
     */
    thread_cache* get_current_cache() const;

    /**
     * @brief 清理指定线程的缓存
     * @param thread_id 线程 ID
     */
    void cleanup_thread_cache(std::thread::id thread_id);

    /**
     * @brief 清理当前线程的缓存
     */
    void cleanup_current_thread();

    /**
     * @brief 关闭管理器
     * @details 清理所有缓存，之后分配操作将失败
     */
    void shutdown();

    /**
     * @brief 获取活跃线程缓存数量
     * @return std::size_t 缓存数量
     */
    std::size_t get_cache_count() const;

    /**
     * @brief 检查是否已关闭
     * @return bool 如果已关闭返回 true
     */
    bool is_shutdown() const { return shutdown_.load(); }
};

}  // namespace thread_cache
}  // namespace cascading