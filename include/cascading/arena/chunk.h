#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "cascading/arena/slab_group.h"
#include "cascading/utils/lock_free_pool.h"

namespace cascading {
namespace arena {

/**
 * @brief Chunk 内存管理类
 * @details 管理 4MB 内存块，分配给多个 slab_group
 *          - 4MB 内存完全用于用户数据（slab 数据区域）
 *          - 元数据（slab_group、bitmap 等）存储在 chunk 外
 *          - 使用无锁池管理 slab_group，每个线程可以获取独立的 slab_group
 * 进行分配
 * @tparam ObjectSize 对象大小
 * @tparam ChunkSize chunk 大小，默认 4MB
 * @tparam SlabSize 每个 slab 的数据区域大小，默认 256KB
 * @tparam SlabsPerGroup 每个 slab_group 包含的 slab 数量，默认 16
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize = 4 * 1024 * 1024,
          std::size_t SlabSize = 256 * 1024,
          std::size_t SlabsPerGroup = 16>
class chunk {
   public:
    // 每个 slab group 的数据区域大小
    static constexpr std::size_t SLAB_GROUP_DATA_SIZE =
        SlabSize * SlabsPerGroup;
    // slab group 类型
    using slab_group_t = slab_group<ObjectSize, SlabSize, SlabsPerGroup>;

   private:
    alignas(64) slab_group_t slab_group_;
    // 4MB 数据区域基地址
    void* data_region_base_{nullptr};
    // 是否已初始化
    std::atomic<bool> initialized_{false};

   public:
    /**
     * @brief 默认构造函数
     */
    chunk();

    /**
     * @brief 析构函数
     */
    ~chunk();

    /**
     * @brief 初始化 chunk
     * @param data_region 4MB 数据区域基地址
     * @return 是否初始化成功
     */
    bool initialize(void* data_region);

    /**
     * @brief 分配一个对象（使用轮询策略）
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
     * @brief 检查 chunk 是否包含指定指针
     * @param ptr 要检查的指针
     * @return true 如果指针属于本 chunk
     */
    bool contains(void* ptr) const;

    /**
     * @brief 检查 chunk 是否为空
     * @return true 如果所有 slab_group 都为空
     */
    bool empty() const;

    /**
     * @brief 检查 chunk 是否已满
     * @return true 如果所有 slab_group 都已满
     */
    bool full() const;

    /**
     * @brief 获取数据区域基地址
     * @return void* 4MB 数据区域基地址
     */
    void* get_data_region_base() const { return data_region_base_; }

    /**
     * @brief 检查是否已初始化
     * @return true 如果已初始化
     */
    bool is_initialized() const {
        return initialized_.load(std::memory_order_acquire);
    }
};

}  // namespace arena
}  // namespace cascading
