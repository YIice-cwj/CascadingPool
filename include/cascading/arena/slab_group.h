#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "cascading/arena/slab.h"
#include "cascading/utils/lock_free_pool.h"

namespace cascading {
namespace arena {

/**
 * @brief slab_group 元数据管理类
 * @details 元数据与数据分离设计：
 *          - slab_group 只管理 slab 元数据（bitmap、计数器等）
 *          - 实际数据区域存储在 chunk 的 4MB 内存中
 *          - 每个 slab_group 管理多个 slab，每个 slab 对应 chunk
 * 中的一段数据区域
 * @tparam ObjectSize 每个对象的大小
 * @tparam SlabSize 每个 slab 的数据区域大小，默认 256KB
 * @tparam SlabCount 每个 slab_group 管理的 slab 数量
 */
template <std::size_t ObjectSize,
          std::size_t SlabSize = 256 * 1024,
          std::size_t SlabCount = 16>
class slab_group {
   public:
    // 每个 slab_group 包含的所有 slab 数据区域总大小
    constexpr static std::size_t TOTAL_DATA_SIZE = SlabSize * SlabCount;
    using slab_t = slab<ObjectSize>;
    // slab 无锁池 - 管理 slab 元数据，支持多线程并发获取
    using slab_pool_t = utils::
        lock_free_pool<slab_t, SlabCount, utils::pool_strategy::ROUND_ROBIN>;

   private:
    // slab 无锁池 - 存储在 chunk 外的单独内存
    alignas(64) mutable slab_pool_t slab_pool_;
    // 已初始化的 slab 数量（原子操作保证线程安全）
    alignas(64) std::atomic<std::size_t> initialized_count_;
    // 数据区域基地址
    void* data_region_base_{nullptr};
    // 是否已初始化
    alignas(64) std::atomic<bool> initialized_{false};

   public:
    /**
     * @brief 构造函数
     */
    slab_group();

    /**
     * @brief 析构函数
     */
    ~slab_group();

    /**
     * @brief 初始化 slab_group
     * @details 初始化所有 slab 元数据，将数据区域分配给各个 slab
     * @param data_region 指向 slab_group 数据区域的指针
     * @return 是否初始化成功
     */
    bool initialize(void* data_region);

    /**
     * @brief 分配一个对象
     * @return void* 指向分配的对象的指针，失败返回 nullptr
     */
    void* allocate();

    /**
     * @brief 释放一个对象
     * @param ptr 指向要释放的对象的指针
     */
    void deallocate(void* ptr);

    /**
     * @brief 批量分配对象
     * @param ptrs 输出数组，存储分配的指针
     * @param n 请求分配的数量
     * @return int 实际分配的数量
     */
    int allocate_batch(void** ptrs, int n);

    /**
     * @brief 批量释放对象
     * @param ptrs 指向要释放的对象指针数组
     * @param n 释放的数量
     */
    void deallocate_batch(void** ptrs, int n);

    /**
     * @brief 检查 slab_group 是否为空
     * @return true 如果所有 slab 都为空
     */
    bool empty() const;

    /**
     * @brief 检查 slab_group 是否已满
     * @return true 如果所有 slab 都已满
     */
    bool full() const;

    /**
     * @brief 检查是否已初始化
     * @return true 如果已初始化
     */
    bool is_initialized() const {
        return initialized_.load(std::memory_order_acquire);
    }

    /**
     * @brief 检查 slab_group 是否包含指定指针
     * @param ptr 要检查的指针
     * @return true 如果指针属于本 slab_group
     */
    bool contains(void* ptr) const;
};

}  // namespace arena
}  // namespace cascading
