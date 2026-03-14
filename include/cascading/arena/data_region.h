#pragma once
#include <cstddef>
#include <cstdint>
#include "cascading/utils/lock_free_pool.h"

namespace cascading {
namespace arena {

/**
 * @brief 数据区域
 * @details 数据区域是内存中的一段连续区域，用于存储数据
 * @tparam ObjectSize 每个对象的大小
 */
template <std::size_t ObjectSize>
class data_region {
   public:
    using byte_t = unsigned char;
    static constexpr std::size_t OBJECT_SIZE = ObjectSize;

   private:
    alignas(8) byte_t data_[ObjectSize];

   public:
    /**
     * @brief 获取数据区域
     * @return 数据区域
     */
    void* data() { return data_; }

    /**
     * @brief 从数据指针获取数据区域
     * @details 给定数据指针，返回指向该数据区域的指针
     * @param ptr 数据指针
     * @return 数据区域指针
     */
    static data_region* from_data(void* ptr) {
        return reinterpret_cast<data_region*>(static_cast<byte_t*>(ptr) -
                                              offsetof(data_region, data_));
    }
};

}  // namespace arena
}  // namespace cascading
