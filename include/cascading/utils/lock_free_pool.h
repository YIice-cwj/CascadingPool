#pragma once
#include <array>
#include <atomic>
#include <memory>
#include "cascading/utils/tagger_ptr.h"

namespace cascading {
namespace utils {

/**
 * @brief 无锁池策略枚举
 * @details 定义三种不同的内存分配策略：
 *          - LIFO: 后进先出，使用栈结构，适合缓存友好的场景
 *          - FIFO: 先进先出，使用队列结构，适合需要公平性的场景
 *          - ROUND_ROBIN: 轮询分配，适合高并发负载均衡场景
 */
enum class pool_strategy {
    LIFO,        ///< 后进先出（栈）- 默认策略
    FIFO,        ///< 先进先出（队列）
    ROUND_ROBIN  ///< 轮询分配
};

/**
 * @brief 无锁空闲池主模板（前置声明）
 * @details 无锁空闲池是一种基于无锁数据结构的内存池，用于在多线程环境下
 *          安全地进行内存分配和释放操作。支持多种策略：LIFO（栈）、
 *          FIFO（队列）、轮询。
 * @tparam T 内存块类型
 * @tparam N 内存池大小（固定容量）
 * @tparam Strategy 池策略，默认为 LIFO
 *
 * @par 使用示例
 * @code
 * // 默认 LIFO 策略
 * lock_free_pool<int, 100> pool_lifo;
 *
 * // FIFO 策略
 * lock_free_pool<int, 100, pool_strategy::FIFO> pool_fifo;
 *
 * // 轮询策略
 * lock_free_pool<int, 100, pool_strategy::ROUND_ROBIN> pool_rr;
 *
 * // 获取内存块
 * int* ptr = pool_lifo.acquire();
 *
 * // 释放内存块
 * pool_lifo.release(ptr);
 * @endcode
 */
template <typename T,
          std::size_t N,
          pool_strategy Strategy = pool_strategy::LIFO>
class lock_free_pool;

/**
 * @brief LIFO 策略无锁池特化（栈结构）
 * @details 使用 Treiber Stack 算法实现，特点：
 *          - 后进先出（LIFO）语义
 *          - 使用 tagger_ptr 实现 ABA 安全
 *          - 高并发性能优秀
 *          - 缓存友好（最近使用的元素最先被复用）
 *
 * @par 适用场景
 * - 一般内存池场景（默认选择）
 * - 需要缓存局部性的场景
 * - 分配和释放频率相近的场景
 */
template <typename T, std::size_t N>
class lock_free_pool<T, N, pool_strategy::LIFO> {
   public:
    using value_type = T;                       ///< 元素类型
    using size_type = std::size_t;              ///< 大小类型
    using pointer = value_type*;                ///< 指针类型
    using const_pointer = const value_type*;    ///< 常量指针类型
    using reference = value_type&;              ///< 引用类型
    using const_reference = const value_type&;  ///< 常量引用类型

   private:
    ///< 存储元素的缓冲区，使用 unique_ptr 管理生命周期
    std::unique_ptr<std::array<value_type, N>> buffer_;

    ///< 栈顶指针（带标签，用于 ABA 保护）
    tagger_ptr<value_type> free_top_;

    ///< 每个元素的 next 指针数组，构成链表结构
    std::unique_ptr<std::array<std::atomic<pointer>, N>> next_;

    ///< 当前空闲元素数量
    std::atomic<size_type> size_;

   public:
    /**
     * @brief 构造函数
     * @details 初始化所有元素，将它们链接成一个链表，栈顶指向第一个元素
     */
    lock_free_pool();

    /**
     * @brief 析构函数
     * @details 自动释放缓冲区内存
     */
    ~lock_free_pool() = default;

    /**
     * @brief 获取一个空闲元素（LIFO - 后进先出）
     * @return 指向获取到的元素的指针，如果池为空则返回 nullptr
     * @details 使用 CAS 操作原子地从栈顶弹出元素。如果并发竞争激烈，
     *          会自动重试直到成功或确认池为空。
     *
     * @par 线程安全
     * 此操作是线程安全的，可以在多线程环境下并发调用。
     */
    pointer acquire();

