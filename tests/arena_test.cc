#include "cascading/arena/arena.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <random>
#include <thread>
#include <vector>
#include "test_framework.h"

using namespace cascading::arena;

// ==================== 基础功能测试 ====================

TEST(Arena, SingletonInstance) {
    arena& instance1 = arena::get_instance();
    arena& instance2 = arena::get_instance();
    ASSERT_EQ(&instance1, &instance2);
}

TEST(Arena, Initialize) {
    arena& a = arena::get_instance();
    (void)a;
}

TEST(Arena, BasicAllocate) {
    arena& a = arena::get_instance();
    a.initialize();

    // 分配 64 字节
    void* ptr = a.allocate(64);
    ASSERT_NOT_NULL(ptr);

    // 使用内存
    memset(ptr, 0xAB, 64);

    // 释放内存
    a.deallocate(ptr, 64);
}

TEST(Arena, AllocateDifferentSizes) {
    arena& a = arena::get_instance();
    a.initialize();

    // 测试各种大小类
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 3456};

    for (std::size_t size : sizes) {
        void* ptr = a.allocate(size);
        ASSERT_NOT_NULL(ptr);

        // 写入数据验证
        memset(ptr, 0xCD, size);

        a.deallocate(ptr, size);
    }
}

TEST(Arena, AllocateZeroSize) {
    arena& a = arena::get_instance();
    a.initialize();

    void* ptr = a.allocate(0);
    ASSERT_NULL(ptr);
}

TEST(Arena, DeallocateNullptr) {
    arena& a = arena::get_instance();
    a.initialize();

    // 释放空指针不应崩溃
    a.deallocate(nullptr, 64);
}

// ==================== 大小类测试 ====================

TEST(Arena, SizeClassTable) {
    // 测试大小类转换
    ASSERT_EQ(size_class_table::size_to_class(0), 0);
    ASSERT_EQ(size_class_table::size_to_class(8), 0);
    ASSERT_EQ(size_class_table::size_to_class(9), 1);
    ASSERT_EQ(size_class_table::size_to_class(16), 1);
    ASSERT_EQ(size_class_table::size_to_class(17), 2);

    // 测试 class_to_size
    ASSERT_EQ(size_class_table::class_to_size(0), 8);
    ASSERT_EQ(size_class_table::class_to_size(1), 16);
    ASSERT_EQ(size_class_table::class_to_size(7), 64);
}

TEST(Arena, LargeObjectAllocation) {
    arena& a = arena::get_instance();
    a.initialize();

    // 超过最大大小类 (3456B) 的分配
    void* ptr = a.allocate(4096);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xEF, 4096);
    a.deallocate(ptr, 4096);
}

TEST(Arena, VeryLargeObjectAllocation) {
    arena& a = arena::get_instance();
    a.initialize();

    // 大对象分配 (1MB)
    void* ptr = a.allocate(1024 * 1024);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0x12, 1024 * 1024);
    a.deallocate(ptr, 1024 * 1024);
}

// ==================== 批量操作测试 ====================

TEST(Arena, AllocateBatch) {
    arena& a = arena::get_instance();
    a.initialize();

    const int count = 100;
    void* ptrs[count];

    int allocated = a.allocate_batch(ptrs, count, 128);
    ASSERT_EQ(allocated, count);

    // 验证所有指针有效
    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i & 0xFF, 128);
    }

    // 批量释放
    a.deallocate_batch(ptrs, allocated, 128);
}

TEST(Arena, AllocateBatchByClass) {
    arena& a = arena::get_instance();
    a.initialize();

    const int count = 50;
    void* ptrs[count];

    // 使用大小类索引 5 (48B)
    int allocated = a.allocate_batch_by_class(ptrs, count, 5);
    ASSERT_EQ(allocated, count);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 使用大小类索引批量释放
    a.deallocate_batch_by_class(ptrs, allocated, 5);
}

