#pragma once
#include <array>
#include <cstddef>
#include "cascading/arena/data_region.h"
#include "cascading/utils/compiler.h"

namespace cascading {
namespace arena {

/**
 * @brief Slab 元数据 - 不包含实际数据区域
 * @details 元数据与数据分离设计：
 *          - 元数据（bitmap、计数器等）存储在 chunk 外的单独内存
 *          - 实际数据区域存储在 chunk 的 4MB 内存中
 * @tparam ObjectSize 每个对象的大小
 * @tparam SlabSize slab 总大小，默认 256KB
 */
template <std::size_t ObjectSize, std::size_t SlabSize = 256 * 1024>
class slab {
   public:
    // 每个对象的大小
    static constexpr std::size_t OBJECT_SIZE = ObjectSize;
    // 每个 slab 包含的区域数量
    static constexpr std::size_t REGION_COUNT = SlabSize / ObjectSize;
    // 每个 word 表示 64 个区域的分配状态
    static constexpr std::size_t BITS_PER_WORD = 64;
    // 分配的区域数量需要多少个 word 表示
    static constexpr std::size_t BITMAP_WORDS =
        (REGION_COUNT + BITS_PER_WORD - 1) / BITS_PER_WORD;
    // 每个 slab 包含多个数据区域
    using data_region_t = data_region<OBJECT_SIZE>;

   private:
    // 位图 - 每个 bit 表示一个区域的分配状态：0=空闲, 1=已分配
    alignas(64) std::array<std::atomic<std::uint64_t>, BITMAP_WORDS> bitmap_;
    // 下一个搜索起始位置（优化分配速度）
    std::uint16_t next_search_start_{0};
    // 已分配计数
    std::uint16_t allocated_count_{0};
    // 指向实际数据区域的指针（在 chunk 的 4MB 内存中）
    data_region_t* data_region_{nullptr};

   public:
    /**
     * @brief 默认构造函数
     */
    slab();

    /**
     * @brief 初始化 slab 元数据
     * @param data_region 指向实际数据区域的指针（chunk 内存中）
     * @details 清零位图，重置计数器，分配数据区域
     */
    void initialize(void* data_region);

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
     * @brief 检查 slab 是否包含指定指针
     * @param ptr 要检查的指针
     * @return true 如果指针属于本 slab
     */
    bool contains(void* ptr) const;

    /**
     * @brief 检查 slab 是否为空（无可分配区域）
     * @return true 如果没有空闲区域
     */
    bool empty() const;

    /**
     * @brief 检查 slab 是否已满（所有区域都已分配）
     * @return true 如果所有区域都已分配
     */
    bool full() const;

    /**
     * @brief 获取已分配数量
     * @return 已分配的区域数量
     */
    std::uint16_t allocated() const;

    /**
     * @brief 获取空闲数量
     * @return 空闲的区域数量
     */
    std::uint16_t available() const;

    /**
     * @brief 获取位图大小（字节）
     * @return 位图占用的字节数
     */
    static constexpr std::size_t bitmap_size();
};

}  // namespace arena
}  // namespace cascading
