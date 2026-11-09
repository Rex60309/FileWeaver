#pragma once

#include <cstdint>
#include <string_view>

namespace fileweaver {
// Strict Gregorian YYYY-MM-DDTHH:MM:SSZ, whole seconds, no leap seconds.
// Returns signed Unix nanoseconds; throws StorageError (2) on invalid/range errors.
[[nodiscard]] std::int64_t parse_utc_time(std::string_view text);
} // namespace fileweaver
