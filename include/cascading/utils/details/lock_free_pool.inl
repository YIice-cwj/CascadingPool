namespace cascading {
namespace utils {

// ============================================================================
// LIFO 策略实现（Treiber Stack）
// ============================================================================

template <typename T, std::size_t N>
lock_free_pool<T, N, pool_strategy::LIFO>::lock_free_pool()
    : buffer_(std::make_unique<std::array<value_type, N>>()),
      free_top_(&(*buffer_)[0], 0),
      next_(std::make_unique<std::array<std::atomic<pointer>, N>>()),
      size_(N) {
    for (size_type i = 0; i < N - 1; ++i) {
        (*next_)[i].store(&(*buffer_)[i + 1], std::memory_order_relaxed);
    }
    (*next_)[N - 1].store(nullptr, std::memory_order_relaxed);
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::LIFO>::pointer
lock_free_pool<T, N, pool_strategy::LIFO>::acquire() {
    tagger_ptr<value_type> old_top =
        free_top_.get_ptr_tag(std::memory_order_acquire);

    while (old_top.get_ptr() != nullptr) {
        pointer next_ptr = (*next_)[old_top.get_ptr() - &(*buffer_)[0]].load(
            std::memory_order_relaxed);
        tagger_ptr<value_type> new_top(next_ptr, old_top.get_tag() + 1);

        if (free_top_.compare_exchange_weak(old_top, new_top,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
            size_.fetch_sub(1, std::memory_order_relaxed);
            return old_top.get_ptr();
        }
    }
    return nullptr;
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::LIFO>::release(pointer ptr) {
    if (ptr < &(*buffer_)[0] || ptr >= &(*buffer_)[0] + N) {
        return false;
    }

    tagger_ptr<value_type> old_top =
        free_top_.get_ptr_tag(std::memory_order_acquire);

    while (true) {
        (*next_)[ptr - &(*buffer_)[0]].store(old_top.get_ptr(),
                                             std::memory_order_relaxed);
        tagger_ptr<value_type> new_top(ptr, old_top.get_tag() + 1);

        if (free_top_.compare_exchange_weak(old_top, new_top,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
            size_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::LIFO>::contains(pointer ptr) const {
    return begin() <= ptr && ptr < end();
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::LIFO>::empty() const {
    return free_top_.get_ptr(std::memory_order_acquire) == nullptr;
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::LIFO>::size_type
lock_free_pool<T, N, pool_strategy::LIFO>::size() const {
    return size_.load(std::memory_order_relaxed);
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::LIFO>::pointer
lock_free_pool<T, N, pool_strategy::LIFO>::begin() const {
    return &(*buffer_)[0];
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::LIFO>::pointer
lock_free_pool<T, N, pool_strategy::LIFO>::end() const {
    return &(*buffer_)[0] + N;
}

// ============================================================================
// FIFO 策略实现（Michael-Scott Queue）
// ============================================================================

template <typename T, std::size_t N>
lock_free_pool<T, N, pool_strategy::FIFO>::lock_free_pool()
    : buffer_(std::make_unique<std::array<value_type, N>>()),
      head_(&(*buffer_)[0], 0),
      tail_(&(*buffer_)[N - 1], 0),
      next_(std::make_unique<std::array<std::atomic<pointer>, N>>()),
      size_(N) {
    // 初始化链表：0 -> 1 -> 2 -> ... -> N-1 -> nullptr
    for (size_type i = 0; i < N - 1; ++i) {
        (*next_)[i].store(&(*buffer_)[i + 1], std::memory_order_relaxed);
    }
    (*next_)[N - 1].store(nullptr, std::memory_order_relaxed);
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::FIFO>::pointer
lock_free_pool<T, N, pool_strategy::FIFO>::acquire() {
    // FIFO: 从头部弹出
    tagger_ptr<value_type> old_head =
        head_.get_ptr_tag(std::memory_order_acquire);

    while (old_head.get_ptr() != nullptr) {
        pointer next_ptr = (*next_)[old_head.get_ptr() - &(*buffer_)[0]].load(
            std::memory_order_relaxed);
        tagger_ptr<value_type> new_head(next_ptr, old_head.get_tag() + 1);

        if (head_.compare_exchange_weak(old_head, new_head,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
            size_.fetch_sub(1, std::memory_order_relaxed);
            return old_head.get_ptr();
        }
    }
    return nullptr;
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::FIFO>::release(pointer ptr) {
    if (ptr < &(*buffer_)[0] || ptr >= &(*buffer_)[0] + N) {
        return false;
    }

    // FIFO: 添加到尾部
    // 先将新节点的 next 设为 nullptr
    (*next_)[ptr - &(*buffer_)[0]].store(nullptr, std::memory_order_relaxed);

    // 检查是否为空池（head 为 nullptr）
    tagger_ptr<value_type> old_head =
        head_.get_ptr_tag(std::memory_order_acquire);
    if (old_head.get_ptr() == nullptr) {
        // 空池情况：需要同时更新 head 和 tail
        // 尝试将 head 从 nullptr 改为 ptr
        tagger_ptr<value_type> new_head(ptr, old_head.get_tag() + 1);
        if (!head_.compare_exchange_weak(old_head, new_head,
                                         std::memory_order_release,
                                         std::memory_order_acquire)) {
            // CAS 失败，说明其他线程已经添加了节点，走正常流程
        } else {
            // 成功更新 head，现在更新 tail
            tagger_ptr<value_type> old_tail =
                tail_.get_ptr_tag(std::memory_order_acquire);
            tagger_ptr<value_type> new_tail(ptr, old_tail.get_tag() + 1);
            tail_.compare_exchange_weak(old_tail, new_tail,
                                        std::memory_order_release,
                                        std::memory_order_relaxed);
            size_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    // 正常情况：添加到尾部
    tagger_ptr<value_type> old_tail =
        tail_.get_ptr_tag(std::memory_order_acquire);

    while (true) {
        // 尝试将旧尾节点的 next 指向新节点
        pointer expected_null = nullptr;
        if ((*next_)[old_tail.get_ptr() - &(*buffer_)[0]].compare_exchange_weak(
                expected_null, ptr, std::memory_order_release,
                std::memory_order_relaxed)) {
            // 成功链接，现在更新 tail
            tagger_ptr<value_type> new_tail(ptr, old_tail.get_tag() + 1);
            tail_.compare_exchange_weak(old_tail, new_tail,
                                        std::memory_order_release,
                                        std::memory_order_relaxed);
            size_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        // 失败，说明其他线程已经添加了节点，帮助更新 tail
        pointer next_ptr = (*next_)[old_tail.get_ptr() - &(*buffer_)[0]].load(
            std::memory_order_acquire);
        if (next_ptr != nullptr) {
            tagger_ptr<value_type> new_tail(next_ptr, old_tail.get_tag() + 1);
            tail_.compare_exchange_weak(old_tail, new_tail,
                                        std::memory_order_release,
                                        std::memory_order_relaxed);
        }
    }
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::FIFO>::contains(pointer ptr) const {
    return begin() <= ptr && ptr < end();
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::FIFO>::empty() const {
    return head_.get_ptr(std::memory_order_acquire) == nullptr;
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::FIFO>::size_type
lock_free_pool<T, N, pool_strategy::FIFO>::size() const {
    return size_.load(std::memory_order_relaxed);
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::FIFO>::pointer
lock_free_pool<T, N, pool_strategy::FIFO>::begin() const {
    return &(*buffer_)[0];
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::FIFO>::pointer
lock_free_pool<T, N, pool_strategy::FIFO>::end() const {
    return &(*buffer_)[0] + N;
}

// ============================================================================
// 轮询策略实现（Round-Robin）
// ============================================================================

template <typename T, std::size_t N>
lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::lock_free_pool()
    : buffer_(std::make_unique<std::array<value_type, N>>()),
      in_use_(std::make_unique<std::array<std::atomic<bool>, N>>()),
      round_robin_index_(0),
      size_(0) {
    // 初始化所有槽位为未使用
    for (size_type i = 0; i < N; ++i) {
        (*in_use_)[i].store(false, std::memory_order_relaxed);
    }
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::pointer
lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::acquire() {
    // 轮询查找可用槽位
    size_type start_idx =
        round_robin_index_.fetch_add(1, std::memory_order_relaxed) % N;

    for (size_type i = 0; i < N; ++i) {
        size_type idx = (start_idx + i) % N;
        bool expected = false;
        if ((*in_use_)[idx].compare_exchange_weak(expected, true,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed)) {
            size_.fetch_add(1, std::memory_order_relaxed);
            return &(*buffer_)[idx];
        }
    }
    return nullptr;  // 所有槽位都被占用
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::release(pointer ptr) {
    if (ptr < &(*buffer_)[0] || ptr >= &(*buffer_)[0] + N) {
        return false;
    }

    size_type idx = ptr - &(*buffer_)[0];
    bool expected = true;
    if ((*in_use_)[idx].compare_exchange_weak(expected, false,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
        size_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::contains(
    pointer ptr) const {
    return begin() <= ptr && ptr < end();
}

template <typename T, std::size_t N>
bool lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::empty() const {
    return size_.load(std::memory_order_relaxed) == 0;
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::size_type
lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::size() const {
    return size_.load(std::memory_order_relaxed);
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::pointer
lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::begin() const {
    return &(*buffer_)[0];
}

template <typename T, std::size_t N>
typename lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::pointer
lock_free_pool<T, N, pool_strategy::ROUND_ROBIN>::end() const {
    return &(*buffer_)[0] + N;
}

}  // namespace utils
}  // namespace cascading
