#include "cascading/thread_cache/thread_cache_manager.h"
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>
#include "test_framework.h"

using namespace cascading::thread_cache;

// ==================== 基础功能测试 ====================

TEST(ThreadCacheManager, BasicAllocateAndDeallocate) {
    auto& manager = thread_cache_manager::get_instance();

    // 分配内存
    void* ptr = manager.allocate(64);
    ASSERT_NOT_NULL(ptr);

    // 写入数据
    memset(ptr, 0xAB, 64);

    // 释放内存
    manager.deallocate(ptr, 64);
}

TEST(ThreadCacheManager, AllocateDifferentSizes) {
    auto& manager = thread_cache_manager::get_instance();

    // 测试不同大小的分配
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    void* ptrs[9];

    for (int i = 0; i < 9; ++i) {
        ptrs[i] = manager.allocate(sizes[i]);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, sizes[i]);
    }

    for (int i = 0; i < 9; ++i) {
        manager.deallocate(ptrs[i], sizes[i]);
    }
}

TEST(ThreadCacheManager, AllocateZeroSize) {
    auto& manager = thread_cache_manager::get_instance();

    // 分配 0 字节应该返回 nullptr
    void* ptr = manager.allocate(0);
    ASSERT_NULL(ptr);
}

TEST(ThreadCacheManager, DeallocateNullptr) {
    auto& manager = thread_cache_manager::get_instance();

    // 释放空指针不应崩溃
    manager.deallocate(nullptr, 64);
}

TEST(ThreadCacheManager, MultipleAllocateDeallocate) {
    auto& manager = thread_cache_manager::get_instance();

    const int count = 100;
    void* ptrs[count];

    // 分配多个对象
    for (int i = 0; i < count; ++i) {
        ptrs[i] = manager.allocate(64);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, 64);
    }

    // 释放所有对象
    for (int i = 0; i < count; ++i) {
        manager.deallocate(ptrs[i], 64);
    }
}

// ==================== 批量操作测试 ====================

TEST(ThreadCacheManager, BatchAllocateAndDeallocate) {
    auto& manager = thread_cache_manager::get_instance();

    const int count = 32;
    void* ptrs[count];

    // 批量分配
    int allocated = manager.allocate_batch(ptrs, count, 64);
    ASSERT_EQ(count, allocated);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, 64);
    }

    // 批量释放
    manager.deallocate_batch(ptrs, allocated, 64);
}

TEST(ThreadCacheManager, BatchAllocatePartial) {
    auto& manager = thread_cache_manager::get_instance();

    const int count = 1000;
    void* ptrs[count];

    // 批量分配大量对象
    int allocated = manager.allocate_batch(ptrs, count, 64);
    ASSERT_GT(allocated, 0);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
    }

    manager.deallocate_batch(ptrs, allocated, 64);
}

// ==================== 多线程测试 ====================

