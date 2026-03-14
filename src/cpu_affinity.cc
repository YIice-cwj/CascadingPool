#include "cascading/cpu_affinity.h"

namespace cascading {

std::atomic<int> cpu_affinity::next_cpu_selector_ = {0};

thread_local int cpu_affinity::tls_cpu_id_ = {-1};

thread_local bool cpu_affinity::tls_initialized_ = {false};

/**
 * @brief 获取当前运行的CPU ID
 */
int cpu_affinity::current() {
#ifdef __linux__
    return sched_getcpu();
#else
    PROCESSOR_NUMBER pn;
    GetCurrentProcessorNumberEx(&pn);
    return pn.Group * 64 + pn.Number;
#endif
}

/**
 * @brief
 */
static bool bind_cpu(std::vector<int> cpu_ids) {
    if (cpu_ids.empty()) {
        return false;
    }

#ifdef __linux__

    int max_cpu = *std::max_element(cpu_ids.begin(), cpu_ids.end());

    size_t set_size = CPU_ALLOC_SIZE(max_cpu + 1);
    cpu_set_t* cpu_set = CPU_ALLOC(max_cpu + 1);
    if (!cpu_set) {
        return false;
    }

    CPU_ZERO_S(set_size, cpu_set);

    for (int cpu_id : cpu_ids) {
        CPU_SET_S(cpu_id, set_size, cpu_set);
    }

    if (pthread_setaffinity_np(pthread_self(), set_size, cpu_set);
        CPU_FREE(cpu_set) != 0) {
        return false;
    }

#elif _WIN32 || _WIN64

    std::map<WORD, uint64_t> group_masks;
    for (int cpu_id : cpu_ids) {
        WORD group = static_cast<WORD>(cpu_id / 64);
        int number = cpu_id % 64;
        group_masks[group] |= (1ULL << number);
    }

    for (auto& [group, mask] : group_masks) {
        GROUP_AFFINITY ga = {};
        ga.Mask = mask;
        ga.Group = group;

        if (!SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr)) {
            return false;
        }
    }
#endif
    return true;
}

/**
 * @brief 绑定当前线程到指定cpu
 * @param cpu_id cpu id列
 * @return bool, true:绑定成功, false:绑定失败
 */
bool cpu_affinity::bind(int cpu_id) {
    if (cpu_id < 0) {
        return false;
    }
    return bind(std::vector<int>{cpu_id});
}

/**
 * @brief 绑定当前线程到指定cpu
 * @param cpu_ids cpu id列表
 * @return bool, true:绑定成功, false:绑定失败
 */
bool cpu_affinity::bind(std::initializer_list<int> cpu_ids) {
    return bind(std::vector<int>(cpu_ids));
}

/**
 * @brief 绑定当前线程到指定cpu
 * @param cpu_ids cpu id列表
 * @return bool, true:绑定成功, false:绑定失败
 */
bool cpu_affinity::bind(std::vector<int> cpu_ids) {
    if (cpu_ids.empty()) {
        return false;
    }

    for (int cpu : cpu_ids) {
        if (cpu < 0) {
            return false;
        }
    }

    if (!bind_cpu(cpu_ids)) {
        return false;
    }

    return true;
}

/**
 * @brief 绑定当前线程到指定cpu区间
 * @param cpu_begin cpu 区间开始
 * @param cpu_end cpu 区间结束
 * @return bool, true:绑定成功, false:绑定失败
 */
bool cpu_affinity::bind(int cpu_begin, int cpu_end) {
    if (cpu_begin > cpu_end || cpu_begin < 0) {
        return false;
    }

    std::vector<int> cpu_ids;
    cpu_ids.reserve(cpu_end - cpu_begin + 1);

    for (int i = cpu_begin; i <= cpu_end; ++i) {
        cpu_ids.push_back(i);
    }

    return bind(cpu_ids);
}

/**
 * @brief 绑定当前线程到任意cpu
 * @return bool, true:绑定成功, false:绑定失败
 */
bool cpu_affinity::bind_any() {
    int cpu_count = count();
    if (cpu_count <= 0)
        return false;

    int cpu_id =
        next_cpu_selector_.fetch_add(1, std::memory_order_relaxed) % cpu_count;

    return bind(cpu_id);
}

/**
 * @brief 绑定当前线程到任意cpu
 * @param cpu_num 绑定的cpu数量
 * @return bool, true:绑定成功, false:绑定失败
 */
bool cpu_affinity::bind_any(int cpu_num) {
    if (cpu_num <= 0) {
        return false;
    }

    int cpu_count = count();
    if (cpu_num > cpu_count) {
        return false;
    }

    int start =
        next_cpu_selector_.fetch_add(cpu_num, std::memory_order_relaxed) %
        cpu_count;

    std::vector<int> cpu_ids;
    cpu_ids.reserve(cpu_num);

    for (int i = 0; i < cpu_num; ++i) {
        cpu_ids.push_back((start + i) % cpu_count);
    }

    return bind(cpu_ids);
}

/**
 * @brief 获取当前线程绑定的cpu id
 * @return std::vector<int>, cpu_ids
 */
std::vector<int> cpu_affinity::get_bind_cpu_id() {
    std::vector<int> result;

#ifdef __linux__
    int cpu_count = count();
    size_t set_size = CPU_ALLOC_SIZE(cpu_count);
    cpu_set_t* cpu_set = CPU_ALLOC(cpu_count);

    if (!cpu_set)
        return result;

    if (pthread_getaffinity_np(pthread_self(), set_size, cpu_set) == 0) {
        for (int i = 0; i < cpu_count; ++i) {
            if (CPU_ISSET_S(i, set_size, cpu_set)) {
                result.push_back(i);
            }
        }
    }

    CPU_FREE(cpu_set);
#else
    GROUP_AFFINITY ga;
    if (GetThreadGroupAffinity(GetCurrentThread(), &ga)) {
        for (int i = 0; i < 64; ++i) {
            if (ga.Mask & (1ULL << i)) {
                result.push_back(ga.Group * 64 + i);
            }
        }
    }
#endif

    return result;
}

/**
 * @brief 获取系统cpu数量
 * @return int, cpu数量
 */
int cpu_affinity::count() {
#ifdef __linux__
    int n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
#else
    DWORD bufferSize = 0;

    // 第一次调用获取所需缓冲区大小
    GetLogicalProcessorInformationEx(RelationGroup, nullptr, &bufferSize);
    if (bufferSize == 0) {
        return 1;
    }

    // 分配缓冲区
    std::vector<uint8_t> buffer(bufferSize);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            buffer.data());

    // 第二次调用获取实际数据
    if (!GetLogicalProcessorInformationEx(RelationGroup, info, &bufferSize)) {
        return 1;
    }

    // 遍历所有结构，累加所有组的处理器数量
    int totalCpus = 0;
    DWORD offset = 0;

    while (offset < bufferSize) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX current =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buffer.data() + offset);

        if (current->Relationship == RelationGroup) {
            // 遍历所有处理器组
            for (WORD i = 0; i < current->Group.ActiveGroupCount; ++i) {
                totalCpus += current->Group.GroupInfo[i].ActiveProcessorCount;
            }
            break;  // RelationGroup 只有一个条目
        }

        offset += current->Size;
    }

    return totalCpus > 0 ? totalCpus : 1;
#endif
}

}  // namespace cascading