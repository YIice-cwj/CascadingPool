#pragma once
#include "cascading/utils/lock_free_pool.h"
#include "cascading/utils/tagger_ptr.h"

namespace cascading {
namespace utils {

/**
 * @brief 无锁栈
 * @details
 * 无锁栈是一种基于无锁数据结构的栈，用于在多线程环境下安全地进行栈操作。
 *          无锁栈使用原子操作来确保在并发环境下的线程安全，避免了传统栈中可能出现的竞态条件和死锁问题。
 * @tparam T 元素类型
 * @tparam N 栈大小
 */
template <typename T, std::size_t N>
class lock_free_stack {
   public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;

   private:
    struct lock_free_node {
        value_type value;
        tagger_ptr<lock_free_node> next;
    };

   private:
    lock_free_pool<lock_free_node, N> pool_;
    tagger_ptr<lock_free_node> top_;
    std::atomic<size_type> size_;

   public:
    /**
     * @brief 构造函数
     * @details 初始化空栈
     */
    lock_free_stack();

    /**
     * @brief 入栈操作
     * @details 将新元素压入栈顶，使用CAS循环直到成功
     * @param value 要压入的元素值
     */
    bool push(const value_type& value);

    /**
     * @brief 出栈操作
     * @details 将栈顶元素弹出，使用CAS循环直到成功
     * @param result 存储弹出值的引用
     * @return 是否成功
     */
    bool pop(value_type& result);

    /**
     * @brief 在栈中直接构造对象
     * @details 使用完美转发在栈中直接构造对象，避免拷贝
     * @tparam Args 构造参数类型
     * @param args 构造参数
     * @return 是否成功
     */
    template <typename... Args>
    bool emplace(Args&&... args);

    /**
     * @brief 获取栈的大小
     * @return 栈中元素的数量
     */
    size_type size() const;

    /**
     * @brief 判断栈是否为空
     * @return 如果栈为空返回true
     */
    bool empty() const;
};

}  // namespace utils
}  // namespace cascading

#include "details/lock_free_stack.inl"
