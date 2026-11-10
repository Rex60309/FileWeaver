#pragma once

#include "fileweaver/application/index.hpp"
#include <optional>
#include <string>
#include <vector>

namespace fileweaver {
struct RefreshRootResult {
    std::int64_t root_id = 0;
    std::filesystem::path root;
    bool attempted = false;
    int exit_code = 1;
    std::string error = "Not attempted: refresh cancelled";
    std::optional<IndexSummary> index;
};
struct RefreshSummary {
    std::vector<RefreshRootResult> roots;
    bool cancelled = false;
    bool complete = false;
};
// Snapshot registered roots and saved policies; never invent default policies
// for migrated roots. Root failures are isolated; metadata is never erased here.
[[nodiscard]] RefreshSummary refresh_index(const std::filesystem::path &database,
                                           const std::function<std::int64_t()> &clock,
                                           const std::stop_token &stop = {});
struct RootRefreshRequest {
    std::filesystem::path database;
    std::int64_t root_id = 0;
    std::filesystem::path expected_root;
};
// Refresh exactly the selected registration; reject changed identity/path before scanning.
[[nodiscard]] RefreshRootResult refresh_root(const RootRefreshRequest &request,
                                             const std::function<std::int64_t()> &clock,
                                             const std::stop_token &stop = {});
} // namespace fileweaver
