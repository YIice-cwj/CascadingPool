#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace cascading {
namespace utils {

template <typename T>
class tagger_ptr {
   public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer_tag =
        std::conditional_t<sizeof(void*) == 8, std::int64_t, std::int32_t>;
    constexpr static pointer_tag TAG_MARK =
        sizeof(void*) == 8 ? 0xFFFF000000000000LL : 0xFF000000;
    constexpr static pointer_tag MARK_SHIFT = sizeof(void*) == 8 ? 48 : 24;

   private:
    std::atomic<pointer_tag> pointer_tag_;

    static pointer_tag pack(pointer ptr, pointer_tag tag) {
        return (reinterpret_cast<pointer_tag>(ptr) & ~TAG_MARK) |
               ((tag << MARK_SHIFT) & TAG_MARK);
    }

   public:
    /**
     * @brief 构造函数
     * @details 构造函数将指针标签设置为 0
     */
    tagger_ptr() : pointer_tag_(0) {}

    /**
     * @brief 构造函数
     * @details 构造函数将指针标签设置为 pack(type_ptr, tag)
     * @param type_ptr 指向对象的指针
     * @param tag 标签值
     */
    explicit tagger_ptr(pointer type_ptr, pointer_tag tag = 0)
        : pointer_tag_(pack(type_ptr, tag)) {}

    /**
     * @brief 复制构造函数
     * @details 复制构造函数将指针标签设置为 other.pointer_tag_
     * @param other 要复制的 tagger_ptr 对象
     */
    tagger_ptr(const tagger_ptr& other)
        : pointer_tag_(other.pointer_tag_.load(std::memory_order_acquire)) {}

    /**
     * @brief 移动构造函数
     * @details 移动构造函数将指针标签设置为 other.pointer_tag_
     * @param other 要移动的 tagger_ptr 对象
     */
    tagger_ptr(tagger_ptr&& other) noexcept
        : pointer_tag_(other.pointer_tag_.load(std::memory_order_acquire)) {}

    /**
     * @brief 析构函数
     * @details 析构函数将指针标签设置为 0
     */
    ~tagger_ptr() { pointer_tag_.store(0, std::memory_order_release); }

    /**
     * @brief 获取指针
     * @details 获取指针标签中的指针部分
     * @param order 内存顺序
     * @return 指向对象的指针
     */
    pointer get_ptr(std::memory_order order = std::memory_order_acquire) const {
        return reinterpret_cast<pointer>(pointer_tag_.load(order) & ~TAG_MARK);
    }

    /**
     * @brief 获取标签
     * @details 获取指针标签中的标签部分
     * @param order 内存顺序
     * @return 标签值
     */
    pointer_tag get_tag(
        std::memory_order order = std::memory_order_acquire) const {
        return (pointer_tag_.load(order) & TAG_MARK) >> MARK_SHIFT;
    }

    /**
     * @brief 获取指针标签
     * @details 获取指针标签中的指针和标签部分
     * @param order 内存顺序
     * @return tagger_ptr 对象，包含指针和标签
     */
    tagger_ptr get_ptr_tag(
        std::memory_order order = std::memory_order_acquire) const {
        pointer_tag val = pointer_tag_.load(order);
        return tagger_ptr(reinterpret_cast<pointer>(val & ~TAG_MARK),
                          (val & TAG_MARK) >> MARK_SHIFT);
    }

    /**
     * @brief 设置指针
     * @details 设置指针标签中的指针部分
     * @param ptr 指向对象的指针
     * @param order 内存顺序
     * @return 之前的指针值
     */
    pointer set_ptr(pointer ptr,
                    std::memory_order order = std::memory_order_release) {
        pointer_tag current_tag = get_tag(std::memory_order_relaxed);
        pointer_tag old_val =
            pointer_tag_.exchange(pack(ptr, current_tag), order);
        return reinterpret_cast<pointer>(old_val & ~TAG_MARK);
    }

    /**
     * @brief 设置标签
     * @details 设置指针标签中的标签部分
     * @param tag 标签值
     * @param order 内存顺序
     * @return 之前的标签值
     */
    pointer_tag set_tag(pointer_tag tag,
                        std::memory_order order = std::memory_order_release) {
        pointer current_ptr = get_ptr(std::memory_order_relaxed);
        pointer_tag old_val =
            pointer_tag_.exchange(pack(current_ptr, tag), order);
        return (old_val & TAG_MARK) >> MARK_SHIFT;
    }

