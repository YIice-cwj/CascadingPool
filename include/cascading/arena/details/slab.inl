namespace cascading {
namespace arena {

/**
 * @brief 默认构造函数
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
slab<ObjectSize, SlabSize>::slab()
    : bitmap_(),
      next_search_start_(0),
      allocated_count_(0),
      data_region_(nullptr) {}

/**
 * @brief 初始化 slab 元数据
 * @param data_region 指向实际数据区域的指针（chunk 内存中）
 * @details 清零位图，重置计数器，分配数据区域
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
void slab<ObjectSize, SlabSize>::initialize(void* data_region) {
    for (std::size_t i = 0; i < BITMAP_WORDS; ++i) {
        bitmap_[i].store(0, std::memory_order_relaxed);
    }
    next_search_start_ = 0;
    allocated_count_ = 0;
    data_region_ = ::new (data_region) data_region_t[REGION_COUNT];
}

/**
 * @brief 分配一个对象
 * @details 在位图中查找第一个 0 bit，设置为 1，返回对应区域
 * @return void* 指向分配的对象的指针，失败返回 nullptr
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
void* slab<ObjectSize, SlabSize>::allocate() {
    if (data_region_ == nullptr) {
        return nullptr;
    }

    if (allocated_count_ >= REGION_COUNT) {
        return nullptr;
    }

    std::uint16_t start_word = next_search_start_;

    for (std::uint16_t word = 0; word < BITMAP_WORDS; ++word) {
        std::uint16_t word_index = (start_word + word) % BITMAP_WORDS;
        std::uint64_t word_value =
            bitmap_[word_index].load(std::memory_order_relaxed);

        if (word_value == ~0ULL) {
            continue;
        }

        // 查找第一个 0 bit
        std::uint16_t bit_index = find_first_zero(word_value);

        if (bit_index >= BITS_PER_WORD) {
            continue;
        }

        std::uint16_t region_index = word_index * BITS_PER_WORD + bit_index;
        if (region_index >= REGION_COUNT) {
            continue;
        }

        // 尝试原子地设置 bit 为 1（已分配）
        std::uint64_t new_value = word_value | (1ULL << bit_index);
        if (bitmap_[word_index].compare_exchange_weak(
                word_value, new_value, std::memory_order_acquire,
                std::memory_order_relaxed)) {
            allocated_count_++;
            next_search_start_ = word_index;

            // 通过数组下标获取数据区域地址
            return data_region_[region_index].data();
        }

        // CAS 失败，重试
        --word;
    }

    return nullptr;
}

/**
 * @brief 释放一个对象
 * @details 根据指针计算索引，清除对应的 bit
 * @param ptr 指向要释放的对象的指针
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
void slab<ObjectSize, SlabSize>::deallocate(void* ptr) {
    if (ptr == nullptr || data_region_ == nullptr || !contains(ptr)) {
        return;
    }

    // 计算 region_index
    std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
    std::uintptr_t base_addr = reinterpret_cast<std::uintptr_t>(data_region_);
    std::size_t offset = ptr_addr - base_addr;
    std::uint16_t region_index =
        static_cast<std::uint16_t>(offset / ObjectSize);

    if (region_index >= REGION_COUNT) {
        return;
    }

    std::uint16_t word_index = region_index / BITS_PER_WORD;
    std::uint16_t bit_index = region_index % BITS_PER_WORD;
    std::uint64_t mask = ~(1ULL << bit_index);

    bitmap_[word_index].fetch_and(mask, std::memory_order_release);

    allocated_count_--;
}

/**
 * @brief 批量分配对象
 * @details 在位图中查找多个 0 bit，批量设置为 1，返回对应区域
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @return int 实际分配的数量
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
int slab<ObjectSize, SlabSize>::allocate_batch(void** ptrs, int n) {
    if (data_region_ == nullptr || n <= 0) {
        return 0;
    }

    int allocated = 0;
    std::uint16_t start_word = next_search_start_;

    for (std::uint16_t word = 0; word < BITMAP_WORDS && allocated < n; ++word) {
        std::uint16_t word_index = (start_word + word) % BITMAP_WORDS;
        std::uint64_t word_value =
            bitmap_[word_index].load(std::memory_order_relaxed);

        if (word_value == ~0ULL) {
            continue;
        }

        // 在这个 word 中尽可能多地分配
        while (word_value != ~0ULL && allocated < n) {
            std::uint16_t bit_index = find_first_zero(word_value);

            if (bit_index >= BITS_PER_WORD) {
                break;
            }

            std::uint16_t region_index = word_index * BITS_PER_WORD + bit_index;
            if (region_index >= REGION_COUNT) {
                break;
            }

            // 尝试原子地设置 bit 为 1（已分配）
            std::uint64_t new_value = word_value | (1ULL << bit_index);
            if (bitmap_[word_index].compare_exchange_weak(
                    word_value, new_value, std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                ptrs[allocated++] = data_region_[region_index].data();
                allocated_count_++;
                next_search_start_ = word_index;
                word_value = new_value;  // 更新 word_value 继续查找
            } else {
                // CAS 失败，重新加载 word_value
                word_value =
                    bitmap_[word_index].load(std::memory_order_relaxed);
                if (word_value == ~0ULL) {
                    break;
                }
            }
        }
    }

    return allocated;
}

/**
 * @brief 批量释放对象
 * @details 根据指针数组计算索引，批量清除对应的 bit
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
void slab<ObjectSize, SlabSize>::deallocate_batch(void** ptrs, int n) {
    if (data_region_ == nullptr || ptrs == nullptr || n <= 0) {
        return;
    }

    for (int i = 0; i < n; ++i) {
        void* ptr = ptrs[i];
        if (ptr == nullptr || !contains(ptr)) {
            continue;
        }

        // 计算 region_index
        std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
        std::uintptr_t base_addr =
            reinterpret_cast<std::uintptr_t>(data_region_);
        std::size_t offset = ptr_addr - base_addr;
        std::uint16_t region_index =
            static_cast<std::uint16_t>(offset / ObjectSize);

        if (region_index >= REGION_COUNT) {
            continue;
        }

        std::uint16_t word_index = region_index / BITS_PER_WORD;
        std::uint16_t bit_index = region_index % BITS_PER_WORD;
        std::uint64_t mask = ~(1ULL << bit_index);

        bitmap_[word_index].fetch_and(mask, std::memory_order_release);
        allocated_count_--;
    }
}

/**
 * @brief 检查 slab 是否包含指定指针
 * @param ptr 要检查的指针
 * @return true 如果指针属于本 slab
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
bool slab<ObjectSize, SlabSize>::contains(void* ptr) const {
    if (ptr == nullptr || data_region_ == nullptr) {
        return false;
    }

    std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
    std::uintptr_t start = reinterpret_cast<std::uintptr_t>(data_region_);
    std::uintptr_t end = start + SlabSize;

    return ptr_addr >= start && ptr_addr < end;
}

/**
 * @brief 检查 slab 是否为空（无可分配区域）
 * @return true 如果没有空闲区域
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
bool slab<ObjectSize, SlabSize>::empty() const {
    return allocated_count_ == 0;
}

/**
 * @brief 检查 slab 是否已满（所有区域都已分配）
 * @return true 如果所有区域都已分配
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
bool slab<ObjectSize, SlabSize>::full() const {
    return allocated_count_ >= REGION_COUNT;
}

/**
 * @brief 获取已分配数量
 * @return 已分配的区域数量
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
std::uint16_t slab<ObjectSize, SlabSize>::allocated() const {
    return allocated_count_;
}

/**
 * @brief 获取空闲数量
 * @return 空闲的区域数量
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
std::uint16_t slab<ObjectSize, SlabSize>::available() const {
    return REGION_COUNT - allocated_count_;
}

/**
 * @brief 获取位图大小（字节）
 * @return 位图占用的字节数
 */
template <std::size_t ObjectSize, std::size_t SlabSize>
constexpr std::size_t slab<ObjectSize, SlabSize>::bitmap_size() {
    return BITMAP_WORDS * sizeof(std::uint64_t);
}

}  // namespace arena
}  // namespace cascading
