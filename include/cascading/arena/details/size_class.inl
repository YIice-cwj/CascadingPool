namespace cascading {
namespace arena {

/**
 * @brief 创建并初始化新的 chunk
 * @return chunk_t* 指向新 chunk 的指针，失败返回 nullptr
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
typename size_class<ObjectSize,
                    ChunkSize,
                    ChunkMaxCount,
                    SlabSize,
                    SlabsPerGroup>::chunk_t*
size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    create_new_chunk() {
    std::size_t current_count = chunk_count_.load(std::memory_order_relaxed);
    if (current_count >= ChunkMaxCount) {
        return nullptr;
    }

    // 使用 extent_manager 分配 chunk 数据区域
    void* data_region =
        extent_tree::extent_manager::get_instance().allocate(ChunkSize);
    if (data_region == nullptr) {
        return nullptr;
    }

    // 使用 emplace_back 直接构造 chunk 对象
    chunk_t* chunk = chunk_list_.emplace_back();
    if (chunk == nullptr) {
        extent_tree::extent_manager::get_instance().deallocate(data_region,
                                                               ChunkSize);
        return nullptr;
    }

    // 初始化 chunk
    if (!chunk->initialize(data_region)) {
        extent_tree::extent_manager::get_instance().deallocate(data_region,
                                                               ChunkSize);
        // 初始化失败，减少计数（chunk 对象仍在链表中但不可用）
        return nullptr;
    }

    // 增加计数
    chunk_count_.fetch_add(1, std::memory_order_relaxed);

    return chunk;
}

/**
 * @brief 构造函数
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    size_class()
    : chunk_list_(),
      active_chunk_(nullptr),
      chunk_count_(0),
      initialized_(false) {}

/**
 * @brief 析构函数
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    ~size_class() {}

/**
 * @brief 初始化 size_class
 * @return 是否初始化成功
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    initialize() {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    // 创建第一个 chunk
    chunk_t* chunk = create_new_chunk();
    if (chunk == nullptr) {
        return false;
    }

    // 设置为活跃 chunk
    active_chunk_.store(chunk, std::memory_order_relaxed);

    initialized_.store(true, std::memory_order_release);
    return true;
}

/**
 * @brief 分配内存
 * @return void* 分配的内存地址
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
void* size_class<ObjectSize,
                 ChunkSize,
                 ChunkMaxCount,
                 SlabSize,
                 SlabsPerGroup>::allocate() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return nullptr;
    }

    // 获取活跃 chunk
    chunk_t* chunk = active_chunk_.load(std::memory_order_relaxed);
    if (chunk == nullptr) {
        return nullptr;
    }

    // 尝试分配
    void* ptr = chunk->allocate();
    if (ptr != nullptr) {
        return ptr;
    }

    // 当前 chunk 已满，尝试从链表中找到未满的 chunk
    chunk_t* found_chunk =
        chunk_list_.find_if([](const chunk_t& ck) { return !ck.full(); });

    if (found_chunk != nullptr) {
        ptr = found_chunk->allocate();
        if (ptr != nullptr) {
            // 更新活跃 chunk
            active_chunk_.store(found_chunk, std::memory_order_relaxed);
            return ptr;
        }
    }

    // 所有 chunk 都满，尝试创建新 chunk
    chunk_t* new_chunk = create_new_chunk();
    if (new_chunk != nullptr) {
        active_chunk_.store(new_chunk, std::memory_order_relaxed);
        ptr = new_chunk->allocate();
        if (ptr != nullptr) {
            return ptr;
        }
    }

    return nullptr;
}

/**
 * @brief 释放内存
 * @param ptr 要释放的内存地址
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
void size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    deallocate(void* ptr) {
    if (ptr == nullptr || !initialized_.load(std::memory_order_acquire)) {
        return;
    }

    // 查找包含该指针的 chunk 并释放
    chunk_t* chunk = chunk_list_.find_if(
        [ptr](const chunk_t& ck) { return ck.contains(ptr); });

    if (chunk != nullptr) {
        chunk->deallocate(ptr);
    }
}

/**
 * @brief 批量分配内存
 * @param ptrs 输出数组，存储分配的指针
 * @param n 请求分配的数量
 * @return int 实际分配的数量
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
int size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    allocate_batch(void** ptrs, int n) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0) {
        return 0;
    }

    int allocated = 0;
    chunk_t* current_chunk = active_chunk_.load(std::memory_order_relaxed);

    // 首先尝试从活跃 chunk 分配
    if (current_chunk != nullptr && !current_chunk->full()) {
        int count =
            current_chunk->allocate_batch(&ptrs[allocated], n - allocated);
        allocated += count;
    }

    // 如果还没分配够，遍历链表
    if (allocated < n) {
        // 遍历链表中的所有 chunk
        chunk_t* chunk = nullptr;
        while ((chunk = chunk_list_.find_if(
                    [](const chunk_t& ck) { return !ck.full(); })) != nullptr) {
            int remaining = n - allocated;
            int count = chunk->allocate_batch(&ptrs[allocated], remaining);
            allocated += count;

            if (allocated >= n) {
                active_chunk_.store(chunk, std::memory_order_relaxed);
                break;
            }
        }
    }

    // 如果还没分配够，创建新 chunk
    while (allocated < n) {
        chunk_t* new_chunk = create_new_chunk();
        if (new_chunk == nullptr) {
            break;
        }

        int remaining = n - allocated;
        int count = new_chunk->allocate_batch(&ptrs[allocated], remaining);
        allocated += count;
        active_chunk_.store(new_chunk, std::memory_order_relaxed);

        if (count == 0) {
            break;
        }
    }

    return allocated;
}

/**
 * @brief 批量释放内存
 * @param ptrs 指向要释放的对象指针数组
 * @param n 释放的数量
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
void size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    deallocate_batch(void** ptrs, int n) {
    if (!initialized_.load(std::memory_order_acquire) || ptrs == nullptr ||
        n <= 0) {
        return;
    }

    // 逐个释放
    for (int i = 0; i < n; ++i) {
        deallocate(ptrs[i]);
    }
}

/**
 * @brief 检查是否为空
 * @return true 如果没有已分配的内存
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    empty() const {
    if (!initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    // 检查是否有任何 chunk 非空
    chunk_t* chunk =
        const_cast<utils::lock_free_list<chunk_t, ChunkMaxCount>&>(chunk_list_)
            .find_if([](const chunk_t& ck) { return !ck.empty(); });

    return chunk == nullptr;
}

/**
 * @brief 检查是否已满
 * @return true 如果所有 chunk 都已满
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    full() const {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    std::size_t count = chunk_count_.load(std::memory_order_relaxed);
    if (count == 0) {
        return false;
    }

    // 查找未满的 chunk
    chunk_t* chunk =
        const_cast<utils::lock_free_list<chunk_t, ChunkMaxCount>&>(chunk_list_)
            .find_if([](const chunk_t& ck) { return !ck.full(); });

    // 如果所有 chunk 都满且未达到最大数量，则不算满
    if (chunk == nullptr && count < ChunkMaxCount) {
        return false;
    }

    return chunk == nullptr;
}

/**
 * @brief 检查是否已初始化
 * @return true 如果已初始化
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    is_initialized() const {
    return initialized_.load(std::memory_order_acquire);
}

/**
 * @brief 检查是否包含指定指针
 * @param ptr 要检查的指针
 * @return true 如果该指针属于此 size_class
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
bool size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    contains(void* ptr) const {
    if (ptr == nullptr || !initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    // 查找包含该指针的 chunk
    chunk_t* chunk =
        const_cast<utils::lock_free_list<chunk_t, ChunkMaxCount>&>(chunk_list_)
            .find_if([ptr](const chunk_t& ck) { return ck.contains(ptr); });

    return chunk != nullptr;
}

/**
 * @brief 创建函数指针表入口
 * @return size_class_base 函数指针表
 */
template <std::size_t ObjectSize,
          std::size_t ChunkSize,
          std::size_t ChunkMaxCount,
          std::size_t SlabSize,
          std::size_t SlabsPerGroup>
size_class_base
size_class<ObjectSize, ChunkSize, ChunkMaxCount, SlabSize, SlabsPerGroup>::
    create_vtable_entry() {
    size_class_base base;

    // 设置函数指针（静态模板函数）
    base.allocate_fn = [](void* self) -> void* {
        return static_cast<size_class*>(self)->allocate();
    };

    base.deallocate_fn = [](void* self, void* ptr) -> void {
        static_cast<size_class*>(self)->deallocate(ptr);
    };

    base.allocate_batch_fn = [](void* self, void** ptrs, int n) -> int {
        return static_cast<size_class*>(self)->allocate_batch(ptrs, n);
    };

    base.deallocate_batch_fn = [](void* self, void** ptrs, int n) -> void {
        static_cast<size_class*>(self)->deallocate_batch(ptrs, n);
    };

    base.get_object_size_fn = [](void* self) -> std::size_t {
        (void)self;
        return ObjectSize;
    };

    base.initialize_fn = [](void* self) -> bool {
        return static_cast<size_class*>(self)->initialize();
    };

    base.empty_fn = [](void* self) -> bool {
        return static_cast<size_class*>(self)->empty();
    };

    base.full_fn = [](void* self) -> bool {
        return static_cast<size_class*>(self)->full();
    };

    base.contains_fn = [](void* self, void* ptr) -> bool {
        return static_cast<size_class*>(self)->contains(ptr);
    };

    base.self = this;

    return base;
}

}  // namespace arena
}  // namespace cascading