TEST(ThreadCacheManager, MultiThreadAllocate) {
    auto& manager = thread_cache_manager::get_instance();

    const int num_threads = 4;
    const int allocations_per_thread = 100;

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(
            [&manager, &success_count, allocations_per_thread, t]() {
                for (int i = 0; i < allocations_per_thread; ++i) {
                    void* ptr = manager.allocate(64);
                    if (ptr != nullptr) {
                        memset(ptr, t, 64);
                        success_count++;
                        manager.deallocate(ptr, 64);
                    }
                }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * allocations_per_thread, success_count.load());
}

TEST(ThreadCacheManager, MultiThreadBatchAllocate) {
    auto& manager = thread_cache_manager::get_instance();

    const int num_threads = 4;
    const int batch_size = 32;
    const int iterations = 50;

    std::atomic<int> total_allocated{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(
            [&manager, &total_allocated, batch_size, iterations]() {
                for (int i = 0; i < iterations; ++i) {
                    void* ptrs[batch_size];
                    int n = manager.allocate_batch(ptrs, batch_size, 64);
                    total_allocated += n;
                    manager.deallocate_batch(ptrs, n, 64);
                }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * iterations * batch_size, total_allocated.load());
}

TEST(ThreadCacheManager, ThreadCacheIsolation) {
    auto& manager = thread_cache_manager::get_instance();

    const int num_threads = 4;
    std::vector<thread_cache*> caches(num_threads, nullptr);
    std::mutex mutex;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, &manager, &caches, &mutex]() {
            // 触发缓存创建
            void* ptr = manager.allocate(8);
            manager.deallocate(ptr, 8);

            // 记录当前线程的缓存
            thread_cache* cache = manager.get_current_cache();
            {
                std::lock_guard<std::mutex> lock(mutex);
                caches[t] = cache;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证每个线程有自己的缓存
    for (int i = 0; i < num_threads; ++i) {
        ASSERT_NOT_NULL(caches[i]);
        for (int j = i + 1; j < num_threads; ++j) {
            ASSERT_NE(caches[i], caches[j]);
        }
    }
}

// ==================== 缓存管理测试 ====================

TEST(ThreadCacheManager, GetCacheCount) {
    auto& manager = thread_cache_manager::get_instance();

    // 清理当前线程缓存（如果存在）
    manager.cleanup_current_thread();

    std::size_t initial_count = manager.get_cache_count();

    // 触发缓存创建
    void* ptr = manager.allocate(8);
    manager.deallocate(ptr, 8);

    std::size_t after_alloc = manager.get_cache_count();
    ASSERT_EQ(initial_count + 1, after_alloc);
}

TEST(ThreadCacheManager, CleanupCurrentThread) {
    auto& manager = thread_cache_manager::get_instance();

    // 触发缓存创建
    void* ptr = manager.allocate(8);
    manager.deallocate(ptr, 8);

    // 确认缓存存在
    ASSERT_NOT_NULL(manager.get_current_cache());

    // 清理当前线程缓存
    manager.cleanup_current_thread();

    // 确认缓存已清理
    ASSERT_NULL(manager.get_current_cache());
}

// ==================== 压力测试 ====================

TEST(ThreadCacheManager, StressTest) {
    auto& manager = thread_cache_manager::get_instance();

    const int num_threads = 8;
    const int iterations = 1000;
    const int max_size = 1024;

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(
            [&manager, &success_count, iterations, max_size, t]() {
                for (int i = 0; i < iterations; ++i) {
                    std::size_t size = 8 + (i % max_size);
                    void* ptr = manager.allocate(size);
                    if (ptr != nullptr) {
                        memset(ptr, t, size);
                        success_count++;
                        manager.deallocate(ptr, size);
                    }
                }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * iterations, success_count.load());
}

// ==================== 性能测试 ====================

#include <chrono>
#include <iomanip>

// 辅助函数：测量时间
inline double measure_time_ms(std::function<void()> func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

TEST(ThreadCacheManagerPerf, SingleThreadAllocateDeallocate) {
    auto& manager = thread_cache_manager::get_instance();

    const int iterations = 1000000;
    const std::size_t size = 64;

    double elapsed = measure_time_ms([&]() {
        for (int i = 0; i < iterations; ++i) {
            void* ptr = manager.allocate(size);
            manager.deallocate(ptr, size);
        }
    });

    double ops_per_sec = (iterations * 2) / (elapsed / 1000.0);
    std::cout << "\n  [性能] 单线程分配/释放 " << iterations
              << " 次: " << std::fixed << std::setprecision(2) << elapsed
              << " ms (" << std::setprecision(0) << ops_per_sec << " ops/sec)"
              << std::endl;
}

TEST(ThreadCacheManagerPerf, SingleThreadBatchOperations) {
    auto& manager = thread_cache_manager::get_instance();

    const int iterations = 100000;
    const int batch_size = 32;
    const std::size_t size = 64;

    double elapsed = measure_time_ms([&]() {
        for (int i = 0; i < iterations; ++i) {
            void* ptrs[batch_size];
            int n = manager.allocate_batch(ptrs, batch_size, size);
            manager.deallocate_batch(ptrs, n, size);
        }
    });

    double ops_per_sec = (iterations * batch_size * 2) / (elapsed / 1000.0);
    std::cout << "\n  [性能] 单线程批量操作 " << iterations << " 批次 (每批"
              << batch_size << "个): " << std::fixed << std::setprecision(2)
              << elapsed << " ms (" << std::setprecision(0) << ops_per_sec
              << " ops/sec)" << std::endl;
}

TEST(ThreadCacheManagerPerf, MultiThreadContention) {
    auto& manager = thread_cache_manager::get_instance();

    const int num_threads = 8;
    const int iterations_per_thread = 100000;
    const std::size_t size = 64;

    double elapsed = measure_time_ms([&]() {
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < iterations_per_thread; ++i) {
                    void* ptr = manager.allocate(size);
                    manager.deallocate(ptr, size);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    });

    int total_ops = num_threads * iterations_per_thread * 2;
    double ops_per_sec = total_ops / (elapsed / 1000.0);
    std::cout << "\n  [性能] " << num_threads << " 线程并发操作 (每线程"
              << iterations_per_thread << "次): " << std::fixed
              << std::setprecision(2) << elapsed << " ms ("
              << std::setprecision(0) << ops_per_sec << " ops/sec)"
              << std::endl;
}

TEST(ThreadCacheManagerPerf, DifferentSizes) {
    auto& manager = thread_cache_manager::get_instance();

    const int iterations = 100000;
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};

    std::cout << "\n  [性能] 不同大小分配性能:" << std::endl;
    for (std::size_t size : sizes) {
        double elapsed = measure_time_ms([&]() {
            for (int i = 0; i < iterations; ++i) {
                void* ptr = manager.allocate(size);
                manager.deallocate(ptr, size);
            }
        });

        double ops_per_sec = (iterations * 2) / (elapsed / 1000.0);
        std::cout << "    大小 " << std::setw(4) << size
                  << " bytes: " << std::fixed << std::setprecision(2)
                  << std::setw(8) << elapsed << " ms (" << std::setw(12)
                  << std::setprecision(0) << ops_per_sec << " ops/sec)"
                  << std::endl;
    }
}

TEST(ThreadCacheManagerPerf, MixedWorkload) {
    auto& manager = thread_cache_manager::get_instance();

    const int num_threads = 4;
    const int iterations_per_thread = 50000;

    double elapsed = measure_time_ms([&]() {
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < iterations_per_thread; ++i) {
                    // 混合不同大小
                    std::size_t size = 8 << (i % 8);
                    void* ptr = manager.allocate(size);
                    // 模拟使用
                    if (ptr) {
                        memset(ptr, t, size);
                    }
                    manager.deallocate(ptr, size);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    });

    int total_ops = num_threads * iterations_per_thread * 2;
    double ops_per_sec = total_ops / (elapsed / 1000.0);
    std::cout << "\n  [性能] 混合工作负载 (" << num_threads << "线程, 每线程"
              << iterations_per_thread << "次, 混合大小): " << std::fixed
              << std::setprecision(2) << elapsed << " ms ("
              << std::setprecision(0) << ops_per_sec << " ops/sec)"
              << std::endl;
}

TEST(ThreadCacheManagerPerf, ScalabilityTest) {
    auto& manager = thread_cache_manager::get_instance();

    const int iterations_per_thread = 100000;
    const std::size_t size = 64;

    std::cout << "\n  [性能] 可扩展性测试 (固定每线程工作量):" << std::endl;

    for (int num_threads = 1; num_threads <= 8; num_threads *= 2) {
        double elapsed = measure_time_ms([&]() {
            std::vector<std::thread> threads;
            for (int t = 0; t < num_threads; ++t) {
                threads.emplace_back([&]() {
                    for (int i = 0; i < iterations_per_thread; ++i) {
                        void* ptr = manager.allocate(size);
                        manager.deallocate(ptr, size);
                    }
                });
            }
            for (auto& t : threads) {
                t.join();
            }
        });

        int total_ops = num_threads * iterations_per_thread * 2;
        double ops_per_sec = total_ops / (elapsed / 1000.0);
        double speedup = (num_threads == 1) ? 1.0 : ops_per_sec / ops_per_sec;

        std::cout << "    " << num_threads << " 线程: " << std::fixed
                  << std::setprecision(2) << elapsed << " ms ("
                  << std::setprecision(0) << ops_per_sec << " ops/sec)"
                  << std::endl;
    }
}

// ==================== 主函数 ====================

int main() {
    // 初始化 arena
    cascading::arena::arena::get_instance().initialize();

    // 运行测试
    int result = test::run_all_tests();

    // 清理
    thread_cache_manager::get_instance().shutdown();

    return result;
}
