#pragma once

#include "fileweaver/filesystem/scan.hpp"
#include <cstdint>
#include <functional>
#include <optional>

namespace fileweaver {
struct IndexRequest {
    std::filesystem::path database;
    std::filesystem::path root;
    ScanOptions scan;
    // Refresh uses this to reject changed policies/removed root registrations.
    std::optional<std::int64_t> expected_root_id{};
};
struct IndexSummary {
    ScanSummary scan;
    std::int64_t added = 0;
    std::int64_t updated = 0;
    std::int64_t missing = 0;
    bool missing_reconciled = false;
};
// Clock returns UTC Unix nanoseconds. No source contents are read or changed.
// Requires an initialized v3 database. Cancellation/errors retain prior metadata.
[[nodiscard]] IndexSummary index_directory(const IndexRequest &request,
                                           const std::function<std::int64_t()> &clock,
                                           const std::stop_token &stop = {});
} // namespace fileweaver
