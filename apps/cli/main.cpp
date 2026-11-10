#include "fileweaver/application/index.hpp"
#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/inspect.hpp"
#include "fileweaver/application/refresh.hpp"
#include "fileweaver/application/search.hpp"
#include "fileweaver/core/storage_error.hpp"
#include "fileweaver/core/utc_time.hpp"
#include "output.hpp"
#include "windows_io.hpp"

#include <CLI/CLI.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
bool wants_json(int argc, char *argv[]) noexcept {
    bool json_requested = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--") {
            break;
        }
        if (argument == "--json" || argument.starts_with("--json=")) {
            json_requested = true;
        }
    }
    return json_requested;
}

int run_cli(int argc, char *argv[], bool json_requested) {
    const auto fail = [&](int code, const std::string &message) {
        std::cerr << message << '\n';
        if (json_requested) {
            std::cout << fileweaver::cli::json_error(code) << '\n';
        }
        return code;
    };
    if (argc > 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        for (int index = 2; index < argc; ++index) {
            if (std::string_view(argv[index]) != "--json") {
                return fail(2, "Invalid arguments: help must be used alone or with --json");
            }
        }
    }
    CLI::App app{"FileWeaver - local storage foundation", "fileweaver"};
    bool json = false;
    app.add_flag("--json", json, "Output exactly one JSON document")->disable_flag_override();
    auto *init = app.add_subcommand("init", "Initialize an explicitly selected database");
    init->fallthrough();
    std::string database;
    init->add_option("--database", database, "Database path (parent must exist)")->required();
    auto *index = app.add_subcommand("index", "Index metadata from an explicitly selected root");
    index->fallthrough();
    std::string root;
    std::vector<std::string> exclusions;
    index->add_option("root", root, "Root directory (no links followed)")->required();
    index->add_option("--database", database, "Initialized database path")->required();
    index->add_option("--exclude", exclusions, "Excluded path; repeat for multiple paths");
    auto *refresh =
        app.add_subcommand("refresh", "Refresh registered roots using saved scan policies");
    refresh->fallthrough();
    refresh->add_option("--database", database, "Initialized v3 database path")->required();
    auto *roots = app.add_subcommand(
        "roots", "List registered roots and saved scan policies without scanning");
    roots->fallthrough();
    roots->add_option("--database", database, "Existing v2/v3 database path")->required();
    auto *status =
        app.add_subcommand("status", "Inspect index counts without scanning source files");
    status->fallthrough();
    status->add_option("--database", database, "Existing v2/v3 database path")->required();
    auto *search = app.add_subcommand("search", "Search present indexed filenames");
    search->fallthrough();
    std::string query;
    std::string mode = "substring";
    std::size_t limit = 100;
    std::string sort = "name";
    bool descending = false;
    search->add_option("query", query, "Literal filename query")->required();
    search->add_option("--database", database, "Initialized database path")->required();
    search->add_option("--mode", mode, "exact, substring, prefix or fuzzy")
        ->check(CLI::IsMember({"exact", "substring", "prefix", "fuzzy"}));
    search->add_option("--limit", limit, "Maximum results (1..10000)")->check(CLI::Range(1, 10000));
    const auto *sort_option =
        search->add_option("--sort", sort, "name, size, modified; fuzzy defaults to relevance")
            ->check(CLI::IsMember({"name", "size", "modified", "relevance"}));
    search->add_flag("--descending", descending, "Descending primary sort; ties stay ascending");
    fileweaver::FuzzyOptions fuzzy;
    search
        ->add_option("--fuzzy-candidates", fuzzy.candidate_limit,
                     "Fuzzy candidate budget (1..10000; default 4096)")
        ->check(CLI::Range(1, 10000));
    search
        ->add_option("--fuzzy-exact-weight", fuzzy.weights.exact,
                     "Fuzzy full-name bonus (default 1000)")
        ->check(CLI::Range(0, 10000));
    search
        ->add_option("--fuzzy-prefix-weight", fuzzy.weights.prefix,
                     "Fuzzy filename-prefix bonus (default 500)")
        ->check(CLI::Range(0, 10000));
    search
        ->add_option("--fuzzy-token-weight", fuzzy.weights.token,
                     "Fuzzy token substring score (default 300)")
        ->check(CLI::Range(0, 10000));
    search
        ->add_option("--fuzzy-subsequence-weight", fuzzy.weights.subsequence,
                     "Fuzzy subsequence score (default 100)")
        ->check(CLI::Range(0, 10000));
    search
        ->add_option("--fuzzy-typo-weight", fuzzy.weights.typo,
                     "Fuzzy typo score before edit penalty (default 50)")
        ->check(CLI::Range(0, 10000));
    fileweaver::SearchFilters filters;
    std::string selected_extension;
    std::string selected_root;
    std::string modified_after;
    std::string modified_before;
    const auto *extension_option = search->add_option(
        "--ext", selected_extension,
        "Last extension (ASCII case-insensitive; explicit empty value = no extension)");
    search->add_option("--min-size", filters.minimum_size, "Inclusive minimum logical bytes")
        ->check(CLI::NonNegativeNumber);
    search->add_option("--max-size", filters.maximum_size, "Inclusive maximum logical bytes")
        ->check(CLI::NonNegativeNumber);
    const auto *root_option = search->add_option(
        "--root", selected_root, "Exact indexed root path, relative to current directory");
    const auto *after_option = search->add_option("--modified-after", modified_after,
                                                  "Strictly after UTC YYYY-MM-DDTHH:MM:SSZ");
    const auto *before_option = search->add_option("--modified-before", modified_before,
                                                   "Strictly before UTC YYYY-MM-DDTHH:MM:SSZ");
    app.require_subcommand(0, 1);
    const auto help = [&] {
        const std::string text = "Usage: fileweaver [OPTIONS] [SUBCOMMANDS]\n" + app.help();
        std::cout << (json_requested ? fileweaver::cli::json_help(text) : text);
        if (json_requested) {
            std::cout << '\n';
        }
    };
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &) {
        help();
        return 0;
    } catch (const CLI::ParseError &error) {
        return fail(2, std::string("Invalid arguments: ") + error.what());
    } catch (const std::exception &error) {
        return fail(1, std::string("Operation failed: ") + error.what());
    }
    if (!*init && !*index && !*search && !*refresh && !*roots && !*status) {
        help();
        return 0;
    }
    try {
        const std::u8string utf8(database.begin(), database.end());
        if (*roots) {
            const auto result = fileweaver::list_roots(std::filesystem::path(utf8));
            if (json_requested) {
                const auto document = fileweaver::cli::json_roots(result);
                std::cout << document << '\n';
            } else {
                for (const auto &entry : result) {
                    std::cout << "Root " << entry.root_id << ' '
                              << fileweaver::cli::quote_path(entry.path)
                              << " last_scan_status=" << entry.last_scan_status
                              << " policy_known=" << (entry.policy_known ? "yes" : "no")
                              << " last_scan_at="
                              << (entry.last_scan_at ? std::to_string(*entry.last_scan_at)
                                                     : "unknown")
                              << " maximum_depth="
                              << (entry.maximum_depth ? std::to_string(*entry.maximum_depth)
                                                      : "unknown")
                              << '\n';
                    for (const auto &path : entry.exclusions) {
                        std::cout << "  exclude " << fileweaver::cli::quote_path(path) << '\n';
                    }
                }
                std::cout << "Registered roots=" << result.size() << '\n';
            }
            return 0;
        }
        if (*status) {
            const auto result = fileweaver::catalog_status(std::filesystem::path(utf8));
            if (json_requested) {
                std::cout << fileweaver::cli::json_status(result) << '\n';
            } else {
                std::cout << "Schema=" << result.schema_version << " roots=" << result.roots
                          << " present_files=" << result.present_files
                          << " missing_files=" << result.missing_files
                          << " scan_records=" << result.scan_records
                          << " running_scan_records=" << result.running_scan_records << '\n';
            }
            return 0;
        }
        const auto clock = [] {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        };
        if (*refresh) {
            const auto result = fileweaver::refresh_index(std::filesystem::path(utf8), clock);
            if (json_requested) {
                const auto document = fileweaver::cli::json_refresh(result);
                std::cout << document << '\n';
            }
            for (const auto &entry : result.roots) {
                const auto bytes = entry.root.generic_u8string();
                const auto path =
                    fileweaver::cli::quote_path(std::string(bytes.begin(), bytes.end()));
                if (!json_requested) {
                    std::cout << "Root " << path << " exit_code=" << entry.exit_code
                              << " attempted=" << (entry.attempted ? "yes" : "no");
                    if (entry.index) {
                        std::cout << " files=" << entry.index->scan.files
                                  << " added=" << entry.index->added
                                  << " updated=" << entry.index->updated
                                  << " missing=" << entry.index->missing;
                    }
                    std::cout << '\n';
                }
                if (!entry.error.empty()) {
                    std::cerr << path << ": " << entry.error << '\n';
                }
                if (entry.index && entry.index->scan.complete && !entry.index->missing_reconciled) {
                    std::cerr << path
                              << ": Missing-file reconciliation deferred because exclusions or "
                                 "skipped entries are present.\n";
                }
            }
            if (!json_requested) {
                std::cout << "Refreshed roots=" << result.roots.size()
                          << " status=" << (result.complete ? "completed" : "incomplete") << '\n';
            }
            return result.complete ? 0 : 1;
        }
        if (*search) {
            fileweaver::SearchRequest request{std::filesystem::path(utf8), query,
                                              mode == "fuzzy"   ? fileweaver::SearchMode::fuzzy
                                              : mode == "exact" ? fileweaver::SearchMode::exact
                                              : mode == "prefix"
                                                  ? fileweaver::SearchMode::prefix
                                                  : fileweaver::SearchMode::substring,
                                              limit};
            request.filters = filters;
            if (*extension_option) {
                request.filters.extension = selected_extension;
            }
            request.fuzzy = fuzzy;
            request.sort = (sort == "relevance" || (mode == "fuzzy" && !*sort_option))
                               ? fileweaver::SearchSort::relevance
                           : sort == "size"     ? fileweaver::SearchSort::size
                           : sort == "modified" ? fileweaver::SearchSort::modified
                                                : fileweaver::SearchSort::name;
            request.descending = descending;
            if (*after_option) {
                request.filters.modified_after = fileweaver::parse_utc_time(modified_after);
            }
            if (*before_option) {
                request.filters.modified_before = fileweaver::parse_utc_time(modified_before);
            }
            if (*root_option) {
                request.filters.root = std::filesystem::path(
                    std::u8string(selected_root.begin(), selected_root.end()));
            }
            const auto results = fileweaver::search_files(request);
            if (json_requested) {
                const auto document = fileweaver::cli::json_search(results);
                std::cout << document << '\n';
                return 0;
            }
            for (const auto &result : results) {
                std::cout << result.size_bytes << '\t'
                          << fileweaver::cli::quote_path(result.display_path) << '\n';
            }
            return 0;
        }
        if (*index) {
            fileweaver::IndexRequest request;
            request.database = std::filesystem::path(utf8);
            request.root = std::filesystem::path(std::u8string(root.begin(), root.end()));
            for (const auto &excluded : exclusions) {
                request.scan.excluded_paths.emplace_back(
                    std::u8string(excluded.begin(), excluded.end()));
            }
            const auto result = fileweaver::index_directory(request, clock);
            if (json_requested) {
                std::cout << fileweaver::cli::json_index(result) << '\n';
            } else {
                std::cout << "Indexed files=" << result.scan.files << " added=" << result.added
                          << " updated=" << result.updated << " missing=" << result.missing
                          << " skipped=" << result.scan.skipped << " errors=" << result.scan.errors
                          << " status=" << (result.scan.complete ? "completed" : "incomplete")
                          << '\n';
            }
            if (result.scan.complete && !result.missing_reconciled) {
                std::cerr << "Missing-file reconciliation deferred because exclusions or skipped "
                             "entries are present.\n";
            }
            if (!result.scan.complete) {
                std::cerr << "Scan incomplete; prior file metadata was retained.\n";
                return 1;
            }
            return 0;
        }
        fileweaver::initialize_database(std::filesystem::path(utf8));
        std::cout << (json_requested ? "{\"ok\":true,\"exit_code\":0,\"command\":\"init\"}\n"
                                     : "Database initialized.\n");
        return 0;
    } catch (const fileweaver::StorageError &error) {
        return fail(error.exit_code(), error.what());
    } catch (const std::exception &error) {
        return fail(1, std::string("Operation failed: ") + error.what());
    }
}
} // namespace

int execute(int argc, char *argv[]) {
    const bool json = wants_json(argc, argv);
    try {
        return run_cli(argc, argv, json);
    } catch (const std::exception &error) {
        std::cerr << "Operation failed: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Unexpected operation failure\n";
    }
    if (json) {
        std::cout << "{\"ok\":false,\"exit_code\":1,\"error\":\"Runtime failure\"}\n";
    }
    return 1;
}

int main(int argc, char *argv[]) {
    fileweaver::cli::ConsoleOutput output(std::cout, false);
    fileweaver::cli::ConsoleOutput diagnostics(std::cerr, true);
    int result = 1;
    try {
        fileweaver::cli::Arguments arguments(argc, argv);
        result = execute(arguments.count(), arguments.values());
    } catch (const std::exception &error) {
        std::cerr << "Cannot initialize command line: " << error.what() << '\n';
    }
    const bool output_ok = output.finish();
    const bool diagnostics_ok = diagnostics.finish();
    return output_ok && diagnostics_ok ? result : 1;
}
