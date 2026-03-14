namespace cascading {
namespace arena {

/**
 * @brief 构造函数
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
slab_group<ObjectSize, SlabSize, SlabCount>::slab_group()
    : slab_pool_(),
      initialized_count_(0),
      data_region_base_(nullptr),
      initialized_(false) {}

/**
 * @brief 析构函数
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
slab_group<ObjectSize, SlabSize, SlabCount>::~slab_group() {}

/**
 * @brief 初始化 slab_group
 * @details 初始化所有 slab 元数据，将数据区域分配给各个 slab
 * @param data_region 指向 slab_group 数据区域的指针
 * @return 是否初始化成功
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
bool slab_group<ObjectSize, SlabSize, SlabCount>::initialize(
    void* data_region) {
    if (data_region == nullptr) {
        return false;
    }

    // 使用 CAS 确保只初始化一次
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
        // 已经被其他线程初始化
        return true;
    }

    data_region_base_ = data_region;

    // 从无锁池获取 slab 并初始化
    for (std::size_t i = 0; i < SlabCount; ++i) {
        slab_t* slab = slab_pool_.acquire();
        if (slab == nullptr) {
            initialized_.store(false, std::memory_order_release);
            return false;
        }

        // 设置数据区域地址
        void* slab_data =
            static_cast<void*>(static_cast<char*>(data_region) + i * SlabSize);
        slab->initialize(slab_data);

        // 释放回池中（现在 slab 已准备好被使用）
        slab_pool_.release(slab);
        initialized_count_.fetch_add(1, std::memory_order_relaxed);
    }

    return true;
}

/**
 * @brief 分配一个对象
 * @return void* 指向分配的对象的指针，失败返回 nullptr
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
void* slab_group<ObjectSize, SlabSize, SlabCount>::allocate() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return nullptr;
    }

    // 轮询获取 slab，如果已满则尝试下一个
    for (std::size_t i = 0; i < SlabCount; ++i) {
        slab_t* slab = slab_pool_.acquire();
        if (slab == nullptr) {
            return nullptr;
        }

        // 检查 slab 是否已满
        if (!slab->full()) {
            void* ptr = slab->allocate();
            slab_pool_.release(slab);
            if (ptr != nullptr) {
                return ptr;
            }
        }

        // slab 已满，释放并尝试下一个
        slab_pool_.release(slab);
    }

    // 所有 slab 都已满
    return nullptr;
}

/**
 * @brief 释放一个对象
 * @param ptr 指向要释放的对象的指针
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
void slab_group<ObjectSize, SlabSize, SlabCount>::deallocate(void* ptr) {
    if (ptr == nullptr || !contains(ptr)) {
        return;
    }

    // 计算指针属于哪个 slab
    std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
    std::uintptr_t base_addr =
        reinterpret_cast<std::uintptr_t>(data_region_base_);
    std::size_t offset = ptr_addr - base_addr;
    std::size_t slab_index = offset / SlabSize;

    if (slab_index >= SlabCount) {
        return;
    }

    slab_t* slabs_begin = slab_pool_.begin();
    slab_t* target_slab = slabs_begin + slab_index;

    target_slab->deallocate(ptr);
}

/**
 * @brief 批量分配对象
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @return int 实际分配的数量
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
int slab_group<ObjectSize, SlabSize, SlabCount>::allocate_batch(void** ptrs,
                                                                int n) {
    if (!initialized_.load(std::memory_order_acquire) || n <= 0) {
        return 0;
    }

    int allocated = 0;

    // 轮询获取 slab，尽可能多地分配
    for (std::size_t i = 0; i < SlabCount && allocated < n; ++i) {
        slab_t* slab = slab_pool_.acquire();
        if (slab == nullptr) {
            break;
        }

        // 从当前 slab 批量分配
        int remaining = n - allocated;
        int count = slab->allocate_batch(&ptrs[allocated], remaining);
        allocated += count;

        slab_pool_.release(slab);

        // 如果当前 slab 没有分配任何对象，继续尝试下一个
        if (count == 0) {
            continue;
        }
    }

    return allocated;
}

/**
 * @brief 批量释放对象
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
void slab_group<ObjectSize, SlabSize, SlabCount>::deallocate_batch(void** ptrs,
                                                                   int n) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0) {
        return;
    }

    // 按 slab 分组释放
    for (int i = 0; i < n; ++i) {
        void* ptr = ptrs[i];
        if (ptr == nullptr || !contains(ptr)) {
            continue;
        }

        // 计算指针属于哪个 slab
        std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
        std::uintptr_t base_addr =
            reinterpret_cast<std::uintptr_t>(data_region_base_);
        std::size_t offset = ptr_addr - base_addr;
        std::size_t slab_index = offset / SlabSize;

        if (slab_index >= SlabCount) {
            continue;
        }

        slab_t* slabs_begin = slab_pool_.begin();
        slab_t* target_slab = slabs_begin + slab_index;

        target_slab->deallocate(ptr);
    }
}

/**
 * @brief 检查 slab_group 是否为空
 * @return true 如果所有 slab 都为空
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
bool slab_group<ObjectSize, SlabSize, SlabCount>::empty() const {
    if (!initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    for (std::size_t i = 0; i < SlabCount; ++i) {
        slab_t* slab = slab_pool_.acquire();
        if (slab == nullptr) {
            break;
        }

        if (!slab->empty()) {
            slab_pool_.release(slab);
            return false;
        }

        slab_pool_.release(slab);
    }

    return true;
}

/**
 * @brief 检查 slab_group 是否已满
 * @return true 如果所有 slab 都已满
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
bool slab_group<ObjectSize, SlabSize, SlabCount>::full() const {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    // 遍历所有 slab 检查是否都已满
    for (std::size_t i = 0; i < SlabCount; ++i) {
        slab_t* slab = slab_pool_.acquire();
        if (slab == nullptr) {
            break;
        }

        if (!slab->full()) {
            slab_pool_.release(slab);
            return false;
        }

        slab_pool_.release(slab);
    }

    return true;
}

/**
 * @brief 检查 slab_group 是否包含指定指针
 * @param ptr 要检查的指针
 * @return true 如果指针属于本 slab_group
 */
template <std::size_t ObjectSize, std::size_t SlabSize, std::size_t SlabCount>
bool slab_group<ObjectSize, SlabSize, SlabCount>::contains(void* ptr) const {
    if (ptr == nullptr || data_region_base_ == nullptr) {
        return false;
    }

    std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
    std::uintptr_t base_addr =
        reinterpret_cast<std::uintptr_t>(data_region_base_);
    std::uintptr_t end_addr = base_addr + TOTAL_DATA_SIZE;

    return ptr_addr >= base_addr && ptr_addr < end_addr;
}

}  // namespace arena
}  // namespace cascading
