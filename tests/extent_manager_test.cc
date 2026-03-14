#include "cascading/extent_tree/extent_manager.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>
#include <vector>
#include "test_framework.h"

using namespace cascading::extent_tree;

// ==================== 基础功能测试 ====================

TEST(ExtentManager, BasicAllocate) {
    extent_manager& manager = extent_manager::get_instance();

    // 分配 4KB 内存
    void* ptr = manager.allocate(4 * 1024);
    ASSERT_NOT_NULL(ptr);

    // 使用内存
    memset(ptr, 0xAB, 4 * 1024);

    // 释放内存
    manager.deallocate(ptr, 4 * 1024);
}

TEST(ExtentManager, AllocateDifferentSizes) {
    extent_manager& manager = extent_manager::get_instance();

    // 测试不同大小的分配
    std::size_t sizes[] = {1024,      4 * 1024,  8 * 1024,
                           16 * 1024, 32 * 1024, 64 * 1024};

    for (std::size_t size : sizes) {
        void* ptr = manager.allocate(size);
        ASSERT_NOT_NULL(ptr);

        // 写入数据验证
        memset(ptr, 0xCD, size);

        manager.deallocate(ptr, size);
    }
}

TEST(ExtentManager, AllocateZeroSize) {
    extent_manager& manager = extent_manager::get_instance();

    void* ptr = manager.allocate(0);
    ASSERT_NULL(ptr);
}

TEST(ExtentManager, DeallocateNullptr) {
    extent_manager& manager = extent_manager::get_instance();

    // 释放空指针不应崩溃
    manager.deallocate(nullptr, 1024);
}

TEST(ExtentManager, StaticMethods) {
    // 测试静态方法
    void* ptr = extent_manager::static_allocate(8 * 1024);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xEF, 8 * 1024);

    extent_manager::static_deallocate(ptr, 8 * 1024);
}

// ==================== 单线程压力测试 ====================

TEST(ExtentManager, SingleThreadStressTest) {
    extent_manager& manager = extent_manager::get_instance();

    const int iterations = 100;
    const int allocations_per_iteration = 50;

    std::vector<void*> ptrs;
    ptrs.reserve(allocations_per_iteration);

    for (int iter = 0; iter < iterations; ++iter) {
        // 分配阶段
        for (int i = 0; i < allocations_per_iteration; ++i) {
            std::size_t size = (4 + (i % 8)) * 1024;  // 4KB ~ 11KB
            void* ptr = manager.allocate(size);
            if (ptr) {
                memset(ptr, i & 0xFF, size);
                ptrs.push_back(ptr);
            }
        }

        // 释放阶段 - 释放一半
        size_t release_count = ptrs.size() / 2;
        for (size_t i = 0; i < release_count; ++i) {
            std::size_t size = (4 + (i % 8)) * 1024;
            manager.deallocate(ptrs[i], size);
        }

        // 移除已释放的指针
        ptrs.erase(ptrs.begin(), ptrs.begin() + release_count);
    }

    // 清理剩余
    for (size_t i = 0; i < ptrs.size(); ++i) {
        std::size_t size = (4 + (i % 8)) * 1024;
        manager.deallocate(ptrs[i], size);
    }
}

// ==================== 多线程并发测试 ====================

