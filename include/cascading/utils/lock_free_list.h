#pragma once
#include <atomic>
#include <memory>
#include "cascading/utils/lock_free_pool.h"
#include "cascading/utils/tagger_ptr.h"

namespace cascading {
namespace utils {

/**
 * @brief 无锁空闲链表
 * @details
 * 无锁空闲链表是一种基于无锁数据结构的链表，用于在多线程环境下安全地进行元素的添加、删除和查找操作。
 *          无锁空闲链表使用原子操作来确保在并发环境下的线程安全，避免了传统链表中可能出现的竞态条件和死锁问题。
 * @tparam T 链表元素类型
 * @tparam N 链表节点池大小
 */
template <typename T, std::size_t N>
class lock_free_list {
   public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;

   private:
    struct lock_free_node {
        tagger_ptr<lock_free_node> next;
        value_type value;
    };

    std::unique_ptr<lock_free_pool<lock_free_node, N>> pool_;
    tagger_ptr<lock_free_node> head_;
    tagger_ptr<lock_free_node> tail_;
    std::atomic<size_type> size_;
    std::atomic<size_type> max_retries_;

   public:
    /**
     * @brief 构造函数
     * @details 构造函数初始化空的无锁空闲链表
     * @param max_retries 最大重试次数
     */
    lock_free_list(size_type max_retries = 1000);

    /**
     * @brief 析构函数
     * @details 析构函数释放无锁空闲链表占用的内存
     */
    ~lock_free_list() = default;

    /**
     * @brief 向链表后端添加元素
     * @details 向链表后端添加元素，返回是否添加成功
     * @param value 要添加的元素值
     * @return 是否添加成功
     */
    bool push_back(const value_type& value);

    /**
     * @brief 向链表前端添加元素
     * @details 向链表前端添加元素，返回是否添加成功
     * @param value 要添加的元素值
     * @return 是否添加成功
     */
    bool push_front(const value_type& value);

    /**
     * @brief 在链表后端直接构造元素
     * @details 使用对象池分配节点并直接默认构造元素
     * @return 指向新构造元素的指针，失败返回 nullptr
     */
    pointer emplace_back();

    /**
     * @brief 在链表前端直接构造元素
     * @details 使用对象池分配节点并直接默认构造元素
     * @return 指向新构造元素的指针，失败返回 nullptr
     */
    pointer emplace_front();

    /**
     * @brief 从链表中删除元素
     * @details 从链表中删除第一个值为 value 的元素，返回是否删除成功
     * @param value 要删除的元素值
     * @return 是否删除成功
     */
    bool erase(const value_type& value);

    /**
     * @brief 查找链表中第一个值为 value 的元素
     * @details 查找链表中第一个值为 value 的元素，返回指向该元素的指针
     * @param value 要查找的元素值
     * @return 指向第一个值为 value 的元素的指针
     */
    pointer find(const value_type& value);

    /**
     * @brief 查找满足条件的第一个元素
     * @details 使用谓词函数查找链表中第一个满足条件的元素
     * @tparam Predicate 谓词函数类型
     * @param pred 谓词函数，返回 bool，参数为 const value_type&
     * @return 指向第一个满足条件的元素的指针，未找到返回 nullptr
     */
    template <typename Predicate>
    pointer find_if(Predicate pred);

    /**
     * @brief 获取链表元素数量
     * @details 返回链表当前存储的元素数量
     * @return 链表元素数量
     */
    size_type size() const;

    /**
     * @brief 检查链表是否为空
     * @details 返回链表是否为空
     * @return 是否为空
     */
    bool empty() const;

    /**
     * @brief 清空链表
     * @details 清空链表，将所有元素删除
     */
    void clear();

    /**
     * @brief 获取最大重试次数
     * @details 返回无锁空闲链表在进行操作时的最大重试次数
     * @return 最大重试次数
     */
    size_type max_retries() const;

    /**
     * @brief 设置最大重试次数
     * @details 设置无锁空闲链表在进行操作时的最大重试次数
     * @param max_retries 最大重试次数
     */
    void set_max_retries(size_type max_retries);
};

}  // namespace utils
}  // namespace cascading

#include "details/lock_free_list.inl"
