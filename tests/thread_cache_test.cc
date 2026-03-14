#include "cascading/thread_cache/thread_cache.h"
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>
#include "test_framework.h"

using namespace cascading::thread_cache;

// ==================== 基础功能测试 ====================

TEST(ThreadCache, BasicAllocateAndDeallocate) {
    thread_cache cache;

    // 分配内存
    void* ptr = cache.allocate(64);
    printf("ptr: %p\n", ptr);
    ASSERT_NOT_NULL(ptr);

    // 写入数据
    memset(ptr, 0xAB, 64);

    // 释放内存
    cache.deallocate(ptr, 64);
}

TEST(ThreadCache, AllocateDifferentSizes) {
    thread_cache cache;

    // 测试不同大小的分配
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    void* ptrs[9];

    for (int i = 0; i < 9; ++i) {
        ptrs[i] = cache.allocate(sizes[i]);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, sizes[i]);
    }

    for (int i = 0; i < 9; ++i) {
        cache.deallocate(ptrs[i], sizes[i]);
    }
}

TEST(ThreadCache, AllocateZeroSize) {
    thread_cache cache;

    // 分配 0 字节应该返回 nullptr
    void* ptr = cache.allocate(0);
    ASSERT_NULL(ptr);
}

TEST(ThreadCache, DeallocateNullptr) {
    thread_cache cache;

    // 释放空指针不应崩溃
    cache.deallocate(nullptr, 64);
}

TEST(ThreadCache, DeallocateZeroSize) {
    thread_cache cache;

    // 分配内存
    void* ptr = cache.allocate(64);
    ASSERT_NOT_NULL(ptr);

    // 释放时 size=0 应该安全处理
    cache.deallocate(ptr, 0);
}

TEST(ThreadCache, MultipleAllocateDeallocate) {
    thread_cache cache;

    const int count = 100;
    void* ptrs[count];

    // 分配多个对象
    for (int i = 0; i < count; ++i) {
        ptrs[i] = cache.allocate(64);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, 64);
    }

    // 释放所有对象
    for (int i = 0; i < count; ++i) {
        cache.deallocate(ptrs[i], 64);
    }
}

TEST(ThreadCache, MemoryReuse) {
    thread_cache cache;

    // 分配并释放内存
    void* ptr1 = cache.allocate(64);
    ASSERT_NOT_NULL(ptr1);
    cache.deallocate(ptr1, 64);

    // 再次分配，应该重用同一块内存
    void* ptr2 = cache.allocate(64);
    ASSERT_NOT_NULL(ptr2);

    // 验证地址相同（缓存重用）
    ASSERT_EQ(ptr1, ptr2);

    cache.deallocate(ptr2, 64);
}

// ==================== 批量操作测试 ====================

TEST(ThreadCache, AllocateBatchAndDeallocateBatch) {
    thread_cache cache;

    const int count = 10;
    void* ptrs[count];

    // 批量分配
    int allocated = cache.allocate_batch(ptrs, count, 64);
    ASSERT_EQ(allocated, count);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i, 64);
    }

    // 批量释放
    cache.deallocate_batch(ptrs, allocated, 64);
}

TEST(ThreadCache, AllocateBatchLargeCount) {
    thread_cache cache;

    // 请求大量分配
    const int count = 100;
    void* ptrs[count];

    int allocated = cache.allocate_batch(ptrs, count, 64);
    ASSERT_GT(allocated, 0);

    for (int i = 0; i < allocated; ++i) {
        ASSERT_NOT_NULL(ptrs[i]);
    }

    cache.deallocate_batch(ptrs, allocated, 64);
}

TEST(ThreadCache, DeallocateBatchNullptr) {
    thread_cache cache;

    // 批量释放含空指针不应崩溃
    void* ptrs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    cache.deallocate_batch(ptrs, 5, 64);
}

