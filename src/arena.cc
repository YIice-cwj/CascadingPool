#include "cascading/arena/arena.h"
#include <cstring>

namespace cascading {
namespace arena {

/**
 * @brief 模板辅助函数：初始化指定对象大小的 size_class
 * @tparam ObjectSize 对象大小（字节）
 * @param storage 预分配的内存区域
 * @param vtable 函数指针表，用于类型擦除
 * @details 使用 placement new 在预分配内存上构造 size_class 对象，
 *          并设置函数指针表以实现运行时多态
 */
template <std::size_t ObjectSize>
void init_size_class_impl(void* storage, size_class_base& vtable) {
    using sc_t =
        size_class<ObjectSize, arena::CHUNK_SIZE, arena::CHUNK_MAX_COUNT,
                   arena::SLAB_SIZE, arena::SLABS_PER_GROUP>;

    // placement new 在预分配内存上构造 size_class 对象
    auto* ptr = new (storage) sc_t();

    // 初始化 size_class，分配必要的内存资源
    ptr->initialize();

    // 创建 vtable 条目，包含类型信息和函数指针
    vtable = ptr->create_vtable_entry();
}

/**
 * @brief 获取指定对象大小的初始化函数
 * @param object_size 对象大小（字节）
 * @return 初始化函数指针，如果大小不匹配则返回 nullptr
 * @details 使用跳转表（jump table）实现 O(1) 查找，避免 if-else 链
 */
arena::init_fn_t arena::get_initializer(std::size_t object_size) {
    // 使用跳转表替代 if-else 链 - 36个大小类
    static const init_fn_t init_table[CLASS_COUNT] = {
        &init_size_class_impl<8>,     // class 0: 8B
        &init_size_class_impl<16>,    // class 1: 16B
        &init_size_class_impl<24>,    // class 2: 24B
        &init_size_class_impl<32>,    // class 3: 32B
        &init_size_class_impl<40>,    // class 4: 40B
        &init_size_class_impl<48>,    // class 5: 48B
        &init_size_class_impl<56>,    // class 6: 56B
        &init_size_class_impl<64>,    // class 7: 64B
        &init_size_class_impl<72>,    // class 8: 72B
        &init_size_class_impl<80>,    // class 9: 80B
        &init_size_class_impl<88>,    // class 10: 88B
        &init_size_class_impl<96>,    // class 11: 96B
        &init_size_class_impl<104>,   // class 12: 104B
        &init_size_class_impl<112>,   // class 13: 112B
        &init_size_class_impl<120>,   // class 14: 120B
        &init_size_class_impl<128>,   // class 15: 128B
        &init_size_class_impl<136>,   // class 16: 136B
        &init_size_class_impl<160>,   // class 17: 160B
        &init_size_class_impl<192>,   // class 18: 192B
        &init_size_class_impl<224>,   // class 19: 224B
        &init_size_class_impl<256>,   // class 20: 256B
        &init_size_class_impl<296>,   // class 21: 296B
        &init_size_class_impl<352>,   // class 22: 352B
        &init_size_class_impl<416>,   // class 23: 416B
        &init_size_class_impl<496>,   // class 24: 496B
        &init_size_class_impl<592>,   // class 25: 592B
        &init_size_class_impl<704>,   // class 26: 704B
        &init_size_class_impl<840>,   // class 27: 840B
        &init_size_class_impl<1000>,  // class 28: 1000B (~1KB)
        &init_size_class_impl<1192>,  // class 29: 1192B
        &init_size_class_impl<1424>,  // class 30: 1424B
        &init_size_class_impl<1704>,  // class 31: 1704B
        &init_size_class_impl<2032>,  // class 32: 2032B (~2KB)
        &init_size_class_impl<2424>,  // class 33: 2424B
        &init_size_class_impl<2896>,  // class 34: 2896B
        &init_size_class_impl<3456>,  // class 35: 3456B (~3.4KB)
    };

    // 查找匹配的初始化函数
    for (std::size_t i = 0; i < CLASS_COUNT; ++i) {
        if (size_class_table::class_to_size(i) == object_size) {
            return init_table[i];
        }
    }
    return nullptr;
}

/**
 * @brief 初始化所有大小类
 * @details 为每个大小类分配内存并调用初始化函数
 */
void arena::initialize_all_size_classes() {
    for (std::size_t i = 0; i < CLASS_COUNT; ++i) {
        std::size_t obj_size = size_class_table::class_to_size(i);

        constexpr std::size_t max_size_class_size = 4096;
        size_class_storage_[i] = std::make_unique<char[]>(max_size_class_size);

        auto init_fn = get_initializer(obj_size);
        if (init_fn) {
            init_fn(size_class_storage_[i].get(), size_classes_[i]);
        }
    }
}

/**
 * @brief 构造函数
 * @details 初始化成员变量
 */
arena::arena() : size_classes_{}, size_class_storage_{}, initialized_(false) {
    initialize();
}

/**
 * @brief 析构函数
 * @details size_class 的析构由 unique_ptr 自动处理
 *          由于 size_class 没有复杂资源，直接释放内存即可
 */
arena::~arena() {}

/**
 * @brief 初始化内存分配器
 * @return 初始化前的状态：false 表示之前未初始化，true 表示已经初始化
 */
bool arena::initialize() {
    if (initialized_.exchange(true, std::memory_order_release)) {
        return false;
    }
    initialized_.store(true, std::memory_order_release);
    initialize_all_size_classes();
    return true;
}

/**
 * @brief 分配指定大小的内存
 * @param size 要分配的对象大小（字节）
 * @return 指向分配内存的指针，失败返回 nullptr
 * @details 根据大小选择合适的大小类进行分配，超过最大大小类时使用
 * extent_manager
 */
void* arena::allocate(std::size_t size) {
    if (!initialized_.load(std::memory_order_acquire) || size == 0) {
        return nullptr;
    }

    constexpr std::size_t max_size =
        size_class_table::class_to_size(CLASS_COUNT - 1);

    // 如果超过最大大小类，使用 extent_manager 分配大对象
    if (size > max_size) {
        return extent_tree::extent_manager::get_instance().allocate(size);
    }

    // 获取对应的大小类索引
    std::size_t class_id = size_class_table::size_to_class(size);

    // 通过函数指针表调用分配函数（类型擦除）
    size_class_base& base = size_classes_[class_id];
    return base.allocate();
}

/**
 * @brief 释放指定的内存
 * @param ptr 要释放的内存指针
 * @param size 对象大小（用于计算大小类，默认8字节）
 * @details 根据大小计算大小类，找到对应的 size_class 释放，
 *          如果指针不在该大小类中，则遍历所有大小类查找
 */
void arena::deallocate(void* ptr, std::size_t size) {
    if (!initialized_.load(std::memory_order_acquire) || ptr == nullptr) {
        return;
    }

    // 获取最大大小类的大小
    constexpr std::size_t max_size =
        size_class_table::class_to_size(CLASS_COUNT - 1);

    // 如果超过最大大小类，使用 extent_manager 释放大对象
    if (size > max_size) {
        extent_tree::extent_manager::get_instance().deallocate(ptr, size);
        return;
    }

    // 根据大小计算大小类索引
    std::size_t class_id = size_class_table::size_to_class(size);
    if (class_id < CLASS_COUNT) {
        size_class_base& base = size_classes_[class_id];
        // 检查指针是否属于该 size_class
        if (base.contains(ptr)) {
            base.deallocate(ptr);
            return;
        }
    }

    // 如果不在计算的 size_class 中，遍历所有 size_class 查找
    for (std::size_t i = 0; i < CLASS_COUNT; ++i) {
        size_class_base& base = size_classes_[i];
        if (base.contains(ptr)) {
            base.deallocate(ptr);
            return;
        }
    }

    // 如果没有在 size_class 中找到，可能是大对象，使用 extent_manager 释放
    extent_tree::extent_manager::get_instance().deallocate(ptr, size);
}

/**
 * @brief 批量分配内存（指定大小）
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @param size 每个对象的大小（默认8字节）
 * @return 实际分配的数量
 */
int arena::allocate_batch(void** ptrs, int n, std::size_t size) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0 || size == 0) {
        return 0;
    }

