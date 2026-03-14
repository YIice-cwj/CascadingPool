#pragma once
#include <atomic>
#include <initializer_list>
#include <vector>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#elif _WIN32
#include <windows.h>
#include <map>
#endif

namespace cascading {

class cpu_affinity {
   private:
    static thread_local int tls_cpu_id_;
    static thread_local bool tls_initialized_;
    static std::atomic<int> next_cpu_selector_;

   private:
    /**
     * @brief 禁止实例化
     */
    cpu_affinity() = delete;

   public:
    /**
     * @brief 绑定当前线程到指定cpu
     * @param cpu_id cpu id列
     * @return bool, true:绑定成功, false:绑定失败
     */
    static bool bind(int cpu_id);

    /**
     * @brief 绑定当前线程到指定cpu
     * @param cpu_ids cpu id列表
     * @return bool, true:绑定成功, false:绑定失败
     */
    static bool bind(std::initializer_list<int> cpu_ids);

    /**
     * @brief 绑定当前线程到指定cpu
     * @param cpu_ids cpu id列表
     * @return bool, true:绑定成功, false:绑定失败
     */
    static bool bind(std::vector<int> cpu_ids);

    /**
     * @brief 绑定当前线程到指定cpu区间
     * @param cpu_begin cpu 区间开始
     * @param cpu_end cpu 区间结束
     * @return bool, true:绑定成功, false:绑定失败
     */
    static bool bind(int cpu_begin, int cpu_end);

    /**
     * @brief 绑定当前线程到任意cpu
     * @return bool, true:绑定成功, false:绑定失败
     */
    static bool bind_any();

    /**
     * @brief 绑定当前线程到任意cpu
     * @param cpu_num 绑定的cpu数量
     * @return bool, true:绑定成功, false:绑定失败
     */
    static bool bind_any(int cpu_num);

    /**
     * @brief 获取当前线程绑定的cpu id
     * @return std::vector<int>, cpu_ids
     */
    static std::vector<int> get_bind_cpu_id();

    /**
     * @brief 获取系统cpu数量
     * @return int, cpu数量
     */
    static int count();

    /**
     * @brief 获取当前线程的cpu id
     * @return int, 当前线程cpu id
     */
    static int current();
};

}  // namespace cascading