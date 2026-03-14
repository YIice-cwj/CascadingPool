#include "cascading/cascading_pool.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include "test_framework.h"

using namespace cascading;

// ==================== 基础功能测试 ====================

TEST(CascadingPool, BasicAllocateAndDeallocate) {
    void* ptr = allocate(64);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xAB, 64);
    deallocate(ptr, 64);
}

TEST(CascadingPool, AllocateDifferentSizes) {
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    void* ptrs[10];

    for (int i = 0; i < 10; ++i) {
        ptrs[i] = allocate(sizes[i]);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, sizes[i]);
    }

    for (int i = 0; i < 10; ++i) {
        deallocate(ptrs[i], sizes[i]);
    }
}

TEST(CascadingPool, AllocateZeroSize) {
    void* ptr = allocate(0);
    ASSERT_NULL(ptr);
}

TEST(CascadingPool, DeallocateNullptr) {
    deallocate(nullptr, 64);
}

TEST(CascadingPool, DeallocateZeroSize) {
    void* ptr = allocate(64);
    ASSERT_NOT_NULL(ptr);
    deallocate(ptr, 0);
}

TEST(CascadingPool, MultipleAllocateDeallocate) {
    const int count = 1000;
    void* ptrs[count];

    for (int i = 0; i < count; ++i) {
        ptrs[i] = allocate(64);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i % 256, 64);
    }

    for (int i = 0; i < count; ++i) {
        deallocate(ptrs[i], 64);
    }
}

TEST(CascadingPool, MemoryReuse) {
    void* ptr1 = allocate(64);
    ASSERT_NOT_NULL(ptr1);
    deallocate(ptr1, 64);

    void* ptr2 = allocate(64);
    ASSERT_NOT_NULL(ptr2);
    deallocate(ptr2, 64);
}

// ==================== 批量操作测试 ====================

TEST(CascadingPool, BatchAllocateAndDeallocate) {
    const int count = 32;
    void* ptrs[count];

    int allocated = allocate_batch(ptrs, count, 64);
    ASSERT_EQ(count, allocated);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, 64);
    }

    deallocate_batch(ptrs, allocated, 64);
}

TEST(CascadingPool, BatchAllocatePartial) {
    const int count = 1000;
    void* ptrs[count];

    int allocated = allocate_batch(ptrs, count, 64);
    ASSERT_GT(allocated, 0);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
    }

    deallocate_batch(ptrs, allocated, 64);
}

TEST(CascadingPool, BatchWithNullptrArray) {
    int allocated = allocate_batch(nullptr, 10, 64);
    ASSERT_EQ(0, allocated);
}

TEST(CascadingPool, BatchZeroCount) {
    void* ptrs[10];
    int allocated = allocate_batch(ptrs, 0, 64);
    ASSERT_EQ(0, allocated);
}

// ==================== 内存完整性测试 ====================

TEST(CascadingPool, MemoryIntegrity) {
    const int count = 100;
    void* ptrs[count];

    for (int i = 0; i < count; ++i) {
        ptrs[i] = allocate(64);
        ASSERT_NOT_NULL(ptrs[i]);

        unsigned char* bytes = static_cast<unsigned char*>(ptrs[i]);
        for (int j = 0; j < 64; ++j) {
            bytes[j] = static_cast<unsigned char>((i + j) % 256);
        }
    }

    for (int i = 0; i < count; ++i) {
        unsigned char* bytes = static_cast<unsigned char*>(ptrs[i]);
        for (int j = 0; j < 64; ++j) {
            ASSERT_EQ(bytes[j], static_cast<unsigned char>((i + j) % 256));
        }
    }

    for (int i = 0; i < count; ++i) {
        deallocate(ptrs[i], 64);
    }
}

// ==================== 多线程测试 ====================