TEST(ThreadCache, AllocateBatchEmpty) {
    thread_cache cache;

    void* ptrs[5];
    int count = cache.allocate_batch(ptrs, 0, 64);
    ASSERT_EQ(count, 0);

    count = cache.allocate_batch(nullptr, 5, 64);
    ASSERT_EQ(count, 0);
}

TEST(ThreadCache, DeallocateBatchEmpty) {
    thread_cache cache;

    void* ptrs[5];
    // 空批量释放不应崩溃
    cache.deallocate_batch(ptrs, 0, 64);
    cache.deallocate_batch(nullptr, 5, 64);
}

// ==================== 压力测试 ====================

TEST(ThreadCache, StressTest) {
    thread_cache cache;

    const int iterations = 100;

    for (int iter = 0; iter < iterations; ++iter) {
        // 分配 10 个对象
        void* ptrs[10];
        for (int i = 0; i < 10; ++i) {
            ptrs[i] = cache.allocate(64);
            ASSERT_NOT_NULL(ptrs[i]);
        }

        // 释放 10 个对象
        for (int i = 0; i < 10; ++i) {
            cache.deallocate(ptrs[i], 64);
        }
    }
}

TEST(ThreadCache, RapidAllocateDeallocate) {
    thread_cache cache;

    const int count = 1000;

    for (int i = 0; i < count; ++i) {
        void* ptr = cache.allocate(64);
        ASSERT_NOT_NULL(ptr);
        cache.deallocate(ptr, 64);
    }
}

TEST(ThreadCache, StressTestDifferentSizes) {
    thread_cache cache;

    const int iterations = 50;
    std::size_t sizes[] = {8, 16, 32, 64, 128, 256};

    for (int iter = 0; iter < iterations; ++iter) {
        void* ptrs[6];

        // 分配不同大小
        for (int i = 0; i < 6; ++i) {
            ptrs[i] = cache.allocate(sizes[i]);
            ASSERT_NOT_NULL(ptrs[i]);
        }

        // 释放
        for (int i = 0; i < 6; ++i) {
            cache.deallocate(ptrs[i], sizes[i]);
        }
    }
}

// ==================== 多线程测试 ====================

