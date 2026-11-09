#pragma once

#include <filesystem>

namespace fileweaver {
// Creates or validates FileWeaver storage at an explicitly selected path.
// Throws StorageError on storage or schema failure; does not scan any files.
void initialize_database(const std::filesystem::path &path);
} // namespace fileweaver
