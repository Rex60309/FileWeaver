#include "fileweaver/application/search.hpp"
#include "output.hpp"
#include "process_memory.hpp"
#include "windows_io.hpp"

#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
std::string padded(int index) {
    auto number = std::to_string(index);
    return std::string(6 - number.size(), '0') + number;
}

std::string text(std::u8string_view value) { return {value.begin(), value.end()}; }

enum class Dataset : std::uint8_t { flat_ascii, nested_unicode };

struct FixtureEntry {
    std::string name;
    std::filesystem::path relative_path;
};

FixtureEntry entry_of(int index, Dataset dataset) {
    if (dataset == Dataset::flat_ascii) {
        auto name = "doc_" + padded(index) + "_cat" + std::to_string(index % 10) + ".txt";
        return {name, std::filesystem::path(name)};
    }
    const auto group = index / 3;
    const auto name = text(u8"\u6A94\u6848_\u65E5\u672C\u8A9E_\u00E9\U0001F600_") + padded(group) +
                      text(u8"_\u985E") + std::to_string(group % 10) + ".txt";
    auto branch = std::filesystem::path(u8"\u8CC7\u6599 \u76EE\u9304");
    branch += std::to_string(index % 3);
    auto level = std::filesystem::path(u8"\u968E\u5C64_");
    level += std::to_string(group % 8);
    return {name, branch / level / std::filesystem::path(std::u8string(name.begin(), name.end()))};
}

struct Case {
    fileweaver::SearchMode mode;
    std::string query;
    std::vector<std::pair<std::string, std::string>> expected;
};

struct Sample {
    std::string phase;
    int iteration;
    std::size_t case_index;
    std::int64_t elapsed_ns;
    std::size_t returned_count;
};

struct Configuration {
    std::filesystem::path database;
    std::filesystem::path root;
    int files = 0;
    int repetitions = 5;
    int warmups = 1;
    std::string profile = "flat-ascii";
};

