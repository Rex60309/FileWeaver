#include "fileweaver/core/fuzzy.hpp"
#include "fileweaver/core/storage_error.hpp"
#include "fileweaver/core/text.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace fileweaver {
namespace {
void checkpoint(const std::stop_token &stop) {
    if (stop.stop_requested()) {
        throw StorageError("Search cancelled", 1);
    }
}
bool space(char32_t value) { return value == U' ' || (value >= U'\t' && value <= U'\r'); }
bool separator(char32_t value) {
    return value < 128 && !((value >= U'a' && value <= U'z') || (value >= U'0' && value <= U'9'));
}
std::vector<std::u32string_view> words(std::u32string_view name) {
    std::vector<std::u32string_view> result;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || separator(name[i])) {
            if (begin < i) {
                result.emplace_back(name.data() + begin, i - begin);
            }
            begin = i + 1;
        }
    }
    return result;
}
struct Subsequence {
    std::u32string_view query;
    std::u32string_view name;
};
bool subsequence(Subsequence text) {
    const auto [query, name] = text;
    std::size_t i = 0;
    for (const auto value : name) {
        if (i < query.size() && query[i] == value) {
            ++i;
        }
    }
    return i == query.size();
}
std::size_t tolerance(std::size_t length) { return length < 3 ? 0 : length < 6 ? 1 : 2; }
std::vector<std::uint64_t> grams(std::u32string_view text) {
    std::vector<std::uint64_t> result;
    for (std::size_t i = 1; i < text.size(); ++i) {
        result.push_back((static_cast<std::uint64_t>(text[i - 1]) << 32U) |
                         static_cast<std::uint64_t>(text[i]));
    }
    std::sort(result.begin(), result.end());
    return result;
}
bool possibleTypo(std::u32string_view query, std::u32string_view word) {
    const auto edits = tolerance(query.size());
    const auto longer = std::max(query.size(), word.size());
    const auto shorter = std::min(query.size(), word.size());
    if (edits == 0 || longer - shorter > edits) {
        return false;
    }
    // Every edit changes the multiset overlap by at most one. This necessary
    // bound also rejects disjoint scripts when the bigram lower bound is zero.
    auto query_letters = std::u32string(query);
    auto word_letters = std::u32string(word);
    std::sort(query_letters.begin(), query_letters.end());
    std::sort(word_letters.begin(), word_letters.end());
    std::size_t q = 0, w = 0, overlap = 0;
    while (q < query_letters.size() && w < word_letters.size()) {
        if (query_letters[q] == word_letters[w]) {
            ++overlap;
            ++q;
            ++w;
        } else if (query_letters[q] < word_letters[w]) {
            ++q;
        } else {
            ++w;
        }
    }
    if (overlap < longer - edits) {
        return false;
    }
    const auto required = longer > 1 + 2 * edits ? longer - 1 - 2 * edits : 0;
    if (required == 0) {
        return true;
    }
    const auto left = grams(query);
    const auto right = grams(word);
    std::size_t a = 0, b = 0, shared = 0;
    while (a < left.size() && b < right.size()) {
        if (left[a] == right[b]) {
            ++shared;
            ++a;
            ++b;
        } else if (left[a] < right[b]) {
            ++a;
        } else {
            ++b;
        }
    }
    return shared >= required;
}
// Banded Levenshtein: only cheap-gated words, at most two edits, two bounded rows.
std::size_t distance(std::u32string_view query, std::u32string_view word,
                     const std::stop_token &stop) {
    const auto edits = tolerance(query.size());
    const auto infinity = edits + 1;
    std::vector<std::size_t> previous(word.size() + 1, infinity), current(previous);
    for (std::size_t j = 0; j <= std::min(word.size(), edits); ++j) {
        previous[j] = j;
    }
    for (std::size_t i = 1; i <= query.size(); ++i) {
        checkpoint(stop);
        std::fill(current.begin(), current.end(), infinity);
        if (i <= edits) {
            current[0] = i;
        }
        const auto begin = i > edits ? i - edits : 1;
        const auto end = std::min(word.size(), i + edits);
        for (auto j = begin; j <= end; ++j) {
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                                   previous[j - 1] + (query[i - 1] == word[j - 1] ? 0U : 1U)});
        }
        previous.swap(current);
    }
    return previous.back();
}
} // namespace
FuzzyMatcher::FuzzyMatcher(std::string_view query, FuzzyWeights weights)
    : query_(decode_utf8(query, {128, 2, true})), weights_(weights) {
    for (const int weight : {weights.exact, weights.prefix, weights.token, weights.subsequence,
                             weights.typo, weights.path}) {
        if (weight < 0 || weight > 10000) {
            throw StorageError("Fuzzy weights require 0..10000", 2);
        }
    }
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= query_.size(); ++i) {
        if (i == query_.size() || space(query_[i])) {
            if (begin < i) {
                tokens_.push_back(query_.substr(begin, i - begin));
            }
            begin = i + 1;
        }
    }
    if (tokens_.empty() || tokens_.size() > 16) {
        throw StorageError("Fuzzy query requires 1..16 tokens and at most 128 Unicode characters",
                           2);
    }
}
namespace {
std::vector<std::u32string_view> pieces(const std::u32string &name, const std::u32string &path) {
    std::vector<std::u32string_view> result{name};
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == U'/' || path[i] == U'\\') {
            const auto part = std::u32string_view(path).substr(begin, i - begin);
            if (!part.empty() && part != name) {
                result.push_back(part);
            }
            begin = i + 1;
        }
    }
    if (result.size() > 512) {
        throw StorageError("Fuzzy path exceeds 512 components", 5);
    }
    return result;
}
} // namespace
bool FuzzyMatcher::candidate(std::string_view text, const std::stop_token &stop,
                             std::string_view path) const {
    checkpoint(stop);
    const auto name = decode_utf8(text, {1024, 5, true});
    const auto parent = decode_utf8(path, {32760, 5, true});
    const auto components = pieces(name, parent);
    for (const auto &token : tokens_) {
        checkpoint(stop);
        bool accepted = false;
        for (const auto component : components) {
            if (subsequence({token, component})) {
                accepted = true;
                break;
            }
            const auto word_views = words(component);
            if (std::any_of(word_views.begin(), word_views.end(),
                            [&](auto word) { return possibleTypo(token, word); })) {
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            return false;
        }
    }
    return true;
}
std::optional<std::int64_t> FuzzyMatcher::score(std::string_view text, const std::stop_token &stop,
                                                std::string_view path) const {
    checkpoint(stop);
    const auto name = decode_utf8(text, {1024, 5, true});
    const auto parent = decode_utf8(path, {32760, 5, true});
    const auto components = pieces(name, parent);
    std::int64_t total = name == query_ ? weights_.exact : 0;
    if (name.starts_with(query_)) {
        total += weights_.prefix;
    }
    for (const auto &token : tokens_) {
        checkpoint(stop);
        std::optional<std::int64_t> best;
        for (std::size_t i = 0; i < components.size(); ++i) {
            const auto component = components[i];
            const auto offer = [&](std::int64_t value) {
                if (i != 0) {
                    value = std::min(value, static_cast<std::int64_t>(weights_.path));
                }
                best = best ? std::max(*best, value) : value;
            };
            if (component.find(token) != std::u32string_view::npos) {
                offer(weights_.token);
            }
            if (subsequence({token, component})) {
                offer(weights_.subsequence);
            }
            for (const auto word : words(component)) {
                if (!possibleTypo(token, word)) {
                    continue;
                }
                const auto edits = distance(token, word, stop);
                if (edits > 0 && edits <= tolerance(token.size())) {
                    offer(static_cast<std::int64_t>(weights_.typo) -
                          static_cast<std::int64_t>(edits));
                }
            }
        }
        if (!best) {
            return std::nullopt;
        }
        total += *best;
    }
    return total;
}
} // namespace fileweaver
