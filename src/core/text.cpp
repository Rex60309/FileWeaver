#include "fileweaver/core/text.hpp"
#include "fileweaver/core/storage_error.hpp"
#include <cstdint>
namespace fileweaver {
std::u32string decode_utf8(std::string_view text, TextPolicy policy) {
    const auto [limit, error_code, fold_ascii] = policy;
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
            throw StorageError("Search text requires valid UTF-8", error_code);
        }
        if (remaining > text.size() - i) {
            throw StorageError("Search text requires valid UTF-8", error_code);
        }
        while (remaining > 0) {
            --remaining;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0U) != 0x80U) {
                throw StorageError("Search text requires valid UTF-8", error_code);
            }
            value = (value << 6U) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            value == 0) {
            throw StorageError("Search text requires valid UTF-8 without NUL", error_code);
        }
        if (result.size() == limit) {
            throw StorageError("Search text exceeds Unicode length limit", error_code);
        }
        if (fold_ascii && value >= 'A' && value <= 'Z') {
            value += 'a' - 'A';
        }
        result.push_back(static_cast<char32_t>(value));
    }
    return result;
}
} // namespace fileweaver
