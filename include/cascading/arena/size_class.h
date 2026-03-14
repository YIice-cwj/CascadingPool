#pragma once
#include <array>
#include <cstddef>
#include "cascading/arena/chunk.h"
#include "cascading/extent_tree/extent_manager.h"
#include "cascading/utils/lock_free_list.h"

namespace cascading {
namespace arena {

// 前向声明
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
class size_class;

/**
 * @brief size_class 基类接口 - 使用函数指针表替代虚函数
 * @details 消除 vptr 开销，提高性能
 */
class size_class_base {
   public:
    // 函数指针类型定义
    using allocate_fn_t = void* (*)(void* self);
    using deallocate_fn_t = void (*)(void* self, void* ptr);
    using allocate_batch_fn_t = int (*)(void* self, void** ptrs, int n);
    using deallocate_batch_fn_t = void (*)(void* self, void** ptrs, int n);
    using get_object_size_fn_t = std::size_t (*)(void* self);
    using initialize_fn_t = bool (*)(void* self);
    using empty_fn_t = bool (*)(void* self);
    using full_fn_t = bool (*)(void* self);
    using contains_fn_t = bool (*)(void* self, void* ptr);

    // 函数指针表
    allocate_fn_t allocate_fn;
    deallocate_fn_t deallocate_fn;
    allocate_batch_fn_t allocate_batch_fn;
    deallocate_batch_fn_t deallocate_batch_fn;
    get_object_size_fn_t get_object_size_fn;
    initialize_fn_t initialize_fn;
    empty_fn_t empty_fn;
    full_fn_t full_fn;
    contains_fn_t contains_fn;

    // 指向具体 size_class 实例的指针
    void* self;

    // 内联包装函数，提供类似虚函数的调用接口
    void* allocate() { return allocate_fn(self); }
    void deallocate(void* ptr) { deallocate_fn(self, ptr); }
    int allocate_batch(void** ptrs, int n) {
        return allocate_batch_fn(self, ptrs, n);
    }
    void deallocate_batch(void** ptrs, int n) {
        deallocate_batch_fn(self, ptrs, n);
    }
    bool contains(void* ptr) { return contains_fn(self, ptr); }
    std::size_t get_object_size() { return get_object_size_fn(self); }
    bool initialize() { return initialize_fn(self); }
    bool empty() { return empty_fn(self); }
    bool full() { return full_fn(self); }
};

/**
 * @brief size_class 模板类
 * @details 管理特定大小的内存分配，使用无锁栈管理 chunk 对象
 * @tparam ObjectSize 对象大小
 * @tparam ChunkMaxCount chunk 最大数量
 * @tparam SlabSize 每个 slab 的大小
 * @tparam SlabsPerGroup 每个 slab_group 的 slab 数量
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize = 4 * 1024 * 1024,
          std::size_t ChunkMaxCount = 128,
          std::size_t SlabSize = 256 * 1024,
          std::size_t SlabsPerGroup = 16>
class size_class {
   public:
    constexpr static std::size_t OBJECT_SIZE = ObjectSize;

    using chunk_t = chunk<OBJECT_SIZE, ChunkSize, SlabSize, SlabsPerGroup>;

   private:
    // chunk 无锁链表 - 直接存储 chunk 对象
    utils::lock_free_list<chunk_t, ChunkMaxCount> chunk_list_;
    // 当前活跃 chunk 指针
    std::atomic<chunk_t*> active_chunk_{nullptr};
    // 当前 chunk 数量
    std::atomic<std::size_t> chunk_count_{0};
    // 是否已初始化
    std::atomic<bool> initialized_{false};

   private:
    /**
     * @brief 创建并初始化新的 chunk
     * @return chunk_t* 指向新 chunk 的指针，失败返回 nullptr
     */
    chunk_t* create_new_chunk();

   public:
    /**
     * @brief 构造函数
     */
    size_class();

    /**
     * @brief 析构函数
     */
    ~size_class();

    /**
     * @brief 初始化 size_class
     * @return 是否初始化成功
     */
    bool initialize();

    /**
     * @brief 分配内存
     * @return void* 分配的内存地址
     */
    void* allocate();

    /**
     * @brief 释放内存
     * @param ptr 要释放的内存地址
     */
    void deallocate(void* ptr);

    /**
     * @brief 批量分配内存
     * @param ptrs 输出数组，存储分配的指针
     * @param n 请求分配的数量
     * @return int 实际分配的数量
     */
    int allocate_batch(void** ptrs, int n);

    /**
     * @brief 批量释放内存
     * @param ptrs 指向要释放的对象指针数组
     * @param n 释放的数量
     */
    void deallocate_batch(void** ptrs, int n);

    /**
     * @brief 检查是否为空
     * @return true 如果没有已分配的内存
     */
    bool empty() const;

    /**
     * @brief 检查是否已满
     * @return true 如果所有 chunk 都已满
     */
    bool full() const;

    /**
     * @brief 检查是否已初始化
     * @return true 如果已初始化
     */
    bool is_initialized() const;

    /**
     * @brief 检查是否包含指定指针
     * @param ptr 要检查的指针
     * @return true 如果该指针属于此 size_class
     */
    bool contains(void* ptr) const;

    /**
     * @brief 创建函数指针表入口
     * @return size_class_base 函数指针表
     */
    size_class_base create_vtable_entry();

    /**
     * @brief 获取对象大小
     * @return std::size_t 对象大小
     */
    static constexpr std::size_t get_object_size() { return OBJECT_SIZE; }
};

}  // namespace arena
}  // namespace cascading
