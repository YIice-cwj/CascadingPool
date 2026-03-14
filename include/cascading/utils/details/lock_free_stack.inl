namespace cascading {
namespace utils {

/**
 * @brief 构造函数
 * @details 初始化空栈
 */
template <typename T, std::size_t N>
lock_free_stack<T, N>::lock_free_stack() : pool_(), top_(), size_(0) {}

/**
 * @brief 入栈操作
 * @details 将新元素压入栈顶，使用CAS循环直到成功
 * @param value 要压入的元素值
 * @return 是否成功
 */
template <typename T, std::size_t N>
bool lock_free_stack<T, N>::push(const value_type& value) {
    lock_free_node* new_node = pool_.acquire();
    if (new_node == nullptr) {
        return false;
    }

    new_node->value = value;

    tagger_ptr<lock_free_node> old_top =
        top_.get_ptr_tag(std::memory_order_acquire);

    do {
        new_node->next.set_ptr_tag(old_top.get_ptr(), old_top.get_tag(),
                                   std::memory_order_relaxed);
    } while (!top_.compare_exchange_weak(
        old_top, tagger_ptr<lock_free_node>(new_node, old_top.get_tag() + 1),
        std::memory_order_release, std::memory_order_acquire));

    size_.fetch_add(1, std::memory_order_release);
    return true;
}

/**
 * @brief 出栈操作
 * @details 从栈顶弹出元素，使用CAS循环直到成功
 * @param result 存储弹出值的引用
 * @return 是否成功
 */
template <typename T, std::size_t N>
bool lock_free_stack<T, N>::pop(value_type& result) {
    tagger_ptr<lock_free_node> old_top =
        top_.get_ptr_tag(std::memory_order_acquire);

    while (old_top.get_ptr() != nullptr) {
        lock_free_node* top_node = old_top.get_ptr();
        tagger_ptr<lock_free_node> new_top =
            top_node->next.get_ptr_tag(std::memory_order_acquire);
        if (top_.compare_exchange_weak(
                old_top,
                tagger_ptr<lock_free_node>(new_top.get_ptr(),
                                           old_top.get_tag() + 1),
                std::memory_order_release, std::memory_order_acquire)) {
            result = top_node->value;
            pool_.release(top_node);
            size_.fetch_sub(1, std::memory_order_release);

            return true;
        }
    }
    return false;
}

/**
 * @brief 在栈中直接构造对象
 * @details 使用完美转发在栈中直接构造对象，避免拷贝
 * @tparam Args 构造参数类型
 * @param args 构造参数
 * @return 是否成功
 */
template <typename T, std::size_t N>
template <typename... Args>
bool lock_free_stack<T, N>::emplace(Args&&... args) {
    lock_free_node* new_node = pool_.acquire();
    if (new_node == nullptr) {
        return false;
    }

    // 在节点内存中直接构造对象
    ::new (&new_node->value) value_type(std::forward<Args>(args)...);

    tagger_ptr<lock_free_node> old_top =
        top_.get_ptr_tag(std::memory_order_acquire);

    do {
        new_node->next.set_ptr_tag(old_top.get_ptr(), old_top.get_tag(),
                                   std::memory_order_relaxed);
    } while (!top_.compare_exchange_weak(
        old_top, tagger_ptr<lock_free_node>(new_node, old_top.get_tag() + 1),
        std::memory_order_release, std::memory_order_acquire));

    size_.fetch_add(1, std::memory_order_release);
    return true;
}

/**
 * @brief 获取栈大小
 * @details 返回当前栈中元素的数量
 * @return 栈大小
 */
template <typename T, std::size_t N>
typename lock_free_stack<T, N>::size_type lock_free_stack<T, N>::size() const {
    return size_.load(std::memory_order_acquire);
}

/**
 * @brief 检查栈是否为空
 * @details 判断栈是否为空，即栈顶指针是否为空
 * @return 是否为空
 */
template <typename T, std::size_t N>
bool lock_free_stack<T, N>::empty() const {
    return top_.get_ptr_tag(std::memory_order_acquire).get_ptr() == nullptr;
}

}  // namespace utils
}  // namespace cascading