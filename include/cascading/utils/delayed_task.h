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
 * @brief 一次性定时任务类
 * @details 在指定延迟后执行一次任务。
 */
class delayed_task {
   private:
    // 任务函数
    std::function<void()> task_;
    // 延迟时间（毫秒）
    std::size_t delay_ms_;
    // 线程对象
    std::thread thread_;
    // 取消标志
    std::atomic<bool> cancelled_{false};

   public:
    /**
     * @brief 构造函数
     * @param task 任务函数
     * @param delay_ms 延迟时间（毫秒）
     */
    delayed_task(std::function<void()> task, std::size_t delay_ms)
        : task_(std::move(task)), delay_ms_(delay_ms) {}

    /**
     * @brief 析构函数
     * @details 如果任务还在运行，等待其完成。
     */
    ~delayed_task() {
        cancel();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // 禁止拷贝
    delayed_task(const delayed_task&) = delete;
    delayed_task& operator=(const delayed_task&) = delete;

    // 允许移动
    delayed_task(delayed_task&& other) noexcept
        : task_(std::move(other.task_)),
          delay_ms_(other.delay_ms_),
          thread_(std::move(other.thread_)),
          cancelled_(other.cancelled_.load()) {}

    delayed_task& operator=(delayed_task&& other) noexcept {
        if (this != &other) {
            cancel();
            if (thread_.joinable()) {
                thread_.join();
            }
            task_ = std::move(other.task_);
            delay_ms_ = other.delay_ms_;
            thread_ = std::move(other.thread_);
            cancelled_ = other.cancelled_.load();
        }
        return *this;
    }

    /**
     * @brief 启动延迟任务
     * @details 启动后台线程，在延迟后执行任务。
     */
    void start() {
        if (thread_.joinable()) {
            return;  // 已经启动
        }

        cancelled_ = false;
        thread_ = std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
            if (!cancelled_ && task_) {
                task_();
            }
        });
    }

    /**
     * @brief 取消任务
     * @details 设置取消标志，任务不会执行（如果还未执行）。
     */
    void cancel() { cancelled_ = true; }

    /**
     * @brief 检查是否已取消
     * @return 如果已取消返回 true，否则返回 false
     */
    bool is_cancelled() const { return cancelled_.load(); }

    /**
     * @brief 等待任务完成
     * @details 阻塞当前线程直到任务完成或超时。
     * @param timeout_ms 超时时间（毫秒），0 表示无限等待
     * @return 如果任务完成返回 true，超时返回 false
     */
    bool wait(std::size_t timeout_ms = 0) {
        if (!thread_.joinable()) {
            return true;
        }

        if (timeout_ms == 0) {
            thread_.join();
            return true;
        } else {
            // 使用条件变量实现超时等待
            std::mutex mutex;
            std::condition_variable cv;
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this]() { return !thread_.joinable(); });
        }
    }
};

}  // namespace utils
}  // namespace cascading