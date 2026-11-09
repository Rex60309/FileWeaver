#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <vector>

namespace fileweaver {
struct FileMetadata {
    std::filesystem::path path;
    std::uintmax_t size_bytes;
    std::filesystem::file_time_type modified_at;
};

struct ScanOptions {
    // Excludes exactly these paths and their descendants. Relative paths are
    // interpreted against the explicitly selected root.
    std::vector<std::filesystem::path> excluded_paths;
    // Includes the root iterator. Must be between 1 and 128.
    std::size_t maximum_depth = 128;
};

struct ScanSummary {
    std::uint64_t files = 0;
    std::uint64_t skipped = 0;
    std::uint64_t errors = 0;
    bool cancelled = false;
    bool complete = true;
};

// Streams regular-file metadata without reading contents. The sink runs on the
// calling thread. Sink exceptions propagate immediately. Traversal order is not
// specified; consumers must explicitly sort when exposing ordered results.
// Invalid/inaccessible/reparse roots throw StorageError (exit code 3).
[[nodiscard]] ScanSummary scan_directory(const std::filesystem::path &root,
                                         const ScanOptions &options,
                                         const std::function<void(const FileMetadata &)> &sink,
                                         const std::stop_token &stop = {});
} // namespace fileweaver