    /**
     * @brief 释放一个元素回池中（LIFO - 压入栈顶）
     * @param ptr 要释放的元素指针
     * @return 如果成功释放返回 true，如果指针无效返回 false
     * @details 使用 CAS 操作原子地将元素压入栈顶。如果并发竞争激烈，
     *          会自动重试直到成功。
     *
     * @par 线程安全
     * 此操作是线程安全的，可以在多线程环境下并发调用。
     */
    bool release(pointer ptr);

    /**
     * @brief 检查指针是否属于本池
     * @param ptr 要检查的指针
     * @return 如果指针在池的地址范围内返回 true，否则返回 false
     */
    bool contains(pointer ptr) const;

    /**
     * @brief 检查池是否为空
     * @return 如果没有空闲元素返回 true，否则返回 false
     */
    bool empty() const;

    /**
     * @brief 获取当前空闲元素数量
     * @return 空闲元素数量（近似值，由于并发可能不完全准确）
     */
    size_type size() const;

    /**
     * @brief 获取池的起始地址
     * @return 指向第一个元素的指针
     */
    pointer begin() const;

    /**
     * @brief 获取池的结束地址（不包含）
     * @return 指向最后一个元素之后位置的指针
     */
    pointer end() const;
};

/**
 * @brief FIFO 策略无锁池特化（队列结构）
 * @details 使用 Michael-Scott Queue 算法实现，特点：
 *          - 先进先出（FIFO）语义
 *          - 使用 tagger_ptr 实现 ABA 安全
 *          - 公平性：先释放的元素先被获取
 *          - 适合需要公平分配的场景
 *
 * @par 适用场景
 * - 需要公平分配的场景
 * - 资源使用顺序重要的场景
 * - 避免某些元素长期被占用的场景
 */
template <typename T, std::size_t N>
class lock_free_pool<T, N, pool_strategy::FIFO> {
   public:
    using value_type = T;                       ///< 元素类型
    using size_type = std::size_t;              ///< 大小类型
    using pointer = value_type*;                ///< 指针类型
    using const_pointer = const value_type*;    ///< 常量指针类型
    using reference = value_type&;              ///< 引用类型
    using const_reference = const value_type&;  ///< 常量引用类型

   private:
    ///< 存储元素的缓冲区，使用 unique_ptr 管理生命周期
    std::unique_ptr<std::array<value_type, N>> buffer_;

    ///< 队列头部（出队端），带标签用于 ABA 保护
    alignas(64) tagger_ptr<value_type> head_;

    ///< 队列尾部（入队端），带标签用于 ABA 保护
    alignas(64) tagger_ptr<value_type> tail_;

    ///< 每个元素的 next 指针数组，构成链表结构
    std::unique_ptr<std::array<std::atomic<pointer>, N>> next_;

    ///< 当前空闲元素数量
    std::atomic<size_type> size_;

   public:
    /**
     * @brief 构造函数
     * @details 初始化所有元素，将它们链接成一个链表，head 指向第一个元素，
     *          tail 指向最后一个元素
     */
    lock_free_pool();

    /**
     * @brief 析构函数
     * @details 自动释放缓冲区内存
     */
    ~lock_free_pool() = default;

    /**
     * @brief 获取一个空闲元素（FIFO - 先进先出）
     * @return 指向获取到的元素的指针，如果池为空则返回 nullptr
     * @details 使用 CAS 操作原子地从队列头部弹出元素。保证先释放的元素
     *          先被获取（公平性）。
     *
     * @par 线程安全
     * 此操作是线程安全的，可以在多线程环境下并发调用。
     */
    pointer acquire();

    /**
     * @brief 释放一个元素回池中（FIFO - 加入队尾）
     * @param ptr 要释放的元素指针
     * @return 如果成功释放返回 true，如果指针无效返回 false
     * @details 使用 CAS 操作原子地将元素添加到队列尾部。如果检测到其他
     *          线程已经添加了元素，会帮助更新 tail 指针（辅助机制）。
     *
     * @par 线程安全
     * 此操作是线程安全的，可以在多线程环境下并发调用。
     */
    bool release(pointer ptr);

    /**
     * @brief 检查指针是否属于本池
     * @param ptr 要检查的指针
     * @return 如果指针在池的地址范围内返回 true，否则返回 false
     */
    bool contains(pointer ptr) const;