TEST(ExtentManager, MultiThreadConcurrentAllocate) {
    extent_manager& manager = extent_manager::get_instance();

    const int num_threads = 8;
    const int allocations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<> size_dist(1, 32);

            for (int i = 0; i < allocations_per_thread; ++i) {
                std::size_t size = size_dist(rng) * 1024;
                void* ptr = manager.allocate(size);

                if (ptr) {
                    // 写入线程标识
                    memset(ptr, t, size);
                    success_count++;

                    // 立即释放
                    manager.deallocate(ptr, size);
                } else {
                    fail_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(success_count + fail_count, num_threads * allocations_per_thread);
}

TEST(ExtentManager, MultiThreadMixedOperations) {
    extent_manager& manager = extent_manager::get_instance();

    const int num_threads = 8;
    const int operations_per_thread = 200;

    std::vector<std::thread> threads;
    std::atomic<int> allocate_count{0};
    std::atomic<int> deallocate_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<> size_dist(1, 16);
            std::uniform_int_distribution<> op_dist(0, 2);

            std::vector<std::pair<void*, std::size_t>> local_ptrs;
            local_ptrs.reserve(operations_per_thread);

            for (int i = 0; i < operations_per_thread; ++i) {
                int op = op_dist(rng);

                if (op == 0 || local_ptrs.empty()) {
                    // 分配操作
                    std::size_t size = size_dist(rng) * 1024;
                    void* ptr = manager.allocate(size);
                    if (ptr) {
                        memset(ptr, t, size);
                        local_ptrs.push_back({ptr, size});
                        allocate_count++;
                    }
                } else if (op == 1 && !local_ptrs.empty()) {
                    // 释放操作
                    std::uniform_int_distribution<> idx_dist(
                        0, local_ptrs.size() - 1);
                    size_t idx = idx_dist(rng);

                    manager.deallocate(local_ptrs[idx].first,
                                       local_ptrs[idx].second);
                    local_ptrs.erase(local_ptrs.begin() + idx);
                    deallocate_count++;
                }
                // op == 2: 什么都不做，模拟空闲
            }

            // 清理本地剩余的指针
            for (auto& p : local_ptrs) {
                manager.deallocate(p.first, p.second);
                deallocate_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证分配和释放数量匹配
    ASSERT_EQ(allocate_count, deallocate_count);
}

TEST(ExtentManager, MultiThreadHighContention) {
    extent_manager& manager = extent_manager::get_instance();

    const int num_threads = 16;
    const int operations_per_thread = 500;

    std::vector<std::thread> threads;
    std::atomic<int> total_operations{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            // 每个线程快速分配和释放相同大小的内存
            for (int i = 0; i < operations_per_thread; ++i) {
                void* ptr = manager.allocate(4 * 1024);
                if (ptr) {
                    memset(ptr, t, 4 * 1024);
                    manager.deallocate(ptr, 4 * 1024);
                    total_operations++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(total_operations, num_threads * operations_per_thread);
}

TEST(ExtentManager, MultiThreadRandomSizeAllocation) {
    extent_manager& manager = extent_manager::get_instance();

    const int num_threads = 8;
    const int allocations_per_thread = 100;

    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            // 随机大小：1KB ~ 64KB
            std::uniform_int_distribution<> size_dist(1, 64);

            std::vector<std::pair<void*, std::size_t>> local_ptrs;

            for (int i = 0; i < allocations_per_thread; ++i) {
                std::size_t size = size_dist(rng) * 1024;
                void* ptr = manager.allocate(size);

                if (ptr) {
                    // 写入验证数据
                    uint8_t pattern = static_cast<uint8_t>((t * 17 + i) & 0xFF);
                    memset(ptr, pattern, size);

                    // 验证数据
                    uint8_t* data = static_cast<uint8_t*>(ptr);
                    bool valid = true;
                    for (std::size_t j = 0; j < size; j += 1024) {
                        if (data[j] != pattern) {
                            valid = false;
                            break;
                        }
                    }
                    ASSERT_TRUE(valid);

                    local_ptrs.push_back({ptr, size});
                }
            }

            // 释放所有
            for (auto& p : local_ptrs) {
                manager.deallocate(p.first, p.second);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

// ==================== 内存压力测试 ====================

TEST(ExtentManager, MemoryPressureTest) {
    extent_manager& manager = extent_manager::get_instance();

    const int num_threads = 4;
    const int iterations = 50;

    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::vector<std::pair<void*, std::size_t>> local_ptrs;

            for (int iter = 0; iter < iterations; ++iter) {
                // 分配大量内存
                for (int i = 0; i < 20; ++i) {
                    std::size_t size = (4 + i) * 1024;
                    void* ptr = manager.allocate(size);
                    if (ptr) {
                        memset(ptr, t, size);
                        local_ptrs.push_back({ptr, size});
                    }
                }

                // 释放一半
                size_t half = local_ptrs.size() / 2;
                for (size_t i = 0; i < half; ++i) {
                    manager.deallocate(local_ptrs[i].first,
                                       local_ptrs[i].second);
                }
                local_ptrs.erase(local_ptrs.begin(), local_ptrs.begin() + half);
            }

            // 清理
            for (auto& p : local_ptrs) {
                manager.deallocate(p.first, p.second);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 触发回收
    manager.reclaim();

    // 获取内存压力
    double pressure = manager.get_memory_pressure();
    ASSERT_GE(pressure, 0.0);
    ASSERT_LE(pressure, 1.0);
}

// ==================== 回收机制测试 ====================

TEST(ExtentManager, ReclaimTest) {
    extent_manager& manager = extent_manager::get_instance();

    // 分配并释放一些内存，使其进入 dirty_tree
    std::vector<void*> ptrs;
    for (int i = 0; i < 20; ++i) {
        void* ptr = manager.allocate(8 * 1024);
        if (ptr) {
            ptrs.push_back(ptr);
        }
    }

    // 释放所有内存
    for (void* ptr : ptrs) {
        manager.deallocate(ptr, 8 * 1024);
    }

    // 手动触发回收
    manager.reclaim();

    // 验证可以再次分配
    void* new_ptr = manager.allocate(8 * 1024);
    ASSERT_NOT_NULL(new_ptr);
    manager.deallocate(new_ptr, 8 * 1024);
}

TEST(ExtentManager, StaticReclaim) {
    // 测试静态回收方法
    extent_manager::static_reclaim();
}

// ==================== 过期时间测试 ====================

TEST(ExtentManager, ExpireTimeTest) {
    extent_manager& manager = extent_manager::get_instance();

    // 获取过期时间
    std::size_t dirty_expire = manager.get_dirty_expire_time();
    std::size_t muzzy_expire = manager.get_muzzy_expire_time();
    std::size_t retained_expire = manager.get_retained_expire_time();

    // 验证过期时间合理
    ASSERT_GT(dirty_expire, 0);
    ASSERT_GT(muzzy_expire, 0);
    ASSERT_GT(retained_expire, 0);

    // retained 应该最长
    ASSERT_GE(retained_expire, muzzy_expire);
    ASSERT_GE(muzzy_expire, dirty_expire);
}

// ==================== 生产者-消费者模式测试 ====================

TEST(ExtentManager, ProducerConsumerPattern) {
    extent_manager& manager = extent_manager::get_instance();

    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 100;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // 使用简单的共享队列（实际应该用无锁队列）
    std::vector<std::pair<void*, std::size_t>> queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool done = false;

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // 启动生产者
    for (int t = 0; t < num_producers; ++t) {
        producers.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<> size_dist(4, 16);

            for (int i = 0; i < items_per_producer; ++i) {
                std::size_t size = size_dist(rng) * 1024;
                void* ptr = manager.allocate(size);
                if (ptr) {
                    memset(ptr, t, size);
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        queue.push_back({ptr, size});
                    }
                    cv.notify_one();
                    produced++;
                }
            }
        });
    }

    // 启动消费者
    for (int t = 0; t < num_consumers; ++t) {
        consumers.emplace_back([&, t]() {
            while (true) {
                std::pair<void*, std::size_t> item;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    cv.wait(lock, [&]() { return !queue.empty() || done; });

                    if (queue.empty() && done) {
                        break;
                    }

                    if (!queue.empty()) {
                        item = queue.back();
                        queue.pop_back();
                    } else {
                        continue;
                    }
                }

                // 验证数据
                uint8_t* data = static_cast<uint8_t*>(item.first);
                ASSERT_TRUE(*data < num_producers);

                // 释放
                manager.deallocate(item.first, item.second);
                consumed++;
            }
        });
    }

    // 等待生产者完成
    for (auto& t : producers) {
        t.join();
    }

    // 通知消费者结束
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        done = true;
    }
    cv.notify_all();

    // 等待消费者完成
    for (auto& t : consumers) {
        t.join();
    }

    ASSERT_EQ(produced, consumed);
}

// ==================== 边界条件测试 ====================

TEST(ExtentManager, LargeAllocation) {
    extent_manager& manager = extent_manager::get_instance();

    // 超过 MAX_BLOCK_SIZE (32KB) 的分配
    void* ptr = manager.allocate(64 * 1024);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xAA, 64 * 1024);
    manager.deallocate(ptr, 64 * 1024);
}

TEST(ExtentManager, VeryLargeAllocation) {
    extent_manager& manager = extent_manager::get_instance();

    // 1MB 分配
    void* ptr = manager.allocate(1024 * 1024);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xBB, 1024 * 1024);
    manager.deallocate(ptr, 1024 * 1024);
}

TEST(ExtentManager, RapidAllocateDeallocate) {
    extent_manager& manager = extent_manager::get_instance();

    // 快速连续分配释放
    for (int i = 0; i < 1000; ++i) {
        void* ptr = manager.allocate(4 * 1024);
        if (ptr) {
            manager.deallocate(ptr, 4 * 1024);
        }
    }
}

// ==================== 主函数 ====================

int main() {
    return test::run_all_tests();
}
