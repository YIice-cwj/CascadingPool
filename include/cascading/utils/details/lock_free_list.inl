namespace cascading {
namespace utils {

template <typename T, std::size_t N>
lock_free_list<T, N>::lock_free_list(size_type max_retries)
    : pool_(std::make_unique<lock_free_pool<lock_free_node, N>>()),
      head_(nullptr, 0),
      tail_(nullptr, 0),
      size_(0),
      max_retries_(max_retries) {}

template <typename T, std::size_t N>
bool lock_free_list<T, N>::push_back(const value_type& value) {
    auto new_node = pool_->acquire();
    if (!new_node) {
        return false;
    }

    new_node->next.set_ptr_tag(nullptr, 0, std::memory_order_relaxed);
    new_node->value = value;

    size_type retry_count = 0;
    size_type max_retries = max_retries_.load(std::memory_order_acquire);

    while (retry_count < max_retries) {
        retry_count++;

        tagger_ptr<lock_free_node> current_tail =
            tail_.get_ptr_tag(std::memory_order_acquire);

        if (!current_tail.get_ptr()) {
            tagger_ptr<lock_free_node> new_tail(new_node, 0);
            if (tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                tagger_ptr<lock_free_node> current_head =
                    head_.get_ptr_tag(std::memory_order_acquire);
                tagger_ptr<lock_free_node> new_head(new_node,
                                                    current_head.get_tag() + 1);
                head_.compare_exchange_weak(current_head, new_head,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
                size_.fetch_add(1, std::memory_order_release);
                return true;
            }
        } else {
            tagger_ptr<lock_free_node> tail_next =
                current_tail->next.get_ptr_tag(std::memory_order_acquire);

            if (tail_next.get_ptr()) {
                tagger_ptr<lock_free_node> new_tail(tail_next.get_ptr(),
                                                    current_tail.get_tag() + 1);
                tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
                continue;
            }

            tagger_ptr<lock_free_node> new_next(new_node,
                                                tail_next.get_tag() + 1);
            if (current_tail->next.compare_exchange_weak(
                    tail_next, new_next, std::memory_order_release,
                    std::memory_order_acquire)) {
                tagger_ptr<lock_free_node> new_tail(new_node,
                                                    current_tail.get_tag() + 1);
                tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
                size_.fetch_add(1, std::memory_order_release);
                return true;
            }
        }
    }

    pool_->release(new_node);
    return false;
}

template <typename T, std::size_t N>
typename lock_free_list<T, N>::pointer lock_free_list<T, N>::emplace_back() {
    auto new_node = pool_->acquire();
    if (!new_node) {
        return nullptr;
    }

    // 直接默认构造值
    ::new (&new_node->value) value_type();

    new_node->next.set_ptr_tag(nullptr, 0, std::memory_order_relaxed);

    size_type retry_count = 0;
    size_type max_retries = max_retries_.load(std::memory_order_acquire);

    while (retry_count < max_retries) {
        retry_count++;

        tagger_ptr<lock_free_node> current_tail =
            tail_.get_ptr_tag(std::memory_order_acquire);

        if (!current_tail.get_ptr()) {
            tagger_ptr<lock_free_node> new_tail(new_node, 0);
            if (tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                tagger_ptr<lock_free_node> current_head =
                    head_.get_ptr_tag(std::memory_order_acquire);
                tagger_ptr<lock_free_node> new_head(new_node,
                                                    current_head.get_tag() + 1);
                head_.compare_exchange_weak(current_head, new_head,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
                size_.fetch_add(1, std::memory_order_release);
                return &new_node->value;
            }
        } else {
            tagger_ptr<lock_free_node> tail_next =
                current_tail->next.get_ptr_tag(std::memory_order_acquire);

            if (tail_next.get_ptr()) {
                tagger_ptr<lock_free_node> new_tail(tail_next.get_ptr(),
                                                    current_tail.get_tag() + 1);
                tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
                continue;
            }

            tagger_ptr<lock_free_node> new_next(new_node,
                                                tail_next.get_tag() + 1);
            if (current_tail->next.compare_exchange_weak(
                    tail_next, new_next, std::memory_order_release,
                    std::memory_order_acquire)) {
                tagger_ptr<lock_free_node> new_tail(new_node,
                                                    current_tail.get_tag() + 1);
                tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
                size_.fetch_add(1, std::memory_order_release);
                return &new_node->value;
            }
        }
    }

    pool_->release(new_node);
    return nullptr;
}

template <typename T, std::size_t N>
typename lock_free_list<T, N>::pointer lock_free_list<T, N>::emplace_front() {
    auto new_node = pool_->acquire();
    if (!new_node) {
        return nullptr;
    }

    // 直接默认构造值
    ::new (&new_node->value) value_type();

    tagger_ptr<lock_free_node> current_head =
        head_.get_ptr_tag(std::memory_order_acquire);
    new_node->next.set_ptr_tag(current_head.get_ptr(), current_head.get_tag(),
                               std::memory_order_relaxed);

    size_type retry_count = 0;
    size_type max_retries = max_retries_.load(std::memory_order_acquire);

    while (retry_count < max_retries) {
        retry_count++;

        tagger_ptr<lock_free_node> new_head(new_node,
                                            current_head.get_tag() + 1);
        if (head_.compare_exchange_weak(current_head, new_head,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
            if (!current_head.get_ptr()) {
                tagger_ptr<lock_free_node> current_tail =
                    tail_.get_ptr_tag(std::memory_order_acquire);
                tagger_ptr<lock_free_node> new_tail(new_node, 0);
                tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
            }
            size_.fetch_add(1, std::memory_order_release);
            return &new_node->value;
        }
        new_node->next.set_ptr_tag(current_head.get_ptr(),
                                   current_head.get_tag(),
                                   std::memory_order_relaxed);
    }

    pool_->release(new_node);
    return nullptr;
}

template <typename T, std::size_t N>
bool lock_free_list<T, N>::push_front(const value_type& value) {
    auto new_node = pool_->acquire();
    if (!new_node) {
        return false;
    }

    tagger_ptr<lock_free_node> current_head =
        head_.get_ptr_tag(std::memory_order_acquire);
    new_node->next.set_ptr_tag(current_head.get_ptr(), current_head.get_tag(),
                               std::memory_order_relaxed);
    new_node->value = value;

    size_type retry_count = 0;
    size_type max_retries = max_retries_.load(std::memory_order_acquire);

    while (retry_count < max_retries) {
        retry_count++;

        tagger_ptr<lock_free_node> new_head(new_node,
                                            current_head.get_tag() + 1);
        if (head_.compare_exchange_weak(current_head, new_head,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
            if (!current_head.get_ptr()) {
                tagger_ptr<lock_free_node> current_tail =
                    tail_.get_ptr_tag(std::memory_order_acquire);
                tagger_ptr<lock_free_node> new_tail(new_node, 0);
                tail_.compare_exchange_weak(current_tail, new_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
            }
            size_.fetch_add(1, std::memory_order_release);
            return true;
        }
        new_node->next.set_ptr_tag(current_head.get_ptr(),
                                   current_head.get_tag(),
                                   std::memory_order_relaxed);
    }

    pool_->release(new_node);
    return false;
}

template <typename T, std::size_t N>
bool lock_free_list<T, N>::erase(const value_type& value) {
    size_type retry_count = 0;
    size_type max_retries = max_retries_.load(std::memory_order_acquire);
    while (retry_count < max_retries) {
        retry_count++;

        tagger_ptr<lock_free_node> current_head =
            head_.get_ptr_tag(std::memory_order_acquire);

        if (!current_head.get_ptr()) {
            return false;
        }

        if (current_head->value == value) {
            tagger_ptr<lock_free_node> next =
                current_head->next.get_ptr_tag(std::memory_order_acquire);
            tagger_ptr<lock_free_node> new_head(next.get_ptr(),
                                                current_head.get_tag() + 1);

            if (head_.compare_exchange_weak(current_head, new_head,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                if (!next.get_ptr()) {
                    tagger_ptr<lock_free_node> current_tail =
                        tail_.get_ptr_tag(std::memory_order_acquire);
                    tagger_ptr<lock_free_node> null_tail(
                        nullptr, current_tail.get_tag() + 1);
                    tail_.compare_exchange_weak(current_tail, null_tail,
                                                std::memory_order_release,
                                                std::memory_order_acquire);
                }
                pool_->release(current_head.get_ptr());
                size_.fetch_sub(1, std::memory_order_release);
                return true;
            }
            continue;
        }

        tagger_ptr<lock_free_node> prev = current_head;
        bool found = false;

        while (prev.get_ptr()) {
            tagger_ptr<lock_free_node> current =
                prev->next.get_ptr_tag(std::memory_order_acquire);

            if (!current.get_ptr()) {
                break;
            }

            if (current->value == value) {
                found = true;
                tagger_ptr<lock_free_node> next =
                    current->next.get_ptr_tag(std::memory_order_acquire);
                tagger_ptr<lock_free_node> new_next(next.get_ptr(),
                                                    current.get_tag() + 1);

                if (prev->next.compare_exchange_weak(
                        current, new_next, std::memory_order_release,
                        std::memory_order_acquire)) {
                    if (!next.get_ptr()) {
                        tagger_ptr<lock_free_node> current_tail =
                            tail_.get_ptr_tag(std::memory_order_acquire);
                        tagger_ptr<lock_free_node> new_tail(
                            prev.get_ptr(), current_tail.get_tag() + 1);
                        tail_.compare_exchange_weak(current_tail, new_tail,
                                                    std::memory_order_release,
                                                    std::memory_order_acquire);
                    }
                    pool_->release(current.get_ptr());
                    size_.fetch_sub(1, std::memory_order_release);
                    return true;
                }
                break;
            }
            prev = current;
        }

        if (!found) {
            return false;
        }
    }

    return false;
}

template <typename T, std::size_t N>
typename lock_free_list<T, N>::pointer lock_free_list<T, N>::find(
    const value_type& value) {
    tagger_ptr<lock_free_node> current =
        head_.get_ptr_tag(std::memory_order_acquire);

    while (current.get_ptr()) {
        if (current->value == value) {
            return &(current->value);
        }
        current = current->next.get_ptr_tag(std::memory_order_acquire);
    }

    return nullptr;
}

template <typename T, std::size_t N>
template <typename Predicate>
typename lock_free_list<T, N>::pointer lock_free_list<T, N>::find_if(
    Predicate pred) {
    tagger_ptr<lock_free_node> current =
        head_.get_ptr_tag(std::memory_order_acquire);

    while (current.get_ptr()) {
        if (pred(current->value)) {
            return &(current->value);
        }
        current = current->next.get_ptr_tag(std::memory_order_acquire);
    }

    return nullptr;
}

template <typename T, std::size_t N>
typename lock_free_list<T, N>::size_type lock_free_list<T, N>::size() const {
    return size_.load(std::memory_order_acquire);
}

template <typename T, std::size_t N>
bool lock_free_list<T, N>::empty() const {
    return head_.get_ptr_tag(std::memory_order_acquire).get_ptr() == nullptr;
}

template <typename T, std::size_t N>
void lock_free_list<T, N>::clear() {
    size_type retry_count = 0;
    size_type max_retries = max_retries_.load(std::memory_order_acquire);
    while (retry_count < max_retries) {
        retry_count++;

        tagger_ptr<lock_free_node> current_head =
            head_.get_ptr_tag(std::memory_order_acquire);

        if (!current_head.get_ptr()) {
            break;
        }

        tagger_ptr<lock_free_node> next =
            current_head->next.get_ptr_tag(std::memory_order_acquire);
        tagger_ptr<lock_free_node> new_head(next.get_ptr(),
                                            current_head.get_tag() + 1);

        if (head_.compare_exchange_weak(current_head, new_head,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
            if (!next.get_ptr()) {
                tagger_ptr<lock_free_node> current_tail =
                    tail_.get_ptr_tag(std::memory_order_acquire);
                tagger_ptr<lock_free_node> null_tail(
                    nullptr, current_tail.get_tag() + 1);
                tail_.compare_exchange_weak(current_tail, null_tail,
                                            std::memory_order_release,
                                            std::memory_order_acquire);
            }
            pool_->release(current_head.get_ptr());
            size_.fetch_sub(1, std::memory_order_release);
            retry_count = 0;
        }
    }
}
/**
 * @brief 获取最大重试次数
 * @details 返回无锁空闲链表在进行操作时的最大重试次数
 * @return 最大重试次数
 */
template <typename T, std::size_t N>
typename lock_free_list<T, N>::size_type lock_free_list<T, N>::max_retries()
    const {
    return max_retries_.load(std::memory_order_acquire);
}

/**
 * @brief 设置最大重试次数
 * @details 设置无锁空闲链表在进行操作时的最大重试次数
 * @param max_retries 最大重试次数
 */
template <typename T, std::size_t N>
void lock_free_list<T, N>::set_max_retries(size_type max_retries) {
    max_retries_.store(max_retries, std::memory_order_release);
}
}  // namespace utils
}  // namespace cascading