    /**
     * @brief 检查池是否为空
     * @return 如果没有空闲元素返回 true，否则返回 false
     */
    bool empty() const;

    /**
     * @brief 获取当前空闲元素数量
     * @return 空闲元素数量（近似值，由于并发可能不完全准确）
     */
    size_type size() const;

    /**
     * @brief 获取池的起始地址
     * @return 指向第一个元素的指针
     */
    pointer begin() const;

    /**
     * @brief 获取池的结束地址（不包含）
     * @return 指向最后一个元素之后位置的指针
     */
    pointer end() const;
};

/**
 * @brief 轮询策略无锁池特化
 * @details 使用原子标记数组实现，特点：
 *          - 轮询分配，负载均衡
 *          - 每个槽位独立标记使用状态
 *          - 避免某些槽位过度使用
 *          - 适合高并发场景
 *
 * @par 适用场景
 * - 高并发负载均衡场景
 * - 避免热点问题的场景
 * - 需要均匀分配资源的场景
 */
template <typename T, std::size_t N>
class lock_free_pool<T, N, pool_strategy::ROUND_ROBIN> {
   public:
    using value_type = T;                       ///< 元素类型
    using size_type = std::size_t;              ///< 大小类型
    using pointer = value_type*;                ///< 指针类型
    using const_pointer = const value_type*;    ///< 常量指针类型
    using reference = value_type&;              ///< 引用类型
    using const_reference = const value_type&;  ///< 常量引用类型

   private:
    ///< 存储元素的缓冲区，使用 unique_ptr 管理生命周期
    std::unique_ptr<std::array<value_type, N>> buffer_;

    ///< 每个槽位的使用状态（true=已使用，false=空闲）
    std::unique_ptr<std::array<std::atomic<bool>, N>> in_use_;

    ///< 轮询索引，原子递增实现轮询
    alignas(64) std::atomic<size_type> round_robin_index_;

    ///< 当前已使用元素数量
    std::atomic<size_type> size_;

   public:
    /**
     * @brief 构造函数
     * @details 初始化所有槽位为未使用状态，轮询索引从 0 开始
     */
    lock_free_pool();

    /**
     * @brief 析构函数
     * @details 自动释放缓冲区内存
     */
    ~lock_free_pool() = default;

    /**
     * @brief 获取一个空闲元素（轮询策略）
     * @return 指向获取到的元素的指针，如果池已满则返回 nullptr
     * @details 从当前轮询索引开始查找空闲槽位，使用 CAS 操作原子地
     *          标记槽位为已使用。轮询索引原子递增，确保不同线程从
     *          不同位置开始查找，实现负载均衡。
     *
     * @par 线程安全
     * 此操作是线程安全的，可以在多线程环境下并发调用。
     */
    pointer acquire();

    /**
     * @brief 释放一个元素回池中
     * @param ptr 要释放的元素指针
     * @return 如果成功释放返回 true，如果指针无效或已经释放返回 false
     * @details 使用 CAS 操作原子地将对应槽位标记为未使用。
     *
     * @par 线程安全
     * 此操作是线程安全的，可以在多线程环境下并发调用。
     */
    bool release(pointer ptr);

    /**
     * @brief 检查指针是否属于本池
     * @param ptr 要检查的指针
     * @return 如果指针在池的地址范围内返回 true，否则返回 false
     */
    bool contains(pointer ptr) const;

    /**
     * @brief 检查池是否为空（所有槽位都未使用）
     * @return 如果没有使用中的元素返回 true，否则返回 false
     * @note 注意：ROUND_ROBIN 策略的 empty() 表示"没有使用中的元素"，
     *       与 LIFO/FIFO 策略的"没有空闲元素"语义相反
     */
    bool empty() const;

    /**
     * @brief 获取当前已使用元素数量
     * @return 已使用元素数量（近似值，由于并发可能不完全准确）
     */
    size_type size() const;

    /**
     * @brief 获取池的起始地址
     * @return 指向第一个元素的指针
     */
    pointer begin() const;

    /**
     * @brief 获取池的结束地址（不包含）
     * @return 指向最后一个元素之后位置的指针
     */
    pointer end() const;
};

}  // namespace utils
}  // namespace cascading

#include "details/lock_free_pool.inl"
