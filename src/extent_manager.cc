#include "cascading/extent_tree/extent_manager.h"
#include <algorithm>

namespace cascading {
namespace extent_tree {

/**
 * @brief 从系统分配一块内存
 * @details 从系统分配一块内存，
 * 并将其状态设置为 in_use。
 * @param size 要分配的内存大小
 * @return void* 指向分配内存的指针
 */
void* extent_manager::allocate_system(std::size_t size) {
    if (size == 0) {
        return nullptr;
    }
    size = align_to_page(size);
    void* addr = allocate_memory(size);
    if (addr != nullptr) {
        in_use_tree_->insert(
            std::make_unique<extent>(addr, size, extent::state_page::dirty));
        return addr;
    }
    return nullptr;
}

/**
 * @brief 回收脏页
 * @details 回收 dirty_tree_ 中过期的脏页。
 * @param expire_time 过期时间戳
 */
void extent_manager::dirty_reclaim(std::size_t expire_time) {
    dirty_tree_->merge();
    reclaim_extents_ = std::move(dirty_tree_->reclaim(expire_time));
    for (auto& ext : reclaim_extents_) {
        muzzy_tree_->insert(std::move(ext));
    }
}

/**
 * @brief 回收过期模糊页
 * @details 从 muzzy_tree 类中回收所有过期时间小于等于 expire_time
 * 的模糊页。 只有完全合并成完整的页才能回收。
 * @param expire_time 过期时间戳
 */
void extent_manager::muzzy_reclaim(std::size_t expire_time) {
    reclaim_extents_ = std::move(muzzy_tree_->reclaim(expire_time));
    for (auto& ext : reclaim_extents_) {
        retained_tree_->insert(std::move(ext));
    }
}

/**
 * @brief 根据内存压力更新过期时间
 * @details 根据当前内存压力，动态调整 dirty_tree_、muzzy_tree_、retained_tree_
 * 中的过期时间。
 *
 * 调整策略：
 * - 内存压力低（0.0 ~ 0.3）：延长过期时间，减少回收频率，提高性能
 * - 内存压力中（0.3 ~ 0.7）：使用默认过期时间
 * - 内存压力高（0.7 ~ 1.0）：缩短过期时间，加快回收频率，释放内存
 *
 * 计算公式：
 * - 高压时：expire_time = base_interval / (1 + pressure * 2)
 * - 低压时：expire_time = base_interval * (1.5 - pressure)
 */
void extent_manager::update_expire_time() {
    double memory_pressure = get_memory_pressure();

    std::size_t dirty_expire_time;
    std::size_t muzzy_expire_time;
    std::size_t retained_expire_time;

    if (memory_pressure >= 0.7) {
        // 高内存压力：缩短过期时间，加快回收
        // 压力越高，过期时间越短
        double scale_factor = 1.0 + memory_pressure * 3.0;  // 1.0 ~ 4.0

        dirty_expire_time =
            static_cast<std::size_t>(DIRTY_RECLAIM_INTERVAL / scale_factor);
        muzzy_expire_time =
            static_cast<std::size_t>(MUZZY_RECLAIM_INTERVAL / scale_factor);
        retained_expire_time =
            static_cast<std::size_t>(RETAINED_RECLAIM_INTERVAL / scale_factor);

        // 确保最小过期时间不小于100ms，避免过于频繁的回收
        dirty_expire_time =
            std::max(dirty_expire_time, static_cast<std::size_t>(5000));
        muzzy_expire_time =
            std::max(muzzy_expire_time, static_cast<std::size_t>(10000));
        retained_expire_time =
            std::max(retained_expire_time, static_cast<std::size_t>(20000));

    } else if (memory_pressure <= 0.3) {
        // 低内存压力：延长过期时间，减少回收开销
        // 压力越低，过期时间越长
        double scale_factor = 1.5 - memory_pressure;  // 1.2 ~ 1.5

        dirty_expire_time =
            static_cast<std::size_t>(DIRTY_RECLAIM_INTERVAL * scale_factor);
        muzzy_expire_time =
            static_cast<std::size_t>(MUZZY_RECLAIM_INTERVAL * scale_factor);
        retained_expire_time =
            static_cast<std::size_t>(RETAINED_RECLAIM_INTERVAL * scale_factor);

    } else {
        // 中等内存压力：使用默认过期时间
        // 在默认基础上做轻微调整
        double scale_factor = 1.2 - (memory_pressure - 0.3) * 0.5;  // 1.2 ~ 1.0

        dirty_expire_time =
            static_cast<std::size_t>(DIRTY_RECLAIM_INTERVAL * scale_factor);
        muzzy_expire_time =
            static_cast<std::size_t>(MUZZY_RECLAIM_INTERVAL * scale_factor);
        retained_expire_time =
            static_cast<std::size_t>(RETAINED_RECLAIM_INTERVAL * scale_factor);
    }

    // 应用新的过期时间
    dirty_tree_->set_expire_time(dirty_expire_time);
    muzzy_tree_->set_expire_time(muzzy_expire_time);
    retained_tree_->set_expire_time(retained_expire_time);
}

/**
 * @brief 预分配内存块到 dirty_tree
 * @details 初始化时预分配常用大小的内存块（4KB~32KB），
 * 避免运行时系统调用开销。预分配的内存块按指数增长分布，
 * 覆盖常见的中等大小分配需求。
 */
void extent_manager::preallocate_medium_blocks() {
    std::size_t sizes[] = {
        4 * 1024,
        8 * 1024,
        16 * 1024,
        32 * 1024,
    };

    for (std::size_t size : sizes) {
        for (std::size_t i = 0; i < PREALLOC_COUNT_PER_SIZE; ++i) {
            void* addr = allocate_memory(size);
            if (addr != nullptr) {
                dirty_tree_->insert(std::make_unique<extent>(
                    addr, size, extent::state_page::dirty));
            }
        }
    }
}

/**
 * @brief 构造函数
 * @details 创建一个 extent_manager
 * 类的实例，初始化脏页树、模糊页树、保留页树，
 * 并预分配中等大小的内存块。
 */
extent_manager::extent_manager()
    : in_use_tree_(std::make_unique<in_use_tree>()),
      dirty_tree_(std::make_unique<dirty_tree>(DIRTY_RECLAIM_INTERVAL)),
      muzzy_tree_(std::make_unique<muzzy_tree>(MUZZY_RECLAIM_INTERVAL)),
      retained_tree_(
          std::make_unique<retained_tree>(RETAINED_RECLAIM_INTERVAL)),
      reclaim_extents_(),
      reclaim_recycler_task_() {
    preallocate_medium_blocks();
    reclaim_recycler_task_.set_interval(RECLAIM_RECYCLER_INTERVAL);
    reclaim_recycler_task_.set_task([this]() { reclaim(); });
    reclaim_recycler_task_.start();
}

/**
 * @brief 析构函数
 * @details 清空脏页树、模糊页树、保留页树。
 */
extent_manager::~extent_manager() {
    in_use_tree_->clear();
    dirty_tree_->clear();
    muzzy_tree_->clear();
    retained_tree_->clear();
    reclaim_recycler_task_.stop();
}

/**
 * @brief 分配一块内存
 * @details 从 extent_manager 中分配一块内存，
 * 并将其标记为已使用。小于一页（4KB）的分配会对齐到一页。
 * @return void* 指向分配内存的指针
 */
void* extent_manager::allocate(std::size_t size) {
    if (size == 0) {
        return nullptr;
    }

    extent::unique_ptr result = nullptr;

    // 超过最大块大小的分配直接走系统分配
    if (size > MAX_BLOCK_SIZE) {
        void* addr = allocate_system(size);
        if (addr != nullptr) {
            return addr;
        }
    }

    // 对齐到页大小（4KB），确保树中查找和插入的一致性
    std::size_t aligned_size = align_to_page(size);

    result = dirty_tree_->allocate(aligned_size);
    if (result != nullptr) {
        void* addr = result->addr;
        in_use_tree_->insert(std::move(result));
        return addr;
    }

    result = muzzy_tree_->allocate(aligned_size);
    if (result != nullptr) {
        void* addr = result->addr;
        in_use_tree_->insert(std::move(result));
        return addr;
    }

    result = retained_tree_->allocate(aligned_size);
    if (result != nullptr) {
        void* addr = result->addr;
        in_use_tree_->insert(std::move(result));
        return addr;
    }

    void* addr = allocate_system(size);
    if (addr != nullptr) {
        return addr;
    }

    return nullptr;
}

/**
 * @brief
 * @brief 释放一块内存
 * @details 释放 extent_manager 中指向 addr 的内存，
 * 并将其标记为未使用。
 * @param addr 要释放的内存地址
 * @param size 要释放的内存大小
 */
void extent_manager::deallocate(void* addr, std::size_t size) {
    if (addr == nullptr) {
        return;
    }

    if (!in_use_tree_->contains(addr)) {
        return;
    }

    extent::unique_ptr ext = in_use_tree_->remove(addr);
    if (ext == nullptr || ext->size != size) {
        return;
    }

    if (ext->size >= MAX_BLOCK_SIZE) {
        release_memory(ext->addr, ext->size);
        return;
    }

    dirty_tree_->insert(std::move(ext));
}

/**
 * @brief 回收过期内存
 * @details 回收 dirty_tree_、 muzzy_tree_、 retained_tree_ 中过期的内存。
 */
void extent_manager::reclaim() {
    update_expire_time();
    std::size_t current_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    dirty_reclaim(current_time);
    muzzy_reclaim(current_time);
    retained_tree_->reclaim(current_time);
}

/**
 * @brief 获取内存压力
 * @details 获取当前内存压力，基于脏页树、模糊页树、保留页树中的内存总量。
 * 内存压力值范围在 0.0 ~ 1.0 之间：
 * - 0.0 表示没有内存压力（没有缓存的内存）
 * - 1.0 表示内存压力极高（缓存内存达到阈值）
 * @return double 内存压力值（0~1之间的浮点数）
 */
double extent_manager::get_memory_pressure() {
    // 计算所有缓存树中的总字节数
    std::size_t cached_bytes = dirty_tree_->info().byte_total() +
                               muzzy_tree_->info().byte_total() +
                               retained_tree_->info().byte_total();

    // 获取 in_use 树中的内存使用量
    std::size_t in_use_bytes = in_use_tree_->info().byte_total();

    // 总内存使用量 = 正在使用的 + 缓存的
    std::size_t total_bytes = in_use_bytes + cached_bytes;

    // 如果没有内存使用，返回 0
    if (total_bytes == 0) {
        return 0.0;
    }

    double pressure =
        static_cast<double>(cached_bytes) / static_cast<double>(total_bytes);

    // 确保返回值在 0.0 ~ 1.0 范围内
    if (pressure < 0.0)
        pressure = 0.0;
    if (pressure > 1.0)
        pressure = 1.0;

    return pressure;
}

/**
 * @brief 获取单例实例
 * @details 获取 extent_manager 类的单例实例。
 * @return extent_manager& 单例实例引用
 */
extent_manager& extent_manager::get_instance() {
    static extent_manager instance;
    return instance;
}

/**
 * @brief 从 extent_manager 中分配一块内存
 * @details 从 extent_manager 中分配一块内存，
 * 并将其标记为已使用。
 * @param size 要分配的内存大小
 * @return void* 指向分配内存的指针
 */
void* extent_manager::static_allocate(std::size_t size) {
    return get_instance().allocate(size);
}

/**
 * @brief 释放一块内存
 * @details 释放 extent_manager 中指向 addr 的内存，
 * 并将其标记为未使用。
 * @param addr 要释放的内存地址
 * @param size 要释放的内存大小
 */
void extent_manager::static_deallocate(void* addr, std::size_t size) {
    get_instance().deallocate(addr, size);
}

/**
 * @brief 静态回收内存
 * @details 通过单例实例回收内存的便捷静态方法
 */
void extent_manager::static_reclaim() {
    get_instance().reclaim();
}

}  // namespace extent_tree
}  // namespace cascading
