#pragma once

#include <stdexcept>
#include <string>

namespace fileweaver {
class StorageError final : public std::runtime_error {
  public:
    StorageError(const std::string &message, int code) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] int exit_code() const noexcept { return code_; }

  private:
    int code_;
};
} // namespace fileweaver
