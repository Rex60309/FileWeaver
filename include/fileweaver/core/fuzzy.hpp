#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace fileweaver {
struct FuzzyWeights {
    int exact = 1000;
    int prefix = 500;
    int token = 300;
    int subsequence = 100;
    int typo = 50;
};
struct FuzzyOptions {
    std::size_t candidate_limit = 4096;
    FuzzyWeights weights{};
};
// Unicode scalar matching, ASCII case folding only. No filesystem or Qt dependency.
class FuzzyMatcher {
  public:
    explicit FuzzyMatcher(std::string_view query, FuzzyWeights weights = {});
    [[nodiscard]] bool candidate(std::string_view name, const std::stop_token &stop = {}) const;
    [[nodiscard]] std::optional<std::int64_t> score(std::string_view name,
                                                    const std::stop_token &stop = {}) const;

  private:
    std::u32string query_;
    std::vector<std::u32string> tokens_;
    FuzzyWeights weights_;
};
} // namespace fileweaver
