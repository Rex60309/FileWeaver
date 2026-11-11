#include "fileweaver/core/glob.hpp"
#include "fileweaver/core/storage_error.hpp"
#include "fileweaver/core/text.hpp"
#include <algorithm>
#include <limits>
namespace fileweaver {
GlobMatcher::GlobMatcher(std::string_view query) {
    const auto pattern = decode_utf8(query, {256, 2});
    if (pattern.empty()) {
        throw StorageError("Glob pattern must not be empty", 2);
    }
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        Token token;
        token.value = pattern[i];
        if (pattern[i] == U'*') {
            if (!tokens_.empty() && tokens_.back().kind == Kind::star) {
                continue;
            }
            token.kind = Kind::star;
        } else if (pattern[i] == U'?') {
            token.kind = Kind::any;
        } else if (pattern[i] == U'[') {
            token.kind = Kind::group;
            ++i;
            if (i < pattern.size() && pattern[i] == U'!') {
                token.negative = true;
                ++i;
            }
            bool first = true;
            while (i < pattern.size() && (pattern[i] != U']' || first)) {
                const auto begin = pattern[i++];
                auto end = begin;
                if (i + 1 < pattern.size() && pattern[i] == U'-' && pattern[i + 1] != U']') {
                    ++i;
                    end = pattern[i++];
                }
                if (end < begin) {
                    throw StorageError("Glob range must be ascending", 2);
                }
                token.ranges.emplace_back(begin, end);
                first = false;
            }
            if (i == pattern.size() || token.ranges.empty()) {
                throw StorageError("Unclosed or empty glob character group", 2);
            }
        }
        tokens_.push_back(std::move(token));
    }
}
bool GlobMatcher::matches(std::string_view text, const std::stop_token &stop) const {
    const auto name = decode_utf8(text, {1024, 5});
    std::size_t n = 0, p = 0, mark = 0;
    auto star = std::numeric_limits<std::size_t>::max();
    while (n < name.size()) {
        if (stop.stop_requested()) {
            throw StorageError("Search cancelled", 1);
        }
        if (p < tokens_.size() && tokens_[p].kind == Kind::star) {
            star = p++;
            mark = n;
            continue;
        }
        bool match = false;
        if (p < tokens_.size()) {
            const auto &token = tokens_[p];
            if (token.kind == Kind::any) {
                match = true;
            } else if (token.kind == Kind::literal) {
                match = token.value == name[n];
            } else if (token.kind == Kind::group) {
                const bool found =
                    std::any_of(token.ranges.begin(), token.ranges.end(), [&](const auto &range) {
                        return name[n] >= range.first && name[n] <= range.second;
                    });
                match = found != token.negative;
            }
        }
        if (match) {
            ++n;
            ++p;
        } else if (star != std::numeric_limits<std::size_t>::max()) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < tokens_.size() && tokens_[p].kind == Kind::star) {
        ++p;
    }
    if (stop.stop_requested()) {
        throw StorageError("Search cancelled", 1);
    }
    return p == tokens_.size();
}
} // namespace fileweaver
