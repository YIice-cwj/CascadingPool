#pragma once
#include <array>
#include <memory>
#include "cascading/arena/size_class.h"
#include "cascading/arena/size_class_table.h"
#include "cascading/extent_tree/extent_manager.h"

namespace cascading {
namespace arena {

/**
 * @brief 内存分配器类 - 单例模式
 * @details 使用类型擦除避免模板膨胀，运行时初始化
 *          使用 placement new 模式，单例永不析构
 */
class arena {
   public:
    constexpr static std::size_t CHUNK_SIZE = 4 * 1024 * 1024;
    constexpr static std::size_t CHUNK_MAX_COUNT = 128;
    constexpr static std::size_t SLAB_SIZE = 256 * 1024;
    constexpr static std::size_t SLABS_PER_GROUP = 16;
    constexpr static std::size_t CLASS_COUNT = size_class_table::CLASS_COUNT;

    using vtable_t = std::array<size_class_base, CLASS_COUNT>;

   private:
    // 大小类数组，每个元素对应一个大小类
    vtable_t size_classes_;
    // size_class 对象存储（使用统一存储类型避免模板膨胀）
    std::array<std::unique_ptr<char[]>, CLASS_COUNT> size_class_storage_;
    // 是否已初始化
    std::atomic<bool> initialized_{false};

   private:
    /**
     * @brief 初始化所有大小类
     */
    void initialize_all_size_classes();

    /**
     * @brief 辅助：初始化单个大小类（运行时确定大小）
     */
    using init_fn_t = void (*)(void* storage, size_class_base& vtable);

    /**
     * @brief 获取初始化函数指针（运行时确定大小）
     * @param object_size 对象大小
     * @return init_fn_t 初始化函数指针
     */
    static init_fn_t get_initializer(std::size_t object_size);

   private:
    /**
     * @brief 禁止拷贝构造
     */
    arena(const arena&) = delete;
    /**
     * @brief 禁止拷贝赋值
     */
    arena& operator=(const arena&) = delete;

    /**
     * @brief 禁止移动构造
     */
    arena(arena&&) = delete;
    /**
     * @brief 禁止移动赋值
     */
    arena& operator=(arena&&) = delete;

   private:
    /**
     * @brief 私有构造函数
     */
    arena();

   public:
    /**
     * @brief 析构函数
     */
    ~arena();

    /**
     * @brief 初始化内存分配器
     * @return 是否初始化成功
     */
    bool initialize();

    /**
     * @brief 分配一个对象
     * @param size 要分配的对象大小
     * @return void* 指向分配的对象的指针，失败返回 nullptr
     */
    void* allocate(std::size_t size);

    /**
     * @brief 释放一个对象
     * @param ptr 指向要释放的对象的指针
     * @param size 对象大小（用于计算大小类，默认8字节）
     */
    void deallocate(void* ptr, std::size_t size = 8);

    /**
     * @brief 批量分配内存（指定大小）
     * @param ptrs 输出数组，存储分配的指针
     * @param n 请求分配的数量
     * @param size 每个对象的大小（默认8字节）
     * @return int 实际分配的数量
     */
    int allocate_batch(void** ptrs, int n, std::size_t size = 8);

    /**
     * @brief 批量释放内存（指定大小）
     * @param ptrs 指向要释放的对象指针数组
     * @param n 释放的数量
     * @param size 每个对象的大小（默认8字节）
     */
    void deallocate_batch(void** ptrs, int n, std::size_t size = 8);

    /**
     * @brief 批量分配内存（指定大小类索引）
     * @param ptrs 输出数组，存储分配的指针
     * @param n 请求分配的数量
     * @param class_index 大小类索引
     * @return int 实际分配的数量
     */
    int allocate_batch_by_class(void** ptrs, int n, std::size_t class_index);

    /**
     * @brief 批量释放内存（指定大小类索引）
     * @param ptrs 指向要释放的对象指针数组
     * @param n 释放的数量
     * @param class_index 大小类索引
     */
    void deallocate_batch_by_class(void** ptrs, int n, std::size_t class_index);

    /**
     * @brief 获取 arena 单例实例
     * @return arena& arena 实例引用
     */
    static arena& get_instance();
};

}  // namespace arena
}  // namespace cascading
