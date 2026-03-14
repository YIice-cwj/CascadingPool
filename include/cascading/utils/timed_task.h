#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace cascading {
namespace utils {

/**
 * @brief 定时任务类
 * @details 用于在指定时间间隔内循环执行任务，支持启动、停止和动态修改间隔。
 */
class timed_task {
   private:
    // 任务函数
    std::function<void()> task_;
    // 时间间隔（毫秒）
    std::atomic<std::size_t> interval_ms_;
    // 运行标志
    std::atomic<bool> running_{false};
    // 线程对象
    std::thread thread_;
    // 互斥锁
    std::mutex mutex_;
    // 条件变量，用于唤醒线程
    std::condition_variable cv_;
    // 停止标志
    std::atomic<bool> stop_requested_{false};

   private:
    /**
     * @brief 禁止拷贝构造函数
     */
    timed_task(const timed_task&) = delete;

    /**
     * @brief 禁止赋值运算符
     */
    timed_task& operator=(const timed_task&) = delete;

   public:
    /**
     * @brief 默认构造函数
     */
    timed_task() = default;

    /**
     * @brief 构造函数
     * @details 初始化定时任务，设置任务函数和时间间隔。
     * @param task 任务函数
     * @param interval_ms 时间间隔（毫秒）
     */
    timed_task(std::function<void()> task, std::size_t interval_ms)
        : task_(std::move(task)), interval_ms_(interval_ms) {}

    /**
     * @brief 析构函数
     * @details 停止任务并等待线程结束。
     */
    ~timed_task() { stop(); }

    /**
     * @brief 移动构造函数
     * @details 移动另一个定时任务的资源到当前对象。
     * @param other 要移动的定时任务
     */
    timed_task(timed_task&& other) noexcept
        : task_(std::move(other.task_)),
          interval_ms_(other.interval_ms_.load()),
          running_(other.running_.load()),
          thread_(std::move(other.thread_)),
          stop_requested_(other.stop_requested_.load()) {}

    /**
     * @brief 移动赋值运算符
     * @details 移动另一个定时任务的资源到当前对象。
     * @param other 要移动的定时任务
     * @return 当前对象的引用
     */
    timed_task& operator=(timed_task&& other) noexcept {
        if (this != &other) {
            stop();
            task_ = std::move(other.task_);
            interval_ms_ = other.interval_ms_.load();
            running_ = other.running_.load();
            thread_ = std::move(other.thread_);
            stop_requested_ = other.stop_requested_.load();
        }
        return *this;
    }

    /**
     * @brief 启动定时任务
     * @details 启动后台线程，按指定间隔执行任务。
     */
    void start() {
        if (running_.exchange(true)) {
            return;  // 已经在运行
        }

        stop_requested_ = false;
        thread_ = std::thread([this]() {
            while (!stop_requested_) {
                auto start_time = std::chrono::steady_clock::now();

                // 执行任务
                if (task_) {
                    task_();
                }

                // 计算下一次执行时间
                auto next_time =
                    start_time + std::chrono::milliseconds(interval_ms_.load());

                // 等待直到下一次执行时间或停止信号
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_until(lock, next_time,
                               [this]() { return stop_requested_.load(); });
            }
        });
    }

    /**
     * @brief 停止定时任务
     * @details 发送停止信号并等待线程结束。
     */
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }

        stop_requested_ = true;
        cv_.notify_all();

        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /**
     * @brief 检查是否正在运行
     * @return 如果正在运行返回 true，否则返回 false
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    /**
     * @brief 设置时间间隔
     * @details 设置定时任务的执行间隔（毫秒）。
     * @param interval_ms 新的时间间隔（毫秒）
     */
    void set_interval(std::size_t interval_ms) { interval_ms_ = interval_ms; }

    /**
     * @brief 获取时间间隔
     * @details 获取当前定时任务的执行间隔（毫秒）。
     * @return 当前时间间隔（毫秒）
     */
    std::size_t get_interval() const {
        return interval_ms_.load(std::memory_order_acquire);
    }

    /**
     * @brief 设置任务函数
     * @param task 新的任务函数
     */
    void set_task(std::function<void()> task) { task_ = std::move(task); }

    /**
     * @brief 立即执行一次任务
     * @details 在当前线程立即执行任务（如果设置了任务函数）。
     */
    void execute_now() {
        if (task_) {
            task_();
        }
    }
};

}  // namespace utils
}  // namespace cascading
