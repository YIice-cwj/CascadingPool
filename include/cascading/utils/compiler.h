#pragma once
#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <cstddef>
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#elif defined(_MSC_VER)
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

/**
 * @brief 分配内存块
 * @param size 内存块大小
 * @return 分配成功返回内存块地址，否则返回 nullptr
 */
inline void* allocate_memory(std::size_t size) {
    void* result_addr = nullptr;
#if defined(_WIN32) || defined(_WIN64)
    result_addr =
        VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    result_addr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    return (result_addr != nullptr) ? result_addr : nullptr;
}

/**
 * @brief 完全释放指定内存块物理内存和虚拟地址空间
 * @param addr 内存块地址
 * @param size 内存块大小
 * @return 0 成功, -1 失败
 */
inline int release_memory(void* addr, size_t size) {
    int result = 0;
#if defined(_WIN32) || defined(_WIN64)
    size = 0;
    result = VirtualFree(addr, size, MEM_RELEASE) ? 0 : -1;
#else
    result = munmap(addr, size) == 0 ? 0 : -1;
#endif
    return result;
}

/**
 * @brief 尝试在指定地址重新分配内存
 * @param addr 期望的地址
 * @param size 内存大小
 * @return 成功返回实际地址（可能是 addr 或新地址），失败返回 nullptr
 */
inline void* reuse_memory(void* addr, size_t size) {
    void* result_addr = nullptr;
#if defined(_WIN32) || defined(_WIN64)
    result_addr =
        VirtualAlloc(addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (result_addr == addr) {
        return result_addr;
    }
    if (result_addr != nullptr) {
        VirtualFree(result_addr, 0, MEM_RELEASE);
    }
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT,
                        PAGE_READWRITE);

#else
#ifdef MAP_FIXED_NOREPLACE
    result_addr =
        mmap(addr, size, PROT_READ | PROT_WRITE,
             MAP_FIXED_NOREPLACE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (result_addr != MAP_FAILED) {
        return result_addr;
    }
#endif
    return mmap(nullptr, size, PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
#endif
}

/**
 * @brief 提交指定内存块物理内存
 * @param addr 内存块地址
 * @param size 内存块大小
 * @return 0 成功, -1 失败
 */
inline int commit_memory(void* addr, size_t size) {
    int result = 0;
#if defined(_WIN32) || defined(_WIN64)
    result = VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) ? 0 : -1;
#else
    void* ret = mmap(addr, size, PROT_READ | PROT_WRITE,
                     MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    result = (ret != MAP_FAILED) ? 0 : -1;
#endif
    return result;
}

/**
 * @brief 释放指定内存块物理内存
 * @param addr 内存块地址
 * @param size 内存块大小
 * @return 0 成功, -1 失败
 */
inline int decommit_memory(void* addr, size_t size) {
    int result = 0;
#if defined(_WIN32) || defined(_WIN64)
    result = VirtualFree(addr, size, MEM_DECOMMIT) ? 0 : -1;
#else
    result = madvise(addr, size, MADV_DONTNEED) == 0 ? 0 : -1;
#endif
    return result;
}

/**
 * @brief 查找第一个 0 bit（空闲区域）
 * @details 使用 __builtin_ctzll (GCC/Clang) 或 _BitScanForward64 (MSVC)
 * 快速查找
 * @param word 要检查的 64 位字
 * @return 找到的区域索引（0-63），失败返回 64
 */
inline std::uint16_t find_first_zero(std::uint64_t word) {
    if (word == ~0ULL) {
        return 64;
    }
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, ~word);
    return static_cast<std::uint16_t>(index);
#else
    return static_cast<std::uint16_t>(__builtin_ctzll(~word));
#endif
}

constexpr std::size_t PAGE_SIZE = 4096;
constexpr std::size_t LARGE_PAGE_SIZE = 65536;

inline constexpr std::size_t align_up(std::size_t size, std::size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

inline constexpr std::size_t align_to_page(std::size_t size) {
    return align_up(size, PAGE_SIZE);
}

inline constexpr std::size_t align_to_large_page(std::size_t size) {
    return align_up(size, LARGE_PAGE_SIZE);
}