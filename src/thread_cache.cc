#include "cascading/thread_cache/thread_cache.h"

namespace cascading {
namespace thread_cache {

/**
 * @brief 从arena批量填充缓存
 * @details 遍历所有大小类，如果缓存槽数超过刷新阈值，则批量填充缓存槽
 */
void thread_cache::fill_cache(std::size_t class_index, int n) {
    if (n == 0) {
        return;
    }
    arena_.allocate_batch(bins_[class_index].ptrs + bins_[class_index].ncached,
                          n);
    bins_[class_index].ncached += n;
}

/**
 * @brief 刷新指定大小类的缓存到 arena
 * @param class_index 大小类索引
 * @param n 刷新的数量，0 表示全部
 */
void thread_cache::flush_arena(std::size_t class_index, int n) {
    if (n == 0) {
        n = bins_[class_index].ncached;
    }
    arena_.deallocate_batch(bins_[class_index].ptrs, n);
    bins_[class_index].ncached -= n;
}

/**
 * @brief 构造函数
 */
thread_cache::thread_cache() : bins_(), arena_(arena::arena::get_instance()) {
    for (std::size_t i = 0; i < arena::size_class_table::CLASS_COUNT; ++i) {
        bins_[i].ncached =
            arena_.allocate_batch_by_class(bins_[i].ptrs, TCACHE_SIZE, i);
    }
}

/**
 * @brief 析构函数
 * @details 销毁时将所有缓存刷新到 arena
 */
thread_cache::~thread_cache() {
    int size = static_cast<int>(bins_.size());
    for (int i = 0; i < size; ++i) {
        flush_arena(i, 0);
    }
}

/**
 * @brief 分配一个对象
 * @param size 要分配的对象大小
 * @return void* 指向分配的对象的指针，失败返回 nullptr
 */
void* thread_cache::allocate(std::size_t size) {
    if (size == 0) {
        return nullptr;
    }

    std::size_t class_index = arena::size_class_table::size_to_class(size);
    if (class_index >= arena::size_class_table::CLASS_COUNT) {
        return arena_.allocate(size);
    }

    void* ptr = bins_[class_index].allocate();

    if (bins_[class_index].is_fill()) {
        fill_cache(class_index, bins_[class_index].need_fill());
    }

    return ptr;
}

/**
 * @brief 释放一个对象
 * @param ptr 指向要释放的对象的指针
 * @param size 对象大小（用于计算大小类）
 */
void thread_cache::deallocate(void* ptr, std::size_t size) {
    if (ptr == nullptr || size == 0) {
        return;
    }

    std::size_t class_index = arena::size_class_table::size_to_class(size);
    if (class_index >= arena::size_class_table::CLASS_COUNT) {
        arena_.deallocate(ptr, size);
        return;
    }

    if (bins_[class_index].full()) {
        arena_.deallocate(ptr, size);
        return;
    }

    bins_[class_index].deallocate(ptr);
}

/**
 * @brief 批量分配对象
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @param size 每个对象的大小
 * @return int 实际分配的数量
 */
int thread_cache::allocate_batch(void** ptrs, int n, std::size_t size) {
    if (ptrs == nullptr || n == 0 || size == 0) {
        return 0;
    }

    std::size_t class_index = arena::size_class_table::size_to_class(size);
    if (class_index >= arena::size_class_table::CLASS_COUNT) {
        return arena_.allocate_batch(ptrs, n, size);
    }

    int allocated = bins_[class_index].allocate_batch(ptrs, n);
    if (allocated != n) {
        allocated +=
            arena_.allocate_batch(ptrs + allocated, n - allocated, size);
    }

    if (bins_[class_index].is_fill()) {
        fill_cache(class_index, bins_[class_index].need_fill());
    }
    return allocated;
}
/**
 * @brief 批量释放对象
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 * @param size 每个对象的大小
 * @return int 实际释放的数量
 */
void thread_cache::deallocate_batch(void** ptrs, int n, std::size_t size) {
    if (ptrs == nullptr || n == 0 || size == 0) {
        return;
    }

    std::size_t class_index = arena::size_class_table::size_to_class(size);
    if (class_index >= arena::size_class_table::CLASS_COUNT) {
        arena_.deallocate_batch(ptrs, n, size);
        return;
    }
    if (bins_[class_index].full()) {
        arena_.deallocate_batch(ptrs, n, size);
        return;
    }

    if (bins_[class_index].need_fill() >= static_cast<std::size_t>(n)) {
        bins_[class_index].deallocate_batch(ptrs, n);
    } else {
        arena_.deallocate_batch(ptrs, n, size);
    }
}

}  // namespace thread_cache
}  // namespace cascading