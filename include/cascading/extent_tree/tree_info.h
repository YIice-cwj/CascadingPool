#pragma once
#include <atomic>

namespace cascading {
namespace extent_tree {

class tree_info {
   private:
    std::atomic<std::size_t> byte_total_{0};           // 树中所有页的字节数总和
    std::atomic<std::size_t> alloc_total_{0};         // 分配字节数
    std::atomic<std::size_t> free_total_{0};           // 空闲字节数
    std::atomic<std::size_t> allocate_count_{0};       // 分配操作次数
    std::atomic<std::size_t> deallocate_count_{0};     // 释放操作次数
    std::atomic<std::size_t> insert_count_{0};         // 插入操作次数
    std::atomic<std::size_t> reclaim_count_{0};        // 回收操作次数
    std::atomic<std::size_t> hit_count_{0};            // 内存复用命中次数
    std::atomic<std::size_t> miss_count_{0};           // 内存复用未命中次数
    std::atomic<std::size_t> split_count_{0};          // extent 分割次数
    std::atomic<std::size_t> merge_count_{0};          // 峰值统计
    std::atomic<std::size_t> byte_peak_{0};            // 内存使用峰值
    std::atomic<std::size_t> alloc_peak_{0};          // 分配峰值
    std::atomic<std::size_t> allocate_fail_count_{0};  // 分配失败次数
    std::atomic<std::size_t> insert_fail_count_{0};    // 插入失败次数

   public:
    /**
     * @brief 获取树中所有页的字节数总和
     * @return 树中所有页的字节数总和
     */
    std::size_t byte_total() const {
        return byte_total_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取分配字节数
     * @return 分配字节数
     */
    std::size_t alloc_total() const {
        return alloc_total_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取空闲字节数
     * @return 空闲字节数
     */
    std::size_t free_total() const {
        return free_total_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取分配操作次数
     * @return 分配操作次数
     */
    std::size_t allocate_count() const {
        return allocate_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取释放操作次数
     * @return 释放操作次数
     */
    std::size_t deallocate_count() const {
        return deallocate_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取插入操作次数
     * @return 插入操作次数
     */
    std::size_t insert_count() const {
        return insert_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取回收操作次数
     * @return 回收操作次数
     */
    std::size_t reclaim_count() const {
        return reclaim_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取内存复用命中次数
     * @return 内存复用命中次数
     */
    std::size_t hit_count() const {
        return hit_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取内存复用未命中次数
     * @return 内存复用未命中次数
     */
    std::size_t miss_count() const {
        return miss_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取 extent 分割次数
     * @return extent 分割次数
     */
    std::size_t split_count() const {
        return split_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取 extent 合并次数
     * @return extent 合并次数
     */
    std::size_t merge_count() const {
        return merge_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取内存复用命中率（百分比）
     * @return 命中率（0-100）
     */
    double hit_rate() const {
        std::size_t hits = hit_count_.load(std::memory_order_acquire);
        std::size_t misses = miss_count_.load(std::memory_order_acquire);
        std::size_t total = hits + misses;
        return total == 0 ? 0.0 : (static_cast<double>(hits) * 100.0 / total);
    }

    /**
     * @brief 获取内存使用峰值
     * @return 内存使用峰值
     */
    std::size_t byte_peak() const {
        return byte_peak_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取分配峰值
     * @return 分配峰值
     */
    std::size_t alloc_peak() const {
        return alloc_peak_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取分配失败次数
     * @return 分配失败次数
     */
    std::size_t allocate_fail_count() const {
        return allocate_fail_count_.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取插入失败次数
     * @return 插入失败次数
     */
    std::size_t insert_fail_count() const {
        return insert_fail_count_.load(std::memory_order_acquire);
    }

    // ========== 基础内存统计修改方法 ==========

    /**
     * @brief 增加树中所有页的字节数总和
     * @param byte_total 要增加的字节数
     */
    void add_byte_total(std::size_t byte_total) {
        byte_total_.fetch_add(byte_total, std::memory_order_release);
        // 更新峰值
        std::size_t current = byte_total_.load(std::memory_order_acquire);
        std::size_t peak = byte_peak_.load(std::memory_order_acquire);
        while (current > peak && !byte_peak_.compare_exchange_weak(
                                     peak, current, std::memory_order_release,
                                     std::memory_order_acquire)) {
        }
    }

    /**
     * @brief 增加分配字节数
     * @param alloc_total 要增加的字节数
     */
    void add_alloc_total(std::size_t alloc_total) {
        alloc_total_.fetch_add(alloc_total, std::memory_order_release);
        // 更新峰值
        std::size_t current = alloc_total_.load(std::memory_order_acquire);
        std::size_t peak = alloc_peak_.load(std::memory_order_acquire);
        while (current > peak && !alloc_peak_.compare_exchange_weak(
                                     peak, current, std::memory_order_release,
                                     std::memory_order_acquire)) {
        }
    }

    /**
     * @brief 增加空闲字节数
     * @param free_total 要增加的字节数
     */
    void add_free_total(std::size_t free_total) {
        free_total_.fetch_add(free_total, std::memory_order_release);
    }

    /**
     * @brief 减少树中所有页的字节数总和
     * @param byte_total 要减少的字节数
     */
    void sub_byte_total(std::size_t byte_total) {
        byte_total_.fetch_sub(byte_total, std::memory_order_release);
    }

    /**
     * @brief 减少分配字节数
     * @param alloc_total 要减少的字节数
     */
    void sub_alloc_total(std::size_t alloc_total) {
        alloc_total_.fetch_sub(alloc_total, std::memory_order_release);
    }

    /**
     * @brief 减少空闲字节数
     * @param free_total 要减少的字节数
     */
    void sub_free_total(std::size_t free_total) {
        free_total_.fetch_sub(free_total, std::memory_order_release);
    }

    // ========== 操作次数统计增加方法 ==========

    /**
     * @brief 增加分配操作次数
     */
    void inc_allocate_count() {
        allocate_count_.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief 增加释放操作次数
     */
    void inc_deallocate_count() {
        deallocate_count_.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief 增加插入操作次数
     */
    void inc_insert_count() {
        insert_count_.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief 增加回收操作次数
     */
    void inc_reclaim_count() {
        reclaim_count_.fetch_add(1, std::memory_order_release);
    }

    // ========== 内存效率统计增加方法 ==========

    /**
     * @brief 增加内存复用命中次数
     */
    void inc_hit_count() { hit_count_.fetch_add(1, std::memory_order_release); }

    /**
     * @brief 增加内存复用未命中次数
     */
    void inc_miss_count() {
        miss_count_.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief 增加 extent 分割次数
     */
    void inc_split_count() {
        split_count_.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief 增加 extent 合并次数
     */
    void inc_merge_count() {
        merge_count_.fetch_add(1, std::memory_order_release);
    }

    // ========== 错误统计增加方法 ==========

    /**
     * @brief 增加分配失败次数
     */
    void inc_allocate_fail_count() {
        allocate_fail_count_.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief 增加插入失败次数
     */
    void inc_insert_fail_count() {
        insert_fail_count_.fetch_add(1, std::memory_order_release);
    }
};

}  // namespace extent_tree
}  // namespace cascading
