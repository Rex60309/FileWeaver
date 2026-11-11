#pragma once
#include <cstddef>
#include <string>
#include <string_view>
namespace fileweaver {
struct TextPolicy {
    std::size_t limit;
    int error_code;
    bool fold_ascii = false;
};
[[nodiscard]] std::u32string decode_utf8(std::string_view text, TextPolicy policy);
} // namespace fileweaver
