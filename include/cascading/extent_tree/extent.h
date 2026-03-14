#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include "cascading/utils/compiler.h"

namespace cascading {
namespace extent_tree {

struct extent {
    using unique_ptr = std::unique_ptr<extent>;
    /**
     * @brief 页状态枚举类
     * @details 表示内存页的不同状态，包括正在使用、脏页、模糊页和保留状态。
     */
    enum class state_page {
        in_use = 0,   // 正在使用
        dirty = 1,    // 脏页
        muzzy = 2,    // 模糊页
        retained = 3  // 保留页
    };

    /**
     * @brief 页类型枚举类
     * @details 表示内存页的不同类型，包括原始页、主页和分页。
     */
    enum class page_type {
        original,  // 原始页 - 最初分配的完整页面
        primary,   // 主页 - 当前使用的主要页面
        split      // 分页 - 从原始页分割出来的子页面
    };

    void* addr = nullptr;                               // 当前起始地址
    std::size_t size = 0;                               // 当前大小
    std::atomic<state_page> state{state_page::in_use};  // 状态
    std::atomic<page_type> type{page_type::original};   // 页类型
    std::size_t original_size = 0;                      // 原始内存大小
    std::size_t expire_time = 0;                        // 到期时间戳（毫秒）
    std::size_t arena_id = 0;                           // 所属Arena ID

    /**
     * @brief 构造函数
     * @details 初始化 extent 结构体，将状态设为 InUse，到期时间设为 0，Arena ID
     * 设为 0。
     * @param addr 内存起始地址
     * @param size 内存大小
     * @param state 状态
     */
    extent(void* addr = 0,
           std::size_t size = 0,
           state_page state = state_page::in_use,
           page_type type = page_type::original)
        : addr(addr),
          size(size),
          state(state),
          type(type),
          original_size(0),
          expire_time(0),
          arena_id(0) {}

    /**
     * @brief 移动构造函数
     * @details 将 other 移动到当前 extent 中，更新状态、到期时间和 Arena ID。
     * @param other 要移动的 extent 引用
     */
    extent(extent&& other) noexcept
        : addr(other.addr),
          size(other.size),
          state(other.state.load(std::memory_order_acquire)),
          type(other.type.load(std::memory_order_acquire)),
          original_size(other.original_size),
          expire_time(other.expire_time),
          arena_id(other.arena_id) {
        other.addr = nullptr;
        other.size = 0;
        other.state.store(state_page::in_use, std::memory_order_release);
        other.type.store(page_type::original, std::memory_order_release);
        other.original_size = 0;
        other.expire_time = 0;
        other.arena_id = 0;
    }

    ~extent() {
        if ((addr != nullptr &&
             type.load(std::memory_order_acquire) == page_type::split)) {
            release_memory(addr, size);
        }
    }

    /**
     * @brief 移动赋值运算符
     * @details 将 other 移动到当前 extent 中，更新状态、到期时间和 Arena ID。
     * @param other 要移动的 extent 引用
     * @return 当前 extent 引用
     */
    extent& operator=(extent&& other) noexcept {
        if (this != &other) {
            addr = other.addr;
            size = other.size;
            state.store(other.state.load(std::memory_order_acquire),
                        std::memory_order_release);
            type.store(other.type.load(std::memory_order_acquire),
                       std::memory_order_release);
            original_size = other.original_size;
            expire_time = other.expire_time;
            arena_id = other.arena_id;
            other.addr = nullptr;
            other.size = 0;
            other.state.store(state_page::in_use, std::memory_order_release);
            other.type.store(page_type::original, std::memory_order_release);
            other.original_size = 0;
            other.expire_time = 0;
            other.arena_id = 0;
        }
        return *this;
    }

    /**
     * @brief 复制构造函数
     * @details 禁用复制构造函数，防止通过值传递或复制初始化 extent。
     * @param other 要复制的 extent 引用
     */
    extent(const extent&) = delete;

    /**
     * @brief 复制赋值运算符
     * @details 禁用复制赋值运算符，防止通过赋值操作符复制 extent。
     * @param other 要复制的 extent 引用
     * @return 当前 extent 引用
     */
    extent& operator=(const extent&) = delete;
};

constexpr const char* state_page_names[] = {"InUse", "Dirty", "Fuzzy",
                                            "Retained"};

/**
 * @brief extent 状态转换成字符串
 * @param state extent 状态
 * @return 状态对应的字符串
 */
inline const char* state_page_to_string(extent::state_page state) {
    return state_page_names[static_cast<int>(state)];
}

}  // namespace extent_tree
}  // namespace cascading