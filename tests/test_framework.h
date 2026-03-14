#pragma once
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

// 测试结果统计
struct TestStats {
    int passed = 0;
    int failed = 0;
    int total = 0;
};

inline TestStats& get_stats() {
    static TestStats stats;
    return stats;
}

// 测试用例结构
struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_test_cases() {
    static std::vector<TestCase> cases;
    return cases;
}

// 注册测试用例
inline void register_test(const std::string& name, std::function<void()> func) {
    get_test_cases().push_back({name, func});
}

// 断言宏
#define ASSERT_TRUE(expr)                                                 \
    do {                                                                  \
        if (!(expr)) {                                                    \
            std::cerr << "  [失败] 断言为真失败: " << #expr << " 在行号 " \
                      << __LINE__ << std::endl;                           \
            throw std::runtime_error("断言失败");                         \
        }                                                                 \
    } while (0)

#define ASSERT_FALSE(expr)                                                \
    do {                                                                  \
        if (expr) {                                                       \
            std::cerr << "  [失败] 断言为假失败: " << #expr << " 在行号 " \
                      << __LINE__ << std::endl;                           \
            throw std::runtime_error("断言失败");                         \
        }                                                                 \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        if ((expected) != (actual)) {                                         \
            std::cerr << "  [失败] 断言相等失败: 期望 " << (expected)         \
                      << " 但实际得到 " << (actual) << " 在行号 " << __LINE__ \
                      << std::endl;                                           \
            throw std::runtime_error("断言失败");                             \
        }                                                                     \
    } while (0)

#define ASSERT_NE(expected, actual)                                         \
    do {                                                                    \
        if ((expected) == (actual)) {                                       \
            std::cerr << "  [失败] 断言不等失败: 期望不等于 " << (expected) \
                      << " 在行号 " << __LINE__ << std::endl;               \
            throw std::runtime_error("断言失败");                           \
        }                                                                   \
    } while (0)

#define ASSERT_GT(a, b)                                                   \
    do {                                                                  \
        if (!((a) > (b))) {                                               \
            std::cerr << "  [失败] 断言大于失败: " << (a) << " > " << (b) \
                      << " 在行号 " << __LINE__ << std::endl;             \
            throw std::runtime_error("断言失败");                         \
        }                                                                 \
    } while (0)

#define ASSERT_GE(a, b)                                                        \
    do {                                                                       \
        if (!((a) >= (b))) {                                                   \
            std::cerr << "  [失败] 断言大于等于失败: " << (a) << " >= " << (b) \
                      << " 在行号 " << __LINE__ << std::endl;                  \
            throw std::runtime_error("断言失败");                              \
        }                                                                      \
    } while (0)

#define ASSERT_LT(a, b)                                                   \
    do {                                                                  \
        if (!((a) < (b))) {                                               \
            std::cerr << "  [失败] 断言小于失败: " << (a) << " < " << (b) \
                      << " 在行号 " << __LINE__ << std::endl;             \
            throw std::runtime_error("断言失败");                         \
        }                                                                 \
    } while (0)

#define ASSERT_LE(a, b)                                                        \
    do {                                                                       \
        if (!((a) <= (b))) {                                                   \
            std::cerr << "  [失败] 断言小于等于失败: " << (a) << " <= " << (b) \
                      << " 在行号 " << __LINE__ << std::endl;                  \
            throw std::runtime_error("断言失败");                              \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(ptr)                                             \
    do {                                                             \
        if ((ptr) != nullptr) {                                      \
            std::cerr << "  [失败] 断言为空失败: 指针不为空 在行号 " \
                      << __LINE__ << std::endl;                      \
            throw std::runtime_error("断言失败");                    \
        }                                                            \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
    do {                                                                       \
        if ((ptr) == nullptr) {                                                \
            std::cerr << "  [失败] 断言非空失败: 指针为空 在行号 " << __LINE__ \
                      << std::endl;                                            \
            throw std::runtime_error("断言失败");                              \
        }                                                                      \
    } while (0)

// 测试用例定义宏
#define TEST(test_suite, test_name)                                \
    class test_suite##_##test_name##_Test {                        \
       public:                                                     \
        void run();                                                \
        static void register_test() {                              \
            test::register_test(#test_suite "." #test_name, []() { \
                test_suite##_##test_name##_Test t;                 \
                t.run();                                           \
            });                                                    \
        }                                                          \
    };                                                             \
    static struct test_suite##_##test_name##_Registrar {           \
        test_suite##_##test_name##_Registrar() {                   \
            test_suite##_##test_name##_Test::register_test();      \
        }                                                          \
    } test_suite##_##test_name##_registrar;                        \
    void test_suite##_##test_name##_Test::run()

// 运行所有测试
inline int run_all_tests() {
    std::cout << "========================================" << std::endl;
    std::cout << "正在运行 " << get_test_cases().size() << " 个测试..."
              << std::endl;
    std::cout << "========================================" << std::endl;

    auto& cases = get_test_cases();
    auto& stats = get_stats();

    for (auto& test_case : cases) {
        std::cout << "[运行] " << test_case.name << std::endl;
        auto start = std::chrono::high_resolution_clock::now();

        try {
            test_case.func();
            auto end = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                      start);
            std::cout << "  [通过] (" << duration.count() << " 毫秒)"
                      << std::endl;
            stats.passed++;
        } catch (const std::exception& e) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                      start);
            std::cout << "  [失败] (" << duration.count() << " 毫秒) - "
                      << e.what() << std::endl;
            stats.failed++;
        }
        stats.total++;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "结果: " << stats.passed << " 通过, " << stats.failed
              << " 失败, " << stats.total << " 总计" << std::endl;
    std::cout << "========================================" << std::endl;

    return stats.failed > 0 ? 1 : 0;
}

}  // namespace test
