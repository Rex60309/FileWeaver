#pragma once
#include <cstdint>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>
namespace fileweaver {
class GlobMatcher {
  public:
    explicit GlobMatcher(std::string_view pattern);
    [[nodiscard]] bool matches(std::string_view name, const std::stop_token &stop = {}) const;

  private:
    enum class Kind : std::uint8_t { literal, any, star, group };
    struct Token {
        Kind kind = Kind::literal;
        char32_t value = 0;
        bool negative = false;
        std::vector<std::pair<char32_t, char32_t>> ranges;
    };
    std::vector<Token> tokens_;
};
} // namespace fileweaver
