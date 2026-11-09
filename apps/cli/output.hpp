#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "fileweaver/application/index.hpp"
#include "fileweaver/application/inspect.hpp"
#include "fileweaver/application/refresh.hpp"
#include "fileweaver/application/search.hpp"

namespace fileweaver::cli {
inline std::string quote_path(std::string_view path) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (const unsigned char byte : path) {
        if (byte < 32 || byte == 127) {
            output += "\\u00";
            output += hex[byte / 16];
            output += hex[byte % 16];
        } else {
            if (byte == '"' || byte == '\\') {
                output += '\\';
            }
            output += static_cast<char>(byte);
        }
    }
    output += '"';
    return output;
}

inline std::string json_string(std::string_view text) {
    for (std::size_t offset = 0; offset < text.size();) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        if (lead < 128) {
            ++offset;
            continue;
        }
        const unsigned width = lead >= 194 && lead <= 223   ? 2U
                               : lead >= 224 && lead <= 239 ? 3U
                               : lead >= 240 && lead <= 244 ? 4U
                                                            : 0U;
        if (width == 0 || text.size() - offset < width) {
            throw std::runtime_error("JSON output requires valid UTF-8 text");
        }
        unsigned codepoint = lead % (width == 2 ? 32U : width == 3 ? 16U : 8U);
        for (unsigned index = 1; index < width; ++index) {
            const auto next = static_cast<unsigned char>(text[offset + index]);
            if (next < 128 || next > 191) {
                throw std::runtime_error("JSON output requires valid UTF-8 text");
            }
            codepoint = codepoint * 64U + next % 64U;
        }
        const unsigned minimum = width == 2 ? 128U : width == 3 ? 2048U : 65536U;
        if (codepoint < minimum || codepoint > 1114111U ||
            (codepoint >= 55296U && codepoint <= 57343U)) {
            throw std::runtime_error("JSON output requires valid UTF-8 text");
        }
        offset += width;
    }
    return quote_path(text);
}

inline std::string json_error(int code) {
    const char *category = code == 2   ? "Invalid arguments"
                           : code == 3 ? "Workspace or permission error"
                           : code == 4 ? "Index not initialized"
                           : code == 5 ? "Schema or metadata error"
                                       : "Runtime failure";
    return "{\"ok\":false,\"exit_code\":" + std::to_string(code) +
           ",\"error\":" + json_string(category) + "}";
}

inline std::string json_help(std::string_view text) {
    return "{\"ok\":true,\"exit_code\":0,\"command\":\"help\",\"text\":" + json_string(text) + "}";
}

inline std::string json_search(const std::vector<SearchResult> &results) {
    std::string output = "{\"ok\":true,\"exit_code\":0,\"command\":\"search\",\"results\":[";
    bool first = true;
    for (const auto &result : results) {
        if (!first) {
            output += ',';
        }
        first = false;
        output += "{\"file_id\":" + std::to_string(result.file_id) +
                  ",\"root_id\":" + std::to_string(result.root_id) +
                  ",\"path\":" + json_string(result.display_path) +
                  ",\"name\":" + json_string(result.name) +
                  ",\"size_bytes\":" + std::to_string(result.size_bytes) + "}";
    }
    return output + "],\"count\":" + std::to_string(results.size()) + "}";
}

inline std::string json_index(const IndexSummary &result) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    std::string output =
        std::string("{\"ok\":") + boolean(result.scan.complete) +
        ",\"exit_code\":" + (result.scan.complete ? "0" : "1") +
        ",\"command\":\"index\",\"scan\":{\"files\":" + std::to_string(result.scan.files) +
        ",\"skipped\":" + std::to_string(result.scan.skipped) +
        ",\"errors\":" + std::to_string(result.scan.errors) +
        ",\"complete\":" + boolean(result.scan.complete) +
        ",\"cancelled\":" + boolean(result.scan.cancelled) +
        "},\"added\":" + std::to_string(result.added) +
        ",\"updated\":" + std::to_string(result.updated) +
        ",\"missing\":" + std::to_string(result.missing) +
        ",\"missing_reconciled\":" + boolean(result.missing_reconciled);
    if (!result.scan.complete) {
        output += ",\"error\":\"Scan incomplete\"";
    }
    return output + '}';
}
inline std::string json_refresh(const RefreshSummary &result) {
    std::string output =
        std::string("{\"ok\":") + (result.complete ? "true" : "false") +
        ",\"exit_code\":" + (result.complete ? "0" : "1") +
        ",\"command\":\"refresh\",\"cancelled\":" + (result.cancelled ? "true" : "false") +
        ",\"roots\":[";
    bool first = true;
    for (const auto &root : result.roots) {
        if (!first) {
            output += ',';
        }
        first = false;
        const auto bytes = root.root.generic_u8string();
        output += "{\"root_id\":" + std::to_string(root.root_id) +
                  ",\"path\":" + json_string(std::string(bytes.begin(), bytes.end())) +
                  ",\"attempted\":" + (root.attempted ? "true" : "false") +
                  ",\"exit_code\":" + std::to_string(root.exit_code);
        if (!root.error.empty()) {
            output += ",\"error\":" + json_string(root.error);
        }
        if (root.index) {
            output += ",\"index\":" + json_index(*root.index);
        }
        output += '}';
    }
    output += ']';
    if (!result.complete) {
        output += ",\"error\":\"Refresh incomplete\"";
    }
    return output + '}';
}
inline std::string json_roots(const std::vector<RootInfo> &roots) {
    std::string output = "{\"ok\":true,\"exit_code\":0,\"command\":\"roots\",\"roots\":[";
    bool first = true;
    for (const auto &root : roots) {
        if (!first) {
            output += ',';
        }
        first = false;
        output +=
            "{\"root_id\":" + std::to_string(root.root_id) + ",\"path\":" + json_string(root.path) +
            ",\"last_scan_status\":" + json_string(root.last_scan_status) + ",\"last_scan_at\":" +
            (root.last_scan_at ? std::to_string(*root.last_scan_at) : "null") +
            ",\"policy_known\":" + (root.policy_known ? "true" : "false") + ",\"maximum_depth\":" +
            (root.maximum_depth ? std::to_string(*root.maximum_depth) : "null") +
            ",\"exclusions\":[";
        bool first_path = true;
        for (const auto &path : root.exclusions) {
            if (!first_path) {
                output += ',';
            }
            first_path = false;
            output += json_string(path);
        }
        output += "]}";
    }
    return output + "],\"count\":" + std::to_string(roots.size()) + "}";
}

inline std::string json_status(const CatalogStatus &status) {
    return "{\"ok\":true,\"exit_code\":0,\"command\":\"status\",\"schema_version\":" +
           std::to_string(status.schema_version) + ",\"roots\":" + std::to_string(status.roots) +
           ",\"present_files\":" + std::to_string(status.present_files) +
           ",\"missing_files\":" + std::to_string(status.missing_files) +
           ",\"scan_records\":" + std::to_string(status.scan_records) +
           ",\"running_scan_records\":" + std::to_string(status.running_scan_records) + "}";
}
} // namespace fileweaver::cli
