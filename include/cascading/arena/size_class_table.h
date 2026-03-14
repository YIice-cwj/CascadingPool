#pragma once
#include <array>
#include <cstdint>
namespace cascading {
namespace arena {

/**
 * @brief 内存分级表类
 */
class size_class_table {
   public:
    static constexpr std::size_t ALIGNMENT_SHIFT = 3;  // 8字节对齐的位移量
    static constexpr std::size_t LARGE_CLASS_THRESHOLD = 11;  // 大对象分类阈值
    static constexpr std::size_t CLASS_COUNT = 36;  // 内存分级总数 (0-35)

   public:
    using size_class_array_t =
        std::array<size_t, CLASS_COUNT>;  // 内存分级表类型

   public:
    /**
     * @brief 生成内存分级表
     * @return size_class_array_t
     */
    static constexpr size_class_array_t generate_size_classes() {
        size_class_array_t array{};
        size_t size = 1 << ALIGNMENT_SHIFT;

        for (size_t i = 0; i < CLASS_COUNT; ++i) {
            array[i] = size;
            if (i < 16) {
                size += (1 << ALIGNMENT_SHIFT);
            } else {
                size = (size * 5) >> 2;
                size = (size + 7) & ~7;
            }
        }
        return array;
    }

    /**
     * @brief 获取内存大小对应级别
     * @param size 内存大小
     * @return std::size_t 内存级别 (0-35), 超过最大级别返回 CLASS_COUNT
     */
    static constexpr std::size_t size_to_class(std::size_t size) {
        if (size <= (1 << ALIGNMENT_SHIFT))
            return 0;

        constexpr auto table = generate_size_classes();
        std::size_t left = 0, right = CLASS_COUNT;

        while (left < right) {
            auto mid = (left + right) >> 1;
            if (table[mid] < size)
                left = mid + 1;
            else
                right = mid;
        }

        return left < CLASS_COUNT ? left : CLASS_COUNT;
    }

    /**
     * @brief 获取内存级别对应大小
     * @param class_id 内存级别
     * @return std::size_t
     */
    static constexpr std::size_t class_to_size(std::size_t class_id) {
        constexpr auto table = generate_size_classes();
        return class_id < CLASS_COUNT ? table[class_id]
                                      : table[CLASS_COUNT - 1];
    }
};

}  // namespace arena
}  // namespace cascading