TEST(ThreadCache, MultiThreadIndependentCaches) {
    // 每个线程有自己的 thread_cache
    const int num_threads = 4;
    const int operations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_cache cache;

            for (int i = 0; i < operations_per_thread; ++i) {
                void* ptr = cache.allocate(64);
                if (ptr) {
                    memset(ptr, t, 64);
                    cache.deallocate(ptr, 64);
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(success_count.load(), num_threads * operations_per_thread);
}

TEST(ThreadCache, MultiThreadMixedOperations) {
    const int num_threads = 8;
    const int operations_per_thread = 50;

    std::vector<std::thread> threads;
    std::atomic<int> allocate_count{0};
    std::atomic<int> deallocate_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_cache cache;

            std::vector<void*> local_ptrs;

            for (int i = 0; i < operations_per_thread; ++i) {
                if (i % 2 == 0) {
                    // 分配
                    void* ptr = cache.allocate(64);
                    if (ptr) {
                        memset(ptr, t, 64);
                        local_ptrs.push_back(ptr);
                        allocate_count++;
                    }
                } else if (!local_ptrs.empty()) {
                    // 释放
                    void* ptr = local_ptrs.back();
                    local_ptrs.pop_back();
                    cache.deallocate(ptr, 64);
                    deallocate_count++;
                }
            }

            // 清理剩余
            for (void* ptr : local_ptrs) {
                cache.deallocate(ptr, 64);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST(ThreadCache, MultiThreadBatchOperations) {
    const int num_threads = 4;
    const int batch_size = 20;

    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_cache cache;

            for (int iter = 0; iter < 10; ++iter) {
                void* ptrs[batch_size];

                // 批量分配
                int count = cache.allocate_batch(ptrs, batch_size, 64);

                for (int i = 0; i < count; ++i) {
                    ASSERT_NOT_NULL(ptrs[i]);
                    memset(ptrs[i], t, 64);
                }

                // 批量释放
                cache.deallocate_batch(ptrs, count, 64);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST(ThreadCache, MultiThreadHighContention) {
    const int num_threads = 16;
    const int operations_per_thread = 200;

    std::vector<std::thread> threads;
    std::atomic<int> total_operations{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_cache cache;

            for (int i = 0; i < operations_per_thread; ++i) {
                void* ptr = cache.allocate(64);
                if (ptr) {
                    memset(ptr, t, 64);
                    cache.deallocate(ptr, 64);
                    total_operations++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(total_operations.load(), num_threads * operations_per_thread);
}

// ==================== 边界条件测试 ====================

TEST(ThreadCache, LargeAllocation) {
    thread_cache cache;

    // 大对象分配
    void* ptr = cache.allocate(4096);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xFF, 4096);
    cache.deallocate(ptr, 4096);
}

TEST(ThreadCache, VeryLargeAllocation) {
    thread_cache cache;

    // 超大对象分配
    void* ptr = cache.allocate(1024 * 1024);  // 1MB
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xAA, 1024 * 1024);
    cache.deallocate(ptr, 1024 * 1024);
}

TEST(ThreadCache, SizeClassBoundaries) {
    thread_cache cache;

    // 测试大小类边界
    std::size_t boundary_sizes[] = {8,   16,   32,   64,   128, 256,
                                    512, 1024, 2048, 4096, 8192};

    for (std::size_t size : boundary_sizes) {
        void* ptr = cache.allocate(size);
        ASSERT_NOT_NULL(ptr);
        cache.deallocate(ptr, size);
    }
}

// ==================== 内存完整性测试 ====================

TEST(ThreadCache, MemoryIntegrity) {
    thread_cache cache;

    const int count = 100;
    void* ptrs[count];

    // 分配并写入不同数据
    for (int i = 0; i < count; ++i) {
        ptrs[i] = cache.allocate(64);
        ASSERT_NOT_NULL(ptrs[i]);

        // 写入特定模式
        unsigned char* bytes = static_cast<unsigned char*>(ptrs[i]);
        for (int j = 0; j < 64; ++j) {
            bytes[j] = static_cast<unsigned char>((i + j) % 256);
        }
    }

    // 验证数据完整性
    for (int i = 0; i < count; ++i) {
        unsigned char* bytes = static_cast<unsigned char*>(ptrs[i]);
        for (int j = 0; j < 64; ++j) {
            ASSERT_EQ(bytes[j], static_cast<unsigned char>((i + j) % 256));
        }
    }

    // 释放
    for (int i = 0; i < count; ++i) {
        cache.deallocate(ptrs[i], 64);
    }
}

// ==================== 定时刷新测试 ====================

TEST(ThreadCache, FlushTask) {
    thread_cache cache;

    // 分配一些内存
    void* ptrs[10];
    for (int i = 0; i < 10; ++i) {
        ptrs[i] = cache.allocate(64);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 释放到缓存
    for (int i = 0; i < 10; ++i) {
        cache.deallocate(ptrs[i], 64);
    }

    // 等待定时任务可能执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 仍然可以正常分配
    void* ptr = cache.allocate(64);
    ASSERT_NOT_NULL(ptr);
    cache.deallocate(ptr, 64);
}

// ==================== thread_local 测试 ====================

// thread_local 变量：每个线程独立拥有
thread_local thread_cache* tls_cache = nullptr;

// 辅助函数：获取或创建线程本地缓存
thread_cache* get_thread_cache() {
    if (tls_cache == nullptr) {
        tls_cache = new thread_cache();
    }
    return tls_cache;
}

TEST(ThreadCache, ThreadLocalBasic) {
    // 主线程获取缓存
    thread_cache* cache1 = get_thread_cache();
    ASSERT_NOT_NULL(cache1);

    // 分配内存
    void* ptr = cache1->allocate(64);
    ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xAB, 64);
    cache1->deallocate(ptr, 64);
}

TEST(ThreadCache, ThreadLocalIndependentInstances) {
    // 主线程获取缓存实例
    thread_cache* main_cache = get_thread_cache();
    ASSERT_NOT_NULL(main_cache);

    std::atomic<void*> thread1_ptr{nullptr};
    std::atomic<void*> thread2_ptr{nullptr};
    std::atomic<thread_cache*> thread1_cache{nullptr};
    std::atomic<thread_cache*> thread2_cache{nullptr};

    std::thread t1([&]() {
        // 线程1获取自己的缓存实例
        thread_cache* cache = get_thread_cache();
        thread1_cache.store(cache);

        // 分配内存
        void* ptr = cache->allocate(64);
        if (ptr) {
            memset(ptr, 1, 64);
            thread1_ptr.store(ptr);
        }
    });

    std::thread t2([&]() {
        // 线程2获取自己的缓存实例
        thread_cache* cache = get_thread_cache();
        thread2_cache.store(cache);

        // 分配内存
        void* ptr = cache->allocate(64);
        if (ptr) {
            memset(ptr, 2, 64);
            thread2_ptr.store(ptr);
        }
    });

    t1.join();
    t2.join();

    // 验证：每个线程的缓存实例不同
    ASSERT_NE(main_cache, thread1_cache.load());
    ASSERT_NE(main_cache, thread2_cache.load());
    ASSERT_NE(thread1_cache.load(), thread2_cache.load());

    // 验证：每个线程分配的内存地址不同
    ASSERT_NE(thread1_ptr.load(), thread2_ptr.load());
    ASSERT_NE(main_cache, thread1_cache.load());
}

TEST(ThreadCache, ThreadLocalCacheIsolation) {
    // 主线程分配并缓存内存
    thread_cache* main_cache = get_thread_cache();
    void* main_ptr = main_cache->allocate(64);
    ASSERT_NOT_NULL(main_ptr);
    main_cache->deallocate(main_ptr, 64);

    // 再次分配，应该重用缓存
    void* main_ptr2 = main_cache->allocate(64);
    ASSERT_EQ(main_ptr, main_ptr2);
    main_cache->deallocate(main_ptr2, 64);

    std::atomic<void*> other_thread_cached_ptr{nullptr};

    std::thread t([&]() {
        thread_cache* other_cache = get_thread_cache();

        // 其他线程分配内存
        void* ptr = other_cache->allocate(64);
        if (ptr) {
            other_cache->deallocate(ptr, 64);
            // 再次分配，应该重用自己的缓存
            void* ptr2 = other_cache->allocate(64);
            other_thread_cached_ptr.store(ptr2);
            other_cache->deallocate(ptr2, 64);
        }
    });

    t.join();

    // 验证：其他线程的缓存与主线程隔离
    ASSERT_NE(other_thread_cached_ptr.load(), main_ptr);
}

TEST(ThreadCache, ThreadLocalMultiRoundAllocation) {
    const int num_threads = 4;
    const int rounds = 10;
    const int allocs_per_round = 5;

    std::vector<std::thread> threads;
    std::atomic<int> total_success{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            thread_cache* cache = get_thread_cache();
            ASSERT_NOT_NULL(cache);

            for (int round = 0; round < rounds; ++round) {
                void* ptrs[allocs_per_round];
                int success = 0;

                // 批量分配
                for (int i = 0; i < allocs_per_round; ++i) {
                    ptrs[i] = cache->allocate(64);
                    if (ptrs[i]) {
                        memset(ptrs[i], t + round, 64);
                        success++;
                    }
                }

                // 批量释放
                for (int i = 0; i < allocs_per_round; ++i) {
                    if (ptrs[i]) {
                        cache->deallocate(ptrs[i], 64);
                    }
                }

                total_success += success;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有分配都成功
    ASSERT_EQ(total_success.load(), num_threads * rounds * allocs_per_round);
}

// ==================== 主函数 ====================

int main() {
    return test::run_all_tests();
}