    int allocated = 0;
    // 获取最大大小类的大小
    constexpr std::size_t max_size =
        size_class_table::class_to_size(CLASS_COUNT - 1);

    // 如果超过最大大小类，使用 extent_manager 批量分配大对象
    if (size > max_size) {
        for (int i = 0; i < n; ++i) {
            ptrs[i] =
                extent_tree::extent_manager::get_instance().allocate(size);
            if (ptrs[i] == nullptr) {
                break;
            }
            ++allocated;
        }
    } else {
        std::size_t class_id = size_class_table::size_to_class(size);
        size_class_base& base = size_classes_[class_id];
        return base.allocate_batch(ptrs, n);
    }
    return allocated;
}

/**
 * @brief 批量释放内存（指定大小）
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 * @param size 每个对象的大小（默认8字节）
 */
void arena::deallocate_batch(void** ptrs, int n, std::size_t size) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0) {
        return;
    }

    // 获取对应的大小类索引
    std::size_t class_id = size_class_table::size_to_class(size);
    if (class_id >= CLASS_COUNT) {
        // 大对象，逐个释放
        for (int i = 0; i < n; ++i) {
            extent_tree::extent_manager::get_instance().deallocate(ptrs[i],
                                                                   size);
        }
        return;
    }

    // 批量释放到对应 size_class
    size_class_base& base = size_classes_[class_id];
    base.deallocate_batch(ptrs, n);
}

/**
 * @brief 批量分配内存（指定大小类索引）
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @param class_index 大小类索引（0-35）
 * @return 实际分配的数量
 */
int arena::allocate_batch_by_class(void** ptrs,
                                   int n,
                                   std::size_t class_index) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0 || class_index >= CLASS_COUNT) {
        return 0;
    }

    size_class_base& base = size_classes_[class_index];
    return base.allocate_batch(ptrs, n);
}

/**
 * @brief 批量释放内存（指定大小类索引）
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 * @param class_index 大小类索引（0-35）
 */
void arena::deallocate_batch_by_class(void** ptrs,
                                      int n,
                                      std::size_t class_index) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0 || class_index >= CLASS_COUNT) {
        return;
    }

    size_class_base& base = size_classes_[class_index];
    base.deallocate_batch(ptrs, n);
}

/**
 * @brief 获取 arena 单例实例
 * @return arena 实例引用
 * @details 使用 placement new 模式，单例永不析构
 *          程序退出时操作系统会回收内存
 */
arena& arena::get_instance() {
    static arena instance;
    return instance;
}

}  // namespace arena
}  // namespace cascading
