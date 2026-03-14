namespace cascading {
namespace arena {

/**
 * @brief 默认构造函数
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::chunk()
    : slab_group_(), data_region_base_(nullptr), initialized_(false) {}

/**
 * @brief 析构函数
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::~chunk() {}

/**
 * @brief 初始化 chunk
 * @param data_region 4MB 数据区域基地址
 * @return 是否初始化成功
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::initialize(
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

    // 初始化 slab_group
    bool result = slab_group_.initialize(data_region);
    if (!result) {
        initialized_.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

/**
 * @brief 分配一个对象
 * @return void* 指向分配的对象的指针，失败返回 nullptr
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
void* chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::allocate() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return nullptr;
    }

    return slab_group_.allocate();
}

/**
 * @brief 释放一个对象
 * @param ptr 指向要释放的对象的指针
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
void chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::deallocate(
    void* ptr) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return;
    }

    slab_group_.deallocate(ptr);
}

/**
 * @brief 批量分配对象
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @return int 实际分配的数量
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
int chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::allocate_batch(
    void** ptrs,
    int n) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0) {
        return 0;
    }

    return slab_group_.allocate_batch(ptrs, n);
}

/**
 * @brief 批量释放对象
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
void chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::deallocate_batch(
    void** ptrs,
    int n) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0) {
        return;
    }

    slab_group_.deallocate_batch(ptrs, n);
}

/**
 * @brief 检查 chunk 是否包含指定指针
 * @param ptr 要检查的指针
 * @return true 如果指针属于本 chunk
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::contains(
    void* ptr) const {
    if (ptr == nullptr || data_region_base_ == nullptr) {
        return false;
    }

    std::uintptr_t ptr_addr = reinterpret_cast<std::uintptr_t>(ptr);
    std::uintptr_t start = reinterpret_cast<std::uintptr_t>(data_region_base_);
    std::uintptr_t end = start + ChunkSize;

    return ptr_addr >= start && ptr_addr < end;
}

/**
 * @brief 检查 chunk 是否为空
 * @return true 如果 slab_group 为空
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::empty() const {
    if (!initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    return slab_group_.empty();
}

/**
 * @brief 检查 chunk 是否已满
 * @return true 如果 slab_group 已满
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool chunk<ObjectSize, ChunkSize, SlabSize, SlabsPerGroup>::full() const {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    return slab_group_.full();
}

}  // namespace arena
}  // namespace cascading