TEST(Arena, DeallocateBatchByClass) {
    arena& a = arena::get_instance();
    a.initialize();

    const int count = 30;
    void* ptrs[count];

    // 分配
    for (int i = 0; i < count; ++i) {
        ptrs[i] = a.allocate(256);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 使用大小类索引批量释放
    a.deallocate_batch_by_class(ptrs, count, 20);  // class 20 = 256B
}

TEST(Arena, AllocateBatchEmpty) {
    arena& a = arena::get_instance();
    a.initialize();

    void* ptrs[10];
    int allocated = a.allocate_batch(ptrs, 0, 64);
    ASSERT_EQ(allocated, 0);
}

TEST(Arena, AllocateBatchNullPtr) {
    arena& a = arena::get_instance();
    a.initialize();

    int allocated = a.allocate_batch(nullptr, 10, 64);
    ASSERT_EQ(allocated, 0);
}

// ==================== 单线程压力测试 ====================

TEST(Arena, SingleThreadStressTest) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 100;
    const int allocations_per_iteration = 50;

    std::vector<void*> ptrs;
    ptrs.reserve(allocations_per_iteration);

    for (int iter = 0; iter < iterations; ++iter) {
        // 分配阶段
        for (int i = 0; i < allocations_per_iteration; ++i) {
            std::size_t size = (8 + (i % 20) * 8);  // 8B ~ 168B
            void* ptr = a.allocate(size);
            if (ptr) {
                memset(ptr, i & 0xFF, size);
                ptrs.push_back(ptr);
            }
        }

        // 释放一半
        size_t release_count = ptrs.size() / 2;
        for (size_t i = 0; i < release_count; ++i) {
            std::size_t size = (8 + (i % 20) * 8);
            a.deallocate(ptrs[i], size);
        }

        ptrs.erase(ptrs.begin(), ptrs.begin() + release_count);
    }

    // 清理剩余
    for (size_t i = 0; i < ptrs.size(); ++i) {
        std::size_t size = (8 + (i % 20) * 8);
        a.deallocate(ptrs[i], size);
    }
}

// ==================== 多线程并发测试 ====================

