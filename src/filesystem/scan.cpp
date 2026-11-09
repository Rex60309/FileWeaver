#include "fileweaver/filesystem/scan.hpp"
#include "fileweaver/core/storage_error.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fileweaver {
namespace {
constexpr std::size_t depth_limit = 128;

bool reparse_point(const std::filesystem::path &path, std::error_code &error) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    error.clear();
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_symlink(status);
#endif
}

bool same_path(const std::filesystem::path &left, const std::filesystem::path &right) {
#ifdef _WIN32
    const auto &a = left.native();
    const auto &b = right.native();
    if (a.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        b.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

void validate_root(const std::filesystem::path &root) {
    std::error_code error;
    // Check every ancestor too: an ordinary directory beneath a junction must
    // not silently authorize traversal through that junction.
    for (auto path = root; !path.empty();) {
        if (reparse_point(path, error) || error) {
            throw StorageError("Scan root is inaccessible or traverses a link/reparse point", 3);
        }
        const auto parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    if (!std::filesystem::is_directory(root, error) || error) {
        throw StorageError("Scan root must be an accessible directory", 3);
    }
}
} // namespace

ScanSummary scan_directory(const std::filesystem::path &root, const ScanOptions &options,
                           const std::function<void(const FileMetadata &)> &sink,
                           const std::stop_token &stop) {
    if (!sink) {
        throw std::invalid_argument("A metadata sink is required");
    }
    if (root.empty() || root.native().find(std::filesystem::path::value_type{}) !=
                            std::filesystem::path::string_type::npos) {
        throw StorageError("An explicit scan root is required", 3);
    }
#ifdef _WIN32
    auto preferred_root = root;
    preferred_root.make_preferred();
    if (preferred_root.native().starts_with(L"\\\\")) {
        throw StorageError("Windows network/device namespace roots are not supported yet", 3);
    }
#endif
    if (options.maximum_depth == 0 || options.maximum_depth > depth_limit) {
        throw std::invalid_argument("Maximum scan depth must be between 1 and 128");
    }
    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(root, error);
    if (error) {
        throw StorageError("Cannot resolve scan root", 3);
    }
    validate_root(absolute_root);
    std::vector<std::filesystem::path> excluded;
    excluded.reserve(options.excluded_paths.size());
    for (const auto &path : options.excluded_paths) {
        if (path.empty() || path.native().find(std::filesystem::path::value_type{}) !=
                                std::filesystem::path::string_type::npos) {
            throw std::invalid_argument("Excluded paths must not be empty");
        }
        auto absolute = path.is_absolute() ? path : absolute_root / path;
        excluded.push_back(absolute.lexically_normal());
    }
    const auto is_excluded = [&](const std::filesystem::path &path) {
        const auto normalized = path.lexically_normal();
        return std::any_of(excluded.begin(), excluded.end(),
                           [&](const auto &candidate) { return same_path(normalized, candidate); });
    };
    ScanSummary summary;
    if (stop.stop_requested()) {
        summary.cancelled = true;
        summary.complete = false;
        return summary;
    }
    if (is_excluded(absolute_root)) {
        ++summary.skipped;
        return summary;
    }
    std::vector<std::filesystem::directory_iterator> stack;
    stack.reserve(options.maximum_depth);
    stack.emplace_back(absolute_root, error);
    if (error) {
        throw StorageError("Cannot enumerate scan root", 3);
    }
    const std::filesystem::directory_iterator end;
    const auto record_error = [&] {
        ++summary.errors;
        ++summary.skipped;
        summary.complete = false;
    };
    while (!stack.empty()) {
        if (stop.stop_requested()) {
            summary.cancelled = true;
            summary.complete = false;
            break;
        }
        auto &current = stack.back();
        if (current == end) {
            stack.pop_back();
            continue;
        }
        const auto entry = *current;
        current.increment(error);
        if (error) {
            record_error();
            // Abandon this iterator, preserving valid parent/sibling iterators.
            current = end;
        }
        const auto &path = entry.path();
        if (is_excluded(path)) {
            ++summary.skipped;
            continue;
        }
        const bool linked = reparse_point(path, error);
        if (error) {
            record_error();
            continue;
        }
        if (linked) {
            ++summary.skipped;
            continue;
        }
        const auto status = entry.symlink_status(error);
        if (error) {
            record_error();
            continue;
        }
        if (std::filesystem::is_directory(status)) {
            if (stack.size() >= options.maximum_depth) {
                record_error();
                continue;
            }
            std::filesystem::directory_iterator child(path, error);
            if (error) {
                record_error();
                continue;
            }
            stack.push_back(std::move(child));
        } else if (std::filesystem::is_regular_file(status)) {
            const auto size = entry.file_size(error);
            if (error) {
                record_error();
                continue;
            }
            const auto modified = entry.last_write_time(error);
            if (error) {
                record_error();
                continue;
            }
            sink(FileMetadata{path, size, modified});
            ++summary.files;
        } else {
            ++summary.skipped;
        }
    }
    return summary;
}
} // namespace fileweaver
