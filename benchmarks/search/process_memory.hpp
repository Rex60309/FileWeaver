#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(__linux__)
#include <sys/resource.h>
#endif

namespace fileweaver::benchmark {
struct MemoryReading {
    std::optional<std::uint64_t> bytes;
    std::string_view metric;
    std::string_view source;
    std::string_view status;
};

// OS process-lifetime high-water marks, not current bytes or per-query allocations.
inline MemoryReading peak_memory() noexcept {
#ifdef _WIN32
    MemoryReading reading{std::nullopt, "peak_working_set",
                          "GetProcessMemoryInfo.PeakWorkingSetSize", "os_query_failed"};
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, counters.cb) == 0) {
        return reading;
    }
    if (counters.PeakWorkingSetSize == 0) {
        reading.status = "invalid_os_value";
        return reading;
    }
    reading.bytes = static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    reading.status = "available";
    return reading;
#elif defined(__linux__)
    MemoryReading reading{std::nullopt, "peak_resident_set", "getrusage(RUSAGE_SELF).ru_maxrss",
                          "os_query_failed"};
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return reading;
    }
    if (usage.ru_maxrss <= 0) {
        reading.status = "invalid_os_value";
        return reading;
    }
    const auto kibibytes = static_cast<std::uint64_t>(usage.ru_maxrss);
    if (kibibytes > (std::numeric_limits<std::uint64_t>::max)() / 1024U) {
        reading.status = "counter_overflow";
        return reading;
    }
    reading.bytes = kibibytes * 1024U;
    reading.status = "available";
    return reading;
#else
    return {std::nullopt, "unavailable", "none", "unsupported_platform"};
#endif
}
} // namespace fileweaver::benchmark