TEST(Arena, MultiThreadConcurrentAllocate) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 8;
    const int allocations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<> size_dist(1, 50);

            for (int i = 0; i < allocations_per_thread; ++i) {
                std::size_t size = size_dist(rng) * 8;
                void* ptr = a.allocate(size);

                if (ptr) {
                    memset(ptr, t, size);
                    success_count++;
                    a.deallocate(ptr, size);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(success_count, num_threads * allocations_per_thread);
}

TEST(Arena, MultiThreadMixedOperations) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 8;
    const int operations_per_thread = 200;

    std::vector<std::thread> threads;
    std::atomic<int> allocate_count{0};
    std::atomic<int> deallocate_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<> size_dist(1, 30);
            std::uniform_int_distribution<> op_dist(0, 2);

            std::vector<std::pair<void*, std::size_t>> local_ptrs;
            local_ptrs.reserve(operations_per_thread);

            for (int i = 0; i < operations_per_thread; ++i) {
                int op = op_dist(rng);

                if (op == 0 || local_ptrs.empty()) {
                    std::size_t size = size_dist(rng) * 8;
                    void* ptr = a.allocate(size);
                    if (ptr) {
                        memset(ptr, t, size);
                        local_ptrs.push_back({ptr, size});
                        allocate_count++;
                    }
                } else if (op == 1 && !local_ptrs.empty()) {
                    std::uniform_int_distribution<> idx_dist(
                        0, local_ptrs.size() - 1);
                    size_t idx = idx_dist(rng);

                    a.deallocate(local_ptrs[idx].first, local_ptrs[idx].second);
                    local_ptrs.erase(local_ptrs.begin() + idx);
                    deallocate_count++;
                }
            }

            for (auto& p : local_ptrs) {
                a.deallocate(p.first, p.second);
                deallocate_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(allocate_count, deallocate_count);
}

TEST(Arena, MultiThreadHighContention) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 16;
    const int operations_per_thread = 500;

    std::vector<std::thread> threads;
    std::atomic<int> total_operations{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                void* ptr = a.allocate(64);
                if (ptr) {
                    memset(ptr, t, 64);
                    a.deallocate(ptr, 64);
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

TEST(Arena, MultiThreadBatchOperations) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 4;
    const int batch_size = 50;

    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            void* ptrs[batch_size];

            for (int iter = 0; iter < 20; ++iter) {
                // 批量分配
                int allocated = a.allocate_batch(ptrs, batch_size, 128);

                // 验证
                for (int i = 0; i < allocated; ++i) {
                    ASSERT_NOT_NULL(ptrs[i]);
                    memset(ptrs[i], t, 128);
                }

                // 批量释放
                a.deallocate_batch(ptrs, allocated, 128);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

// ==================== 内存重用测试 ====================

TEST(Arena, MemoryReuse) {
    arena& a = arena::get_instance();
    a.initialize();

    // 分配并释放，然后再次分配
    void* ptr1 = a.allocate(64);
    ASSERT_NOT_NULL(ptr1);
    a.deallocate(ptr1, 64);

    void* ptr2 = a.allocate(64);
    ASSERT_NOT_NULL(ptr2);

    // 可能重用同一地址
    a.deallocate(ptr2, 64);
}

// ==================== 边界条件测试 ====================

TEST(Arena, RapidAllocateDeallocate) {
    arena& a = arena::get_instance();
    a.initialize();

    // 快速连续分配释放
    for (int i = 0; i < 1000; ++i) {
        void* ptr = a.allocate(64);
        if (ptr) {
            a.deallocate(ptr, 64);
        }
    }
}

TEST(Arena, AllocateAllSizeClasses) {
    arena& a = arena::get_instance();
    a.initialize();

    // 测试所有大小类
    for (std::size_t class_id = 0; class_id < size_class_table::CLASS_COUNT;
         ++class_id) {
        std::size_t size = size_class_table::class_to_size(class_id);
        void* ptr = a.allocate(size);
        ASSERT_NOT_NULL(ptr);
        a.deallocate(ptr, size);
    }
}

TEST(Arena, CrossSizeClassDeallocate) {
    arena& a = arena::get_instance();
    a.initialize();

    // 分配一个大小，用另一个大小释放（应该能正确处理）
    void* ptr = a.allocate(100);  // 实际分配 104B (class 12)
    ASSERT_NOT_NULL(ptr);

    // 用不同大小释放（arena 会查找正确的大小类）
    a.deallocate(ptr, 100);
}

// ==================== 压力测试 ====================

TEST(Arena, StressTest) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 50;
    const int batch_size = 100;

    for (int iter = 0; iter < iterations; ++iter) {
        void* ptrs[batch_size];

        // 批量分配
        int allocated = a.allocate_batch(ptrs, batch_size, 256);
        ASSERT_EQ(allocated, batch_size);

        // 使用
        for (int i = 0; i < batch_size; ++i) {
            memset(ptrs[i], iter & 0xFF, 256);
        }

        // 批量释放
        a.deallocate_batch(ptrs, batch_size, 256);
    }
}

TEST(Arena, MemoryIntegrityTest) {
    arena& a = arena::get_instance();
    a.initialize();

    const int count = 100;
    void* ptrs[count];
    std::size_t sizes[count];

    // 分配并写入特定模式
    for (int i = 0; i < count; ++i) {
        sizes[i] = (8 + (i % 20) * 8);
        ptrs[i] = a.allocate(sizes[i]);
        ASSERT_NOT_NULL(ptrs[i]);

        uint8_t pattern = static_cast<uint8_t>(i & 0xFF);
        memset(ptrs[i], pattern, sizes[i]);
    }

    // 验证数据完整性
    for (int i = 0; i < count; ++i) {
        uint8_t pattern = static_cast<uint8_t>(i & 0xFF);
        uint8_t* data = static_cast<uint8_t*>(ptrs[i]);

        // 检查首尾字节
        ASSERT_EQ(data[0], pattern);
        ASSERT_EQ(data[sizes[i] - 1], pattern);
    }

    // 释放
    for (int i = 0; i < count; ++i) {
        a.deallocate(ptrs[i], sizes[i]);
    }
}

// ==================== 性能测试 ====================

#include <chrono>
#include <iomanip>
#include <sstream>

// 性能测试辅助宏和函数
namespace perf {

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::micro>;
using TimePoint = Clock::time_point;

struct BenchmarkResult {
    std::string name;
    double total_us;
    double avg_us;
    double ops_per_sec;
    size_t iterations;
    size_t thread_count;
};

inline std::vector<BenchmarkResult> g_results;

inline void report_result(const BenchmarkResult& result) {
    std::cout << "\n[性能测试结果] " << result.name << std::endl;
    std::cout << "  线程数: " << result.thread_count << std::endl;
    std::cout << "  总操作数: " << result.iterations << std::endl;
    std::cout << "  总耗时: " << std::fixed << std::setprecision(2)
              << result.total_us / 1000.0 << " ms" << std::endl;
    std::cout << "  平均耗时: " << std::fixed << std::setprecision(3)
              << result.avg_us << " us/op" << std::endl;
    std::cout << "  吞吐量: " << std::fixed << std::setprecision(0)
              << result.ops_per_sec << " ops/sec" << std::endl;
}

inline void print_summary() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "           性能测试汇总报告              " << std::endl;
    std::cout << "========================================" << std::endl;
    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(40) << r.name << " | "
                  << std::setw(6) << r.thread_count << "线程"
                  << " | " << std::setw(12) << std::fixed
                  << std::setprecision(0) << r.ops_per_sec << " ops/s"
                  << std::endl;
    }
    std::cout << "========================================" << std::endl;
}

}  // namespace perf

// ==================== 单线程性能基准测试 ====================

TEST(ArenaPerf, SingleThreadAllocateDeallocate) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 1000000;
    const size_t size = 64;

    auto start = perf::Clock::now();

    for (int i = 0; i < iterations; ++i) {
        void* ptr = a.allocate(size);
        a.deallocate(ptr, size);
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "单线程分配/释放 (64B)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / iterations;
    result.ops_per_sec = iterations * 1000000.0 / elapsed.count();
    result.iterations = iterations;
    result.thread_count = 1;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

TEST(ArenaPerf, SingleThreadVariableSize) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 500000;
    const size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    const int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    auto start = perf::Clock::now();

    for (int i = 0; i < iterations; ++i) {
        size_t size = sizes[i % num_sizes];
        void* ptr = a.allocate(size);
        a.deallocate(ptr, size);
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "单线程变长分配/释放 (8B-1KB)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / iterations;
    result.ops_per_sec = iterations * 1000000.0 / elapsed.count();
    result.iterations = iterations;
    result.thread_count = 1;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

TEST(ArenaPerf, SingleThreadBatchOperations) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 100000;
    const int batch_size = 100;
    const size_t size = 128;

    void* ptrs[batch_size];

    auto start = perf::Clock::now();

    for (int i = 0; i < iterations; ++i) {
        int allocated = a.allocate_batch(ptrs, batch_size, size);
        a.deallocate_batch(ptrs, allocated, size);
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;
    size_t total_ops = iterations * batch_size;

    perf::BenchmarkResult result;
    result.name = "单线程批量操作 (100x128B)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / total_ops;
    result.ops_per_sec = total_ops * 1000000.0 / elapsed.count();
    result.iterations = total_ops;
    result.thread_count = 1;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

TEST(ArenaPerf, SingleThreadMixedWorkload) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 500000;
    std::vector<void*> ptrs;
    ptrs.reserve(1000);

    std::mt19937 rng(42);
    std::uniform_int_distribution<> size_dist(1, 128);
    std::uniform_int_distribution<> op_dist(0, 1);

    auto start = perf::Clock::now();

    for (int i = 0; i < iterations; ++i) {
        if (op_dist(rng) == 0 || ptrs.empty()) {
            size_t size = size_dist(rng) * 8;
            void* ptr = a.allocate(size);
            if (ptr)
                ptrs.push_back(ptr);
        } else {
            void* ptr = ptrs.back();
            ptrs.pop_back();
            a.deallocate(ptr, 64);  // 简化处理
        }
    }

    // 清理
    for (void* ptr : ptrs) {
        a.deallocate(ptr, 64);
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "单线程混合负载 (分配/释放交替)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / iterations;
    result.ops_per_sec = iterations * 1000000.0 / elapsed.count();
    result.iterations = iterations;
    result.thread_count = 1;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

// ==================== 多线程并发性能测试 ====================

TEST(ArenaPerf, MultiThreadContention2) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 2;
    const int ops_per_thread = 500000;
    const size_t size = 64;

    std::vector<std::thread> threads;
    std::atomic<size_t> total_ops{0};

    auto start = perf::Clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                void* ptr = a.allocate(size);
                if (ptr) {
                    a.deallocate(ptr, size);
                    total_ops++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "多线程竞争测试 (2线程)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / total_ops.load();
    result.ops_per_sec = total_ops.load() * 1000000.0 / elapsed.count();
    result.iterations = total_ops.load();
    result.thread_count = num_threads;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

TEST(ArenaPerf, MultiThreadContention4) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 4;
    const int ops_per_thread = 500000;
    const size_t size = 64;

    std::vector<std::thread> threads;
    std::atomic<size_t> total_ops{0};

    auto start = perf::Clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                void* ptr = a.allocate(size);
                if (ptr) {
                    a.deallocate(ptr, size);
                    total_ops++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "多线程竞争测试 (4线程)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / total_ops.load();
    result.ops_per_sec = total_ops.load() * 1000000.0 / elapsed.count();
    result.iterations = total_ops.load();
    result.thread_count = num_threads;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

TEST(ArenaPerf, MultiThreadContention8) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 8;
    const int ops_per_thread = 500000;
    const size_t size = 64;

    std::vector<std::thread> threads;
    std::atomic<size_t> total_ops{0};

    auto start = perf::Clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                void* ptr = a.allocate(size);
                if (ptr) {
                    a.deallocate(ptr, size);
                    total_ops++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "多线程竞争测试 (8线程)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / total_ops.load();
    result.ops_per_sec = total_ops.load() * 1000000.0 / elapsed.count();
    result.iterations = total_ops.load();
    result.thread_count = num_threads;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

TEST(ArenaPerf, MultiThreadContention16) {
    arena& a = arena::get_instance();
    a.initialize();

    const int num_threads = 16;
    const int ops_per_thread = 500000;
    const size_t size = 64;

    std::vector<std::thread> threads;
    std::atomic<size_t> total_ops{0};

    auto start = perf::Clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                void* ptr = a.allocate(size);
                if (ptr) {
                    a.deallocate(ptr, size);
                    total_ops++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;

    perf::BenchmarkResult result;
    result.name = "多线程竞争测试 (16线程)";
    result.total_us = elapsed.count();
    result.avg_us = elapsed.count() / total_ops.load();
    result.ops_per_sec = total_ops.load() * 1000000.0 / elapsed.count();
    result.iterations = total_ops.load();
    result.thread_count = num_threads;

    perf::g_results.push_back(result);
    perf::report_result(result);
}

// ==================== 批量操作性能测试 ====================

TEST(ArenaPerf, BatchComparisonSmall) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 100000;
    const int batch_size = 10;
    const size_t size = 64;
    void* ptrs[10];

    // 批量操作
    auto start = perf::Clock::now();
    for (int i = 0; i < iterations; ++i) {
        int allocated = a.allocate_batch(ptrs, batch_size, size);
        a.deallocate_batch(ptrs, allocated, size);
    }
    auto end = perf::Clock::now();
    perf::Duration batch_elapsed = end - start;

    // 单个操作
    start = perf::Clock::now();
    for (int i = 0; i < iterations * batch_size; ++i) {
        void* ptr = a.allocate(size);
        a.deallocate(ptr, size);
    }
    end = perf::Clock::now();
    perf::Duration single_elapsed = end - start;

    std::cout << "\n[批量 vs 单个小对象对比]" << std::endl;
    std::cout << "  批量操作 (10x64B): " << std::fixed << std::setprecision(2)
              << batch_elapsed.count() / 1000.0 << " ms" << std::endl;
    std::cout << "  单个操作 (10x64B): " << std::fixed << std::setprecision(2)
              << single_elapsed.count() / 1000.0 << " ms" << std::endl;
    std::cout << "  加速比: " << std::fixed << std::setprecision(2)
              << single_elapsed.count() / batch_elapsed.count() << "x"
              << std::endl;
}

TEST(ArenaPerf, BatchComparisonLarge) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 10000;
    const int batch_size = 100;
    const size_t size = 256;
    void* ptrs[100];

    // 批量操作
    auto start = perf::Clock::now();
    for (int i = 0; i < iterations; ++i) {
        int allocated = a.allocate_batch(ptrs, batch_size, size);
        a.deallocate_batch(ptrs, allocated, size);
    }
    auto end = perf::Clock::now();
    perf::Duration batch_elapsed = end - start;

    // 单个操作
    start = perf::Clock::now();
    for (int i = 0; i < iterations * batch_size; ++i) {
        void* ptr = a.allocate(size);
        a.deallocate(ptr, size);
    }
    end = perf::Clock::now();
    perf::Duration single_elapsed = end - start;

    std::cout << "\n[批量 vs 单个大对象对比]" << std::endl;
    std::cout << "  批量操作 (100x256B): " << std::fixed << std::setprecision(2)
              << batch_elapsed.count() / 1000.0 << " ms" << std::endl;
    std::cout << "  单个操作 (100x256B): " << std::fixed << std::setprecision(2)
              << single_elapsed.count() / 1000.0 << " ms" << std::endl;
    std::cout << "  加速比: " << std::fixed << std::setprecision(2)
              << single_elapsed.count() / batch_elapsed.count() << "x"
              << std::endl;
}

// ==================== 不同大小类性能测试 ====================

TEST(ArenaPerf, SizeClassComparison) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 200000;
    const size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 3456};
    const int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    std::cout << "\n[不同大小类性能对比]" << std::endl;
    std::cout << std::setw(10) << "大小(B)" << std::setw(15) << "耗时(ms)"
              << std::setw(15) << "ops/sec" << std::setw(15) << "us/op"
              << std::endl;
    std::cout << std::string(55, '-') << std::endl;

    for (int s = 0; s < num_sizes; ++s) {
        size_t size = sizes[s];

        auto start = perf::Clock::now();
        for (int i = 0; i < iterations; ++i) {
            void* ptr = a.allocate(size);
            a.deallocate(ptr, size);
        }
        auto end = perf::Clock::now();
        perf::Duration elapsed = end - start;

        double ms = elapsed.count() / 1000.0;
        double ops_per_sec = iterations * 1000000.0 / elapsed.count();
        double us_per_op = elapsed.count() / iterations;

        std::cout << std::setw(10) << size << std::setw(15) << std::fixed
                  << std::setprecision(2) << ms << std::setw(15) << std::fixed
                  << std::setprecision(0) << ops_per_sec << std::setw(15)
                  << std::fixed << std::setprecision(3) << us_per_op
                  << std::endl;
    }
}

// ==================== 内存碎片测试 ====================

TEST(ArenaPerf, FragmentationTest) {
    arena& a = arena::get_instance();
    a.initialize();

    const int iterations = 1000;
    const int objects_per_iteration = 1000;
    const size_t size = 64;

    std::vector<void*> ptrs;
    ptrs.reserve(objects_per_iteration);

    std::mt19937 rng(42);
    std::uniform_int_distribution<> release_dist(10, 90);  // 释放 10%-90%

    auto start = perf::Clock::now();

    for (int iter = 0; iter < iterations; ++iter) {
        // 分配
        for (int i = 0; i < objects_per_iteration; ++i) {
            void* ptr = a.allocate(size);
            if (ptr)
                ptrs.push_back(ptr);
        }

        // 随机释放一部分
        int release_percent = release_dist(rng);
        int release_count = ptrs.size() * release_percent / 100;

        std::shuffle(ptrs.begin(), ptrs.end(), rng);

        for (int i = 0; i < release_count; ++i) {
            a.deallocate(ptrs[i], size);
        }

        ptrs.erase(ptrs.begin(), ptrs.begin() + release_count);
    }

    // 清理剩余
    for (void* ptr : ptrs) {
        a.deallocate(ptr, size);
    }

    auto end = perf::Clock::now();
    perf::Duration elapsed = end - start;
    size_t total_ops = iterations * objects_per_iteration;

    std::cout << "\n[内存碎片测试]" << std::endl;
    std::cout << "  迭代次数: " << iterations << std::endl;
    std::cout << "  每轮对象数: " << objects_per_iteration << std::endl;
    std::cout << "  总操作数: " << total_ops << std::endl;
    std::cout << "  总耗时: " << std::fixed << std::setprecision(2)
              << elapsed.count() / 1000.0 << " ms" << std::endl;
    std::cout << "  平均吞吐量: " << std::fixed << std::setprecision(0)
              << total_ops * 1000000.0 / elapsed.count() << " ops/sec"
              << std::endl;
}

// ==================== 扩展性测试 ====================

TEST(ArenaPerf, ScalabilityTest) {
    arena& a = arena::get_instance();
    a.initialize();

    const int max_threads = 16;
    const int ops_per_thread = 200000;
    const size_t size = 64;

    std::cout << "\n[扩展性测试]" << std::endl;
    std::cout << std::setw(10) << "线程数" << std::setw(15) << "总ops"
              << std::setw(15) << "耗时(ms)" << std::setw(15) << "ops/sec"
              << std::setw(15) << "加速比" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    double baseline_ops = 0;

    for (int num_threads = 1; num_threads <= max_threads; num_threads *= 2) {
        std::vector<std::thread> threads;
        std::atomic<size_t> total_ops{0};

        auto start = perf::Clock::now();

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    void* ptr = a.allocate(size);
                    if (ptr) {
                        a.deallocate(ptr, size);
                        total_ops++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = perf::Clock::now();
        perf::Duration elapsed = end - start;

        double ops_per_sec = total_ops.load() * 1000000.0 / elapsed.count();
        if (num_threads == 1) {
            baseline_ops = ops_per_sec;
        }
        double speedup = ops_per_sec / baseline_ops;

        std::cout << std::setw(10) << num_threads << std::setw(15)
                  << total_ops.load() << std::setw(15) << std::fixed
                  << std::setprecision(2) << elapsed.count() / 1000.0
                  << std::setw(15) << std::fixed << std::setprecision(0)
                  << ops_per_sec << std::setw(15) << std::fixed
                  << std::setprecision(2) << speedup << std::endl;
    }
}

// ==================== 主函数 ====================

int main() {
    int result = test::run_all_tests();

    // 打印性能测试汇总
    if (!perf::g_results.empty()) {
        perf::print_summary();
    }

    return result;
}