std::string measure(const Configuration &configuration) {
    const auto memory_before = fileweaver::benchmark::peak_memory();
    const auto &[database, root, files, repetitions, warmups, profile] = configuration;
    const bool nested = profile == "nested-unicode";
    const auto dataset = nested ? Dataset::nested_unicode : Dataset::flat_ascii;
    using Mode = fileweaver::SearchMode;
    std::vector<Case> cases{
        {Mode::exact, entry_of(0, dataset).name, {}},
        {Mode::exact, entry_of(files - 1, dataset).name, {}},
        {Mode::exact, nested ? text(u8"\u4E0D\u5B58\u5728.txt") : "not-present.txt", {}},
        {Mode::substring, nested ? text(u8"\u65E5\u672C\u8A9E") : "doc_000", {}},
        {Mode::substring, nested ? text(u8"_\u985E3") : "_cat3", {}},
        {Mode::substring, nested ? text(u8"\u4E0D\u5B58\u5728") : "not-present", {}}};
    // Oracle is outside timing; generated (name, relative path) tuples sort by index.
    for (auto &test : cases) {
        for (int index = 0; index < files && test.expected.size() < 100; ++index) {
            const auto entry = entry_of(index, dataset);
            const auto &name = entry.name;
            if ((test.mode == Mode::exact && name == test.query) ||
                (test.mode == Mode::substring && name.find(test.query) != std::string::npos)) {
                const auto path = (root / entry.relative_path).generic_u8string();
                test.expected.emplace_back(name, std::string(path.begin(), path.end()));
            }
        }
    }
    std::vector<Sample> samples;
    const std::vector<std::pair<std::string, int>> phases{{"first_observed_in_native_process", 1},
                                                          {"warmup", warmups},
                                                          {"warm_measured", repetitions}};
    for (const auto &[phase, rounds] : phases) {
        for (int iteration = 0; iteration < rounds; ++iteration) {
            for (std::size_t index = 0; index < cases.size(); ++index) {
                const auto &test = cases[index];
                fileweaver::SearchRequest request{database, test.query, test.mode, 100};
                const auto start = std::chrono::steady_clock::now();
                const auto results = fileweaver::search_files(request);
                const auto end = std::chrono::steady_clock::now();
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                if (results.size() != test.expected.size()) {
                    throw std::runtime_error(
                        "Core search result count differs from fixture oracle");
                }
                for (std::size_t row = 0; row < results.size(); ++row) {
                    if (results[row].name != test.expected[row].first ||
                        results[row].display_path != test.expected[row].second ||
                        results[row].size_bytes != 0) {
                        throw std::runtime_error("Core search results differ from fixture oracle");
                    }
                }
                samples.push_back({phase, iteration, index, elapsed, results.size()});
            }
        }
    }
    const auto memory_after = fileweaver::benchmark::peak_memory();
    std::ostringstream output;
    output << "{\"method\":\"cpp_application_search\",\"dataset_profile\":"
           << fileweaver::cli::json_string(profile)
           << ",\"sqlite_version\":" << fileweaver::cli::json_string(sqlite3_libversion())
           << ",\"memory\":{\"unit\":\"bytes\",\"scope\":\"process_lifetime_through_workload_"
              "before_report_serialization\",\"metric\":"
           << fileweaver::cli::json_string(memory_after.metric)
           << ",\"source\":" << fileweaver::cli::json_string(memory_after.source)
           << ",\"before_status\":" << fileweaver::cli::json_string(memory_before.status)
           << ",\"after_status\":" << fileweaver::cli::json_string(memory_after.status)
           << ",\"before_workload_peak_bytes\":";
    const auto write_memory = [&](const std::optional<std::uint64_t> &bytes) {
        if (bytes) {
            output << *bytes;
        } else {
            output << "null";
        }
    };
    write_memory(memory_before.bytes);
    output << ",\"after_workload_peak_bytes\":";
    write_memory(memory_after.bytes);
    output << "},\"raw\":[";
    bool first = true;
    for (const auto &sample : samples) {
        const auto &test = cases[sample.case_index];
        if (!first) {
            output << ',';
        }
        first = false;
        output << "{\"phase\":" << fileweaver::cli::json_string(sample.phase)
               << ",\"iteration\":" << sample.iteration
               << ",\"method\":\"cpp_application_search\",\"mode\":\""
               << (test.mode == Mode::exact ? "exact" : "substring")
               << "\",\"query\":" << fileweaver::cli::json_string(test.query)
               << ",\"elapsed_ns\":" << sample.elapsed_ns
               << ",\"returned_count\":" << sample.returned_count << ",\"correct\":true}";
    }
    output << "]}";
    return output.str();
}
} // namespace

int main(int argc, char **argv) {
    try {
        fileweaver::cli::Arguments arguments(argc, argv);
        CLI::App app{"Developer-only synthetic fixture core search timing"};
        std::string database;
        std::string root;
        Configuration configuration;
        app.add_option("--database", database)->required();
        app.add_option("--root", root)->required();
        app.add_option("--files", configuration.files)->required()->check(CLI::Range(1, 100000));
        app.add_option("--repetitions", configuration.repetitions)->check(CLI::Range(2, 200));
        app.add_option("--warmups", configuration.warmups)->check(CLI::Range(1, 10));
        app.add_option("--dataset", configuration.profile)
            ->check(CLI::IsMember({"flat-ascii", "nested-unicode"}));
        try {
            app.parse(arguments.count(), arguments.values());
        } catch (const CLI::ParseError &error) {
            return app.exit(error);
        }
        configuration.database =
            std::filesystem::path(std::u8string(database.begin(), database.end()));
        configuration.root = std::filesystem::canonical(
            std::filesystem::path(std::u8string(root.begin(), root.end())));
        const auto document = measure(configuration);
        std::cout << document << '\n';
        return std::cout ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "Core benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
