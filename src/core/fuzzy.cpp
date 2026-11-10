#include "fileweaver/core/fuzzy.hpp"
#include "fileweaver/core/storage_error.hpp"
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
struct DecodePolicy {
    std::size_t limit;
    int error_code;
};
std::u32string decode(std::string_view text, DecodePolicy policy) {
    const auto [limit, error_code] = policy;
    std::u32string result;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        std::uint32_t value = lead;
        std::size_t remaining = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            value = lead & 0x1fU;
            remaining = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            value = lead & 0x0fU;
            remaining = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            value = lead & 0x07U;
            remaining = 3;
            minimum = 0x10000;
        } else if (lead >= 0x80) {
            throw StorageError("Fuzzy search requires valid UTF-8", error_code);
        }
        if (remaining > text.size() - i) {
            throw StorageError("Fuzzy search requires valid UTF-8", error_code);
        }
        while (remaining > 0) {
            --remaining;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0U) != 0x80U) {
                throw StorageError("Fuzzy search requires valid UTF-8", error_code);
            }
            value = (value << 6U) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            value == 0) {
            throw StorageError("Fuzzy search requires valid UTF-8 without NUL", error_code);
        }
        if (result.size() == limit) {
            throw StorageError("Fuzzy text exceeds Unicode length limit", error_code);
        }
        if (value >= 'A' && value <= 'Z') {
            value += 'a' - 'A';
        }
        result.push_back(static_cast<char32_t>(value));
    }
    return result;
}
bool space(char32_t value) { return value == U' ' || (value >= U'\t' && value <= U'\r'); }
bool separator(char32_t value) {
    return value < 128 && !((value >= U'a' && value <= U'z') || (value >= U'0' && value <= U'9'));
}
std::vector<std::u32string_view> words(const std::u32string &name) {
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
    : query_(decode(query, {128, 2})), weights_(weights) {
    for (const int weight :
         {weights.exact, weights.prefix, weights.token, weights.subsequence, weights.typo}) {
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
bool FuzzyMatcher::candidate(std::string_view text, const std::stop_token &stop) const {
    checkpoint(stop);
    const auto name = decode(text, {1024, 5});
    const auto parts = words(name);
    for (const auto &token : tokens_) {
        checkpoint(stop);
        if (subsequence({token, name})) {
            continue;
        }
        if (std::none_of(parts.begin(), parts.end(),
                         [&](auto word) { return possibleTypo(token, word); })) {
            return false;
        }
    }
    return true;
}
std::optional<std::int64_t> FuzzyMatcher::score(std::string_view text,
                                                const std::stop_token &stop) const {
    checkpoint(stop);
    const auto name = decode(text, {1024, 5});
    const auto parts = words(name);
    std::int64_t total = name == query_ ? weights_.exact : 0;
    if (name.starts_with(query_)) {
        total += weights_.prefix;
    }
    for (const auto &token : tokens_) {
        checkpoint(stop);
        std::optional<std::int64_t> best;
        const auto offer = [&](std::int64_t value) {
            best = best ? std::max(*best, value) : value;
        };
        if (name.find(token) != std::u32string::npos) {
            offer(weights_.token);
        }
        if (subsequence({token, name})) {
            offer(weights_.subsequence);
        }
        for (auto word : parts) {
            if (!possibleTypo(token, word)) {
                continue;
            }
            const auto edits = distance(token, word, stop);
            if (edits > 0 && edits <= tolerance(token.size())) {
                offer(static_cast<std::int64_t>(weights_.typo) - static_cast<std::int64_t>(edits));
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