    /**
     * @brief 设置指针标签
     * @details 设置指针标签中的指针和标签部分
     * @param ptr 指向对象的指针
     * @param tag 标签值
     * @param order 内存顺序
     */
    void set_ptr_tag(pointer ptr,
                     pointer_tag tag,
                     std::memory_order order = std::memory_order_release) {
        pointer_tag_.store(pack(ptr, tag), order);
    }

    /**
     * @brief 复制赋值运算符
     * @details 复制赋值运算符将 other 的指针标签赋值给当前对象
     * @param other 要复制的 tagger_ptr 对象
     * @return 当前对象的引用
     */
    tagger_ptr& operator=(const tagger_ptr& other) {
        pointer_tag_.store(other.pointer_tag_.load(std::memory_order_acquire),
                           std::memory_order_release);
        return *this;
    }

    /**
     * @brief 移动赋值运算符
     * @details 移动赋值运算符将 other 的指针标签赋值给当前对象
     * @param other 要移动的 tagger_ptr 对象
     * @return 当前对象的引用
     */
    tagger_ptr& operator=(tagger_ptr&& other) noexcept {
        pointer_tag_.store(other.pointer_tag_.load(std::memory_order_acquire),
                           std::memory_order_release);
        return *this;
    }

    /**
     * @brief 获取指针标签
     * @details 获取指针标签
     * @return 指针标签
     */
    reference operator*() const { return *get_ptr(std::memory_order_acquire); }

    /**
     * @brief 成员访问运算符
     * @details 成员访问运算符返回指向对象的指针
     * @return 指向对象的指针
     */
    pointer operator->() const { return get_ptr(std::memory_order_acquire); }

    /**
     * @brief 赋值运算符
     * @details 赋值运算符将指针标签设置为指定值
     * @param tag 指定标签
     * @return 赋值运算符
     */
    bool compare_exchange_strong(
        tagger_ptr& expected,
        const tagger_ptr& desired,
        std::memory_order success_order = std::memory_order_release,
        std::memory_order fail_order = std::memory_order_acquire) {
        pointer_tag expected_val =
            pack(expected.get_ptr(std::memory_order_relaxed),
                 expected.get_tag(std::memory_order_relaxed));
        pointer_tag desired_val =
            pack(desired.get_ptr(std::memory_order_relaxed),
                 desired.get_tag(std::memory_order_relaxed));

        bool success = pointer_tag_.compare_exchange_strong(
            expected_val, desired_val, success_order, fail_order);

        if (!success) {
            expected =
                tagger_ptr(reinterpret_cast<pointer>(expected_val & ~TAG_MARK),
                           (expected_val & TAG_MARK) >> MARK_SHIFT);
        }

        return success;
    }

    /**
     * @brief 比较交换弱运算符
     * @details 比较交换弱运算符将指针标签与指定值进行比较交换
     * @param expected 预期的 tagger_ptr 对象
     * @param desired 要交换的 tagger_ptr 对象
     * @param success_order 成功时的内存顺序
     * @param fail_order 失败时的内存顺序
     * @return 是否交换成功
     */

    bool compare_exchange_weak(
        tagger_ptr& expected,
        const tagger_ptr& desired,
        std::memory_order success_order = std::memory_order_release,
        std::memory_order fail_order = std::memory_order_acquire) {
        pointer_tag expected_val =
            pack(expected.get_ptr(std::memory_order_relaxed),
                 expected.get_tag(std::memory_order_relaxed));
        pointer_tag desired_val =
            pack(desired.get_ptr(std::memory_order_relaxed),
                 desired.get_tag(std::memory_order_relaxed));

        bool success = pointer_tag_.compare_exchange_weak(
            expected_val, desired_val, success_order, fail_order);

        if (!success) {
            expected =
                tagger_ptr(reinterpret_cast<pointer>(expected_val & ~TAG_MARK),
                           (expected_val & TAG_MARK) >> MARK_SHIFT);
        }

        return success;
    }
};

}  // namespace utils
}  // namespace cascading
