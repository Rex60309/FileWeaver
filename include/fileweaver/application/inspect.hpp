#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace fileweaver {
struct RootInfo {
    std::int64_t root_id = 0;
    std::string path;
    std::string last_scan_status;
    std::optional<std::int64_t> last_scan_at;
    bool policy_known = false;
    std::optional<std::int64_t> maximum_depth;
    std::vector<std::string> exclusions;
};
struct CatalogStatus {
    int schema_version = 0;
    std::int64_t roots = 0;
    std::int64_t present_files = 0;
    std::int64_t missing_files = 0;
    std::int64_t scan_records = 0;
    // Records marked running can represent interrupted processes, not live jobs.
    std::int64_t running_scan_records = 0;
};
[[nodiscard]] std::vector<RootInfo> list_roots(const std::filesystem::path &database,
                                               const std::stop_token &stop = {});
[[nodiscard]] CatalogStatus catalog_status(const std::filesystem::path &database,
                                           const std::stop_token &stop = {});
} // namespace fileweaver
