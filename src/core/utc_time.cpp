#include "fileweaver/core/utc_time.hpp"
#include "fileweaver/core/storage_error.hpp"

#include <chrono>
#include <limits>

namespace fileweaver {
std::int64_t parse_utc_time(std::string_view text) {
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
        throw StorageError("Time must use UTC YYYY-MM-DDTHH:MM:SSZ", 2);
    }
    const auto digits = [&](std::size_t offset, std::size_t length) {
        int value = 0;
        for (std::size_t index = offset; index < offset + length; ++index) {
            if (text[index] < '0' || text[index] > '9') {
                throw StorageError("UTC time contains invalid digits", 2);
            }
            value = value * 10 + (text[index] - '0');
        }
        return value;
    };
    const int year = digits(0, 4);
    const unsigned month = static_cast<unsigned>(digits(5, 2));
    const unsigned day = static_cast<unsigned>(digits(8, 2));
    const int hour = digits(11, 2);
    const int minute = digits(14, 2);
    const int second = digits(17, 2);
    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month},
                                           std::chrono::day{day}};
    if (year == 0 || !date.ok() || hour > 23 || minute > 59 || second > 59) {
        throw StorageError("Invalid UTC calendar date or time", 2);
    }
    const auto days =
        static_cast<std::int64_t>(std::chrono::sys_days{date}.time_since_epoch().count());
    const std::int64_t seconds =
        days * 86400 + std::int64_t{hour} * 3600 + std::int64_t{minute} * 60 + second;
    constexpr std::int64_t billion = 1000000000;
    if (seconds > (std::numeric_limits<std::int64_t>::max)() / billion ||
        seconds < (std::numeric_limits<std::int64_t>::min)() / billion) {
        throw StorageError("UTC time exceeds signed Unix-nanosecond storage range", 2);
    }
    return seconds * billion;
}
} // namespace fileweaver
