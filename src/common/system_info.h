#pragma once

#include <cstdint>
#include <cstdio>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace vivid {

// Returns current process resident memory in bytes (0 on failure).
inline uint64_t get_process_memory_bytes() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long pages = 0;
    // statm: size resident shared text lib data dt — we want resident (2nd field)
    if (std::fscanf(f, "%*lu %lu", &pages) != 1) pages = 0;
    std::fclose(f);
    return static_cast<uint64_t>(pages) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

// Formats bytes into a human-readable string (e.g. "142.3 MB").
// Writes into the provided buffer and returns a pointer to it.
inline const char* format_memory(char* buf, size_t buf_size, uint64_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        std::snprintf(buf, buf_size, "%.1f GB", mb / 1024.0);
    } else {
        std::snprintf(buf, buf_size, "%.1f MB", mb);
    }
    return buf;
}

} // namespace vivid