TEST(CascadingPool, MultiThreadAllocate) {
    const int num_threads = 8;
    const int allocations_per_thread = 1000;

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&success_count, allocations_per_thread, t]() {
            for (int i = 0; i < allocations_per_thread; ++i) {
                void* ptr = allocate(64);
                if (ptr != nullptr) {
                    memset(ptr, t, 64);
                    success_count++;
                    deallocate(ptr, 64);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * allocations_per_thread, success_count.load());
}

TEST(CascadingPool, MultiThreadDifferentSizes) {
    const int num_threads = 8;
    const int iterations = 500;

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&success_count, iterations, t]() {
            std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};
            for (int i = 0; i < iterations; ++i) {
                std::size_t size = sizes[i % 8];
                void* ptr = allocate(size);
                if (ptr != nullptr) {
                    memset(ptr, t, size);
                    success_count++;
                    deallocate(ptr, size);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * iterations, success_count.load());
}

TEST(CascadingPool, MultiThreadBatchAllocate) {
    const int num_threads = 4;
    const int batch_size = 32;
    const int iterations = 100;

    std::atomic<int> total_allocated{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&total_allocated, batch_size, iterations]() {
            for (int i = 0; i < iterations; ++i) {
                void* ptrs[batch_size];
                int n = allocate_batch(ptrs, batch_size, 64);
                total_allocated += n;
                deallocate_batch(ptrs, n, 64);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * iterations * batch_size, total_allocated.load());
}

TEST(CascadingPool, ThreadCacheIsolation) {
    const int num_threads = 8;
    std::atomic<std::size_t> cache_count_before{0};
    std::atomic<std::size_t> cache_count_after{0};

    cache_count_before.store(get_thread_cache_count());

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([]() {
            void* ptr = allocate(8);
            deallocate(ptr, 8);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    cache_count_after.store(get_thread_cache_count());

    ASSERT_EQ(cache_count_before.load() + num_threads,
              cache_count_after.load());
}

// ==================== 压力测试 ====================

TEST(CascadingPool, StressTestHighContention) {
    const int num_threads = 16;
    const int iterations = 10000;
    const std::size_t size = 64;

    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(
            [&success_count, &fail_count, iterations, size, t]() {
                for (int i = 0; i < iterations; ++i) {
                    void* ptr = allocate(size);
                    if (ptr != nullptr) {
                        memset(ptr, t, size);
                        success_count++;
                        deallocate(ptr, size);
                    } else {
                        fail_count++;
                    }
                }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(0, fail_count.load());
    ASSERT_EQ(num_threads * iterations, success_count.load());
}

TEST(CascadingPool, StressTestRandomSizes) {
    const int num_threads = 8;
    const int iterations = 5000;

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&success_count, iterations, t]() {
            std::random_device rd;
            std::mt19937 gen(rd() + t);
            std::uniform_int_distribution<> size_dist(8, 4096);

            for (int i = 0; i < iterations; ++i) {
                std::size_t size = size_dist(gen);
                void* ptr = allocate(size);
                if (ptr != nullptr) {
                    memset(ptr, t, size);
                    success_count++;
                    deallocate(ptr, size);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads * iterations, success_count.load());
}

TEST(CascadingPool, StressTestAllocOnly) {
    const int num_threads = 4;
    const int allocations_per_thread = 10000;
    const std::size_t size = 64;

    std::vector<std::vector<void*>> thread_ptrs(num_threads);
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_ptrs[t].reserve(allocations_per_thread);
            for (int i = 0; i < allocations_per_thread; ++i) {
                void* ptr = allocate(size);
                if (ptr != nullptr) {
                    thread_ptrs[t].push_back(ptr);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    int total_allocated = 0;
    for (int t = 0; t < num_threads; ++t) {
        total_allocated += static_cast<int>(thread_ptrs[t].size());
        for (void* ptr : thread_ptrs[t]) {
            deallocate(ptr, size);
        }
    }

    ASSERT_EQ(num_threads * allocations_per_thread, total_allocated);
}

TEST(CascadingPool, StressTestRapidCreateDestroy) {
    // 使用线程池避免创建过多线程
    const int num_threads = 4;
    const int iterations = 100;
    const int ops_per_iteration = 100;

    std::vector<std::thread> threads;
    std::atomic<int> completed{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int iter = 0; iter < iterations; ++iter) {
                for (int i = 0; i < ops_per_iteration; ++i) {
                    void* ptr = allocate(64);
                    if (ptr != nullptr) {
                        deallocate(ptr, 64);
                    }
                }
            }
            completed++;
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(num_threads, completed.load());
}

// ==================== 性能测试 ====================

inline double measure_time_ms(std::function<void()> func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

TEST(CascadingPoolPerf, SingleThreadThroughput) {
    const int iterations = 1000000;
    const std::size_t size = 64;

    double elapsed = measure_time_ms([&]() {
        for (int i = 0; i < iterations; ++i) {
            void* ptr = allocate(size);
            deallocate(ptr, size);
        }
    });

    double ops_per_sec = (iterations * 2) / (elapsed / 1000.0);
    std::cout << "\n  [性能] 单线程吞吐量: " << iterations << " 次分配/释放, "
              << std::fixed << std::setprecision(2) << elapsed << " ms, "
              << std::setprecision(0) << ops_per_sec << " ops/sec" << std::endl;
}

TEST(CascadingPoolPerf, BatchThroughput) {
    const int iterations = 100000;
    const int batch_size = 32;
    const std::size_t size = 64;

    double elapsed = measure_time_ms([&]() {
        for (int i = 0; i < iterations; ++i) {
            void* ptrs[batch_size];
            int n = allocate_batch(ptrs, batch_size, size);
            deallocate_batch(ptrs, n, size);
        }
    });

    double ops_per_sec = (iterations * batch_size * 2) / (elapsed / 1000.0);
    std::cout << "\n  [性能] 批量吞吐量: " << iterations << " 批次 (每批"
              << batch_size << "个), " << std::fixed << std::setprecision(2)
              << elapsed << " ms, " << std::setprecision(0) << ops_per_sec
              << " ops/sec" << std::endl;
}

TEST(CascadingPoolPerf, MultiThreadScalability) {
    const int iterations_per_thread = 100000;
    const std::size_t size = 64;

    std::cout << "\n  [性能] 多线程扩展性测试:" << std::endl;

    for (int num_threads : {1, 2, 4, 8, 16}) {
        double elapsed = measure_time_ms([&]() {
            std::vector<std::thread> threads;
            for (int t = 0; t < num_threads; ++t) {
                threads.emplace_back([&]() {
                    for (int i = 0; i < iterations_per_thread; ++i) {
                        void* ptr = allocate(size);
                        deallocate(ptr, size);
                    }
                });
            }
            for (auto& t : threads) {
                t.join();
            }
        });

        int total_ops = num_threads * iterations_per_thread * 2;
        double ops_per_sec = total_ops / (elapsed / 1000.0);

        std::cout << "    " << std::setw(2) << num_threads
                  << " 线程: " << std::fixed << std::setprecision(2)
                  << std::setw(8) << elapsed << " ms, " << std::setw(12)
                  << std::setprecision(0) << ops_per_sec << " ops/sec"
                  << std::endl;
    }
}

TEST(CascadingPoolPerf, SizeClassPerformance) {
    const int iterations = 100000;
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};

    std::cout << "\n  [性能] 不同大小类性能:" << std::endl;

    for (std::size_t size : sizes) {
        double elapsed = measure_time_ms([&]() {
            for (int i = 0; i < iterations; ++i) {
                void* ptr = allocate(size);
                deallocate(ptr, size);
            }
        });

        double ops_per_sec = (iterations * 2) / (elapsed / 1000.0);
        std::cout << "    大小 " << std::setw(5) << size
                  << " bytes: " << std::fixed << std::setprecision(2)
                  << std::setw(8) << elapsed << " ms, " << std::setw(12)
                  << std::setprecision(0) << ops_per_sec << " ops/sec"
                  << std::endl;
    }
}

TEST(CascadingPoolPerf, LatencyDistribution) {
    const int iterations = 100000;
    const std::size_t size = 64;

    std::vector<double> latencies;
    latencies.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        void* ptr = allocate(size);
        auto alloc_end = std::chrono::high_resolution_clock::now();
        deallocate(ptr, size);
        auto end = std::chrono::high_resolution_clock::now();

        double alloc_time =
            std::chrono::duration<double, std::micro>(alloc_end - start)
                .count();
        latencies.push_back(alloc_time);
    }

    std::sort(latencies.begin(), latencies.end());

    double p50 = latencies[iterations * 50 / 100];
    double p90 = latencies[iterations * 90 / 100];
    double p99 = latencies[iterations * 99 / 100];
    double p999 = latencies[iterations * 999 / 1000];

    std::cout << "\n  [性能] 延迟分布 (微秒):" << std::endl;
    std::cout << "    P50:  " << std::fixed << std::setprecision(2) << p50
              << " us" << std::endl;
    std::cout << "    P90:  " << p90 << " us" << std::endl;
    std::cout << "    P99:  " << p99 << " us" << std::endl;
    std::cout << "    P99.9: " << p999 << " us" << std::endl;
}

TEST(CascadingPoolPerf, ConcurrentHotSpot) {
    const int num_threads = 8;
    const int iterations = 50000;
    const std::size_t size = 64;

    std::atomic<int> completed{0};

    double elapsed = measure_time_ms([&]() {
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < iterations; ++i) {
                    void* ptr = allocate(size);
                    if (ptr) {
                        memset(ptr, 0, size);
                        deallocate(ptr, size);
                    }
                }
                completed++;
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    });

    int total_ops = num_threads * iterations * 2;
    double ops_per_sec = total_ops / (elapsed / 1000.0);

    std::cout << "\n  [性能] 并发热点测试 (" << num_threads
              << "线程): " << std::fixed << std::setprecision(2) << elapsed
              << " ms, " << std::setprecision(0) << ops_per_sec << " ops/sec"
              << std::endl;
}

// ==================== 清理测试 ====================

TEST(CascadingPool, CleanupAndReallocate) {
    void* ptr1 = allocate(64);
    ASSERT_NOT_NULL(ptr1);
    deallocate(ptr1, 64);

    cleanup_current_thread();

    void* ptr2 = allocate(64);
    ASSERT_NOT_NULL(ptr2);
    deallocate(ptr2, 64);
}

TEST(CascadingPool, GetThreadCacheCount) {
    std::size_t count_before = get_thread_cache_count();

    void* ptr = allocate(8);
    deallocate(ptr, 8);

    std::size_t count_after = get_thread_cache_count();
    ASSERT_GE(count_after, count_before);
}

// ==================== 主函数 ====================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Cascading Pool 完整测试" << std::endl;
    std::cout << "========================================" << std::endl;

    // 初始化（可能已经初始化过）
    initialize();

    // 运行测试
    int result = test::run_all_tests();

    // 关闭
    shutdown();

    std::cout << "========================================" << std::endl;
    std::cout << "测试完成" << std::endl;
    std::cout << "========================================" << std::endl;

    return result;
}
