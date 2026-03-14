#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cascading {
namespace utils {

class tagged_index {
   public:
    using index_type = std::uint32_t;
    using tag_type = std::uint32_t;
    using value_type = std::uint64_t;

   private:
    std::atomic<value_type> value_;

    /**
     * @brief 打包索引和标签
     * @details 将索引和标签打包成一个 64 位整数
     * @param index 索引值
     * @param tag 标签值
     * @return 打包后的 64 位整数
     */
    static value_type pack(index_type index, tag_type tag) {
        return (static_cast<value_type>(tag) << 32) |
               static_cast<value_type>(index);
    }

   public:
    /**
     * @brief 构造函数
     * @details 构造函数将索引和标签设置为 0
     */
    tagged_index() : value_(0) {}

    /**
     * @brief 构造函数
     * @details 构造函数将索引和标签设置为指定值
     * @param index 索引值
     * @param tag 标签值
     */
    explicit tagged_index(index_type index, tag_type tag = 0)
        : value_(pack(index, tag)) {}

    /**
     * @brief 复制构造函数
     * @details 复制构造函数将索引和标签设置为 other.value_
     * @param other 要复制的 tagged_index 对象
     */
    tagged_index(const tagged_index& other)
        : value_(other.value_.load(std::memory_order_acquire)) {}

    /**
     * @brief 移动构造函数
     * @details 移动构造函数将索引和标签设置为 other.value_
     * @param other 要移动的 tagged_index 对象
     */
    tagged_index(tagged_index&& other) noexcept
        : value_(other.value_.load(std::memory_order_acquire)) {}

    /**
     * @brief 析构函数
     * @details 析构函数将索引和标签设置为 0
     */
    ~tagged_index() = default;

    /**
     * @brief 获取索引
     * @details 获取索引标签中的索引部分
     * @param order 内存顺序
     * @return 索引值
     */
    index_type get_index(
        std::memory_order order = std::memory_order_acquire) const {
        return static_cast<index_type>(value_.load(order) & 0xFFFFFFFF);
    }

    /**
     * @brief 获取标签
     * @details 获取索引标签中的标签部分
     * @param order 内存顺序
     * @return 标签值
     */
    tag_type get_tag(
        std::memory_order order = std::memory_order_acquire) const {
        return static_cast<tag_type>((value_.load(order) >> 32) & 0xFFFFFFFF);
    }

    /**
     * @brief 获取索引标签
     * @details 获取索引标签中的索引和标签部分
     * @param order 内存顺序
     * @return tagged_index 对象，包含索引和标签
     */
    tagged_index get_index_tag(
        std::memory_order order = std::memory_order_acquire) const {
        value_type val = value_.load(order);
        return tagged_index(static_cast<index_type>(val & 0xFFFFFFFF),
                            static_cast<tag_type>((val >> 32) & 0xFFFFFFFF));
    }

    /**
     * @brief 设置索引
     * @details 设置索引标签中的索引部分
     * @param index 索引值
     * @param order 内存顺序
     * @return 之前的索引值
     */
    void set_index(index_type index,
                   std::memory_order order = std::memory_order_release) {
        tag_type current_tag = get_tag(std::memory_order_relaxed);
        value_.store(pack(index, current_tag), order);
    }

    /**
     * @brief 设置标签
     * @details 设置索引标签中的标签部分
     * @param tag 标签值
     * @param order 内存顺序
     * @return 之前的标签值
     */
    void set_tag(tag_type tag,
                 std::memory_order order = std::memory_order_release) {
        index_type current_index = get_index(std::memory_order_relaxed);
        value_.store(pack(current_index, tag), order);
    }

    /**
     * @brief 设置索引标签
     * @details 设置索引标签中的索引和标签部分
     * @param index 索引值
     * @param tag 标签值
     * @param order 内存顺序
     * @return 之前的索引标签
     */
    void set_index_tag(index_type index,
                       tag_type tag,
                       std::memory_order order = std::memory_order_release) {
        value_.store(pack(index, tag), order);
    }

    /**
     * @brief 复制赋值运算符
     * @details 复制赋值运算符将索引和标签设置为 other.value_
     * @param other 要复制的 tagged_index 对象
     * @return 当前对象的引用
     */
    tagged_index& operator=(const tagged_index& other) {
        value_.store(other.value_.load(std::memory_order_acquire),
                     std::memory_order_release);
        return *this;
    }

    /**
     * @brief 移动赋值运算符
     * @details 移动赋值运算符将索引和标签设置为 other.value_
     * @param other 要移动的 tagged_index 对象
     * @return 当前对象的引用
     */
    tagged_index& operator=(tagged_index&& other) noexcept {
        value_.store(other.value_.load(std::memory_order_acquire),
                     std::memory_order_release);
        return *this;
    }

    /**
     * @brief 比较并交换强
     * @details 比较并交换强将索引和标签设置为 desired.value_，如果当前值等于
     * expected.value_
     * @param expected 要比较的 tagged_index 对象
     * @param desired 要设置的 tagged_index 对象
     * @param success_order 成功时的内存顺序
     * @param fail_order 失败时的内存顺序
     * @return 是否成功
     */
    bool compare_exchange_strong(
        tagged_index& expected,
        const tagged_index& desired,
        std::memory_order success_order = std::memory_order_release,
        std::memory_order fail_order = std::memory_order_acquire) {
        value_type expected_val =
            pack(expected.get_index(std::memory_order_relaxed),
                 expected.get_tag(std::memory_order_relaxed));
        value_type desired_val =
            pack(desired.get_index(std::memory_order_relaxed),
                 desired.get_tag(std::memory_order_relaxed));

        bool success = value_.compare_exchange_strong(
            expected_val, desired_val, success_order, fail_order);

        if (!success) {
            expected = tagged_index(
                static_cast<index_type>(expected_val & 0xFFFFFFFF),
                static_cast<tag_type>((expected_val >> 32) & 0xFFFFFFFF));
        }

        return success;
    }

    /**
     * @brief 比较并交换弱
     * @details 比较并交换弱将索引和标签设置为 desired.value_，如果当前值等于
     * expected.value_
     * @param expected 要比较的 tagged_index 对象
     * @param desired 要设置的 tagged_index 对象
     * @param success_order 成功时的内存顺序
     * @param fail_order 失败时的内存顺序
     * @return 是否成功
     */
    bool compare_exchange_weak(
        tagged_index& expected,
        const tagged_index& desired,
        std::memory_order success_order = std::memory_order_release,
        std::memory_order fail_order = std::memory_order_acquire) {
        value_type expected_val =
            pack(expected.get_index(std::memory_order_relaxed),
                 expected.get_tag(std::memory_order_relaxed));
        value_type desired_val =
            pack(desired.get_index(std::memory_order_relaxed),
                 desired.get_tag(std::memory_order_relaxed));

        bool success = value_.compare_exchange_weak(expected_val, desired_val,
                                                    success_order, fail_order);

        if (!success) {
            expected = tagged_index(
                static_cast<index_type>(expected_val & 0xFFFFFFFF),
                static_cast<tag_type>((expected_val >> 32) & 0xFFFFFFFF));
        }

        return success;
    }
};

}  // namespace utils
}  // namespace cascading
