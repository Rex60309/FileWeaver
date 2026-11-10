#pragma once

#include "fileweaver/core/fuzzy.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace fileweaver {
enum class SearchMode : std::uint8_t { exact, substring, prefix, fuzzy };
enum class SearchSort : std::uint8_t { name, size, modified, relevance };
struct SearchFilters {
    std::optional<std::string> extension;
    std::optional<std::int64_t> minimum_size;
    std::optional<std::int64_t> maximum_size;
    std::optional<std::filesystem::path> root;
    // Strict UTC Unix-nanosecond bounds; neither boundary is included.
    std::optional<std::int64_t> modified_after;
    std::optional<std::int64_t> modified_before;
};
struct SearchRequest {
    std::filesystem::path database;
    std::string query;
    SearchMode mode = SearchMode::substring;
    std::size_t limit = 100;
    SearchFilters filters{};
    SearchSort sort = SearchSort::name;
    bool descending = false;
    FuzzyOptions fuzzy{};
};
struct SearchResult {
    std::int64_t file_id;
    std::int64_t root_id;
    std::string display_path;
    std::string name;
    std::int64_t size_bytes;
};
// Returns present indexed records only; does not revalidate live source files.
// Exact is binary case-sensitive; substring/prefix fold ASCII case only.
// Prefix matches the literal start of a filename. Limits 1..10000.
// Fuzzy requires relevance sort, descending=false, bounded candidates and valid UTF-8.
// Cancellation throws StorageError rather than returning partial results.
[[nodiscard]] std::vector<SearchResult> search_files(const SearchRequest &request,
                                                     const std::stop_token &stop = {});
} // namespace fileweaver
