#pragma once
#include "fileweaver/application/inspect.hpp"
#include <cstdint>
#include <memory>
#include <stop_token>

namespace fileweaver::desktop {
struct WatchTarget {
    std::filesystem::path database;
    RootInfo root;
};
enum class DirectoryChange : std::uint8_t { timeout, changed, overflow, cancelled };
class DirectoryChanges {
  public:
    virtual ~DirectoryChanges() = default;
    // Wait at most 200 ms normally; implementations honor cancellation.
    [[nodiscard]] virtual DirectoryChange poll(const std::stop_token &stop) = 0;
};
void validateWatchTarget(const WatchTarget &target);
[[nodiscard]] std::unique_ptr<DirectoryChanges> windowsDirectoryChanges(const WatchTarget &target);
} // namespace fileweaver::desktop
