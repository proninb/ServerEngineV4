/*
 * PARSER-V2-PERF-01: isolated, opt-in Header V2 phase baseline.
 * This is a benchmark, not the production Parser entry point.
 * Physically prepare once per trial, then a single Header V2 semantic pass.
 * Semantic preprocessing and G writes run inside parser.parse() and cannot
 * honestly be separated with external wall clocks.
 */
#include "project/file/file_context.hpp"
#include "project/file/file_kind.hpp"
#include "project/frontend/prepared_include_v2.hpp"
#include "project/frontend/preprocessor_v2.hpp"
#include "project/parser/header_parser_v2.hpp"
#include "project/preprocessor_configuration.hpp"
#include "project/graph/graph.hpp"
#include "project/semantic/identity.hpp"
#include "project/string/string_table.hpp"

#include <chrono>
#include <array>
#include <initializer_list>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
using namespace cw::server;
using clock_type = std::chrono::steady_clock;

struct options final {
    std::string_view shape;
    std::size_t types = 0;
    std::size_t members = 0;
    std::size_t runs = 0;
    std::size_t warmup = 0;
};

[[nodiscard]] bool number(const char* text, std::size_t& value) noexcept {
    if (text == nullptr || *text == '\0' || *text == '-') return false;
    char* end = nullptr;
    const auto parsed = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' ||
        parsed > static_cast<unsigned long long>(
            (std::numeric_limits<std::size_t>::max)())) return false;
    value = static_cast<std::size_t>(parsed);
    return true;
}

[[nodiscard]] bool read_args(int argc, char** argv, options& value) noexcept {
    if (argc != 6) return false;
    value.shape = argv[1];
    if (value.shape != "fields" && value.shape != "ctor-after" &&
        value.shape != "ctor-before") return false;
    if (!number(argv[2], value.types) || !number(argv[3], value.members) ||
        !number(argv[4], value.runs) || !number(argv[5], value.warmup)) return false;
    return value.types > 0 && value.members > 0 && value.runs > 0 &&
        value.types <= 10000 && value.members <= 4096 &&
        value.runs <= 100 && value.warmup <= 100 &&
        value.types <= 2000000 / value.members;
}

[[nodiscard]] std::string generate(const options& config) {
    std::string output;
    for (std::size_t i = 0; i < config.types; ++i) {
        const auto type = "T" + std::to_string(i);
        output += "struct " + type + " {\n";
        const auto constructor = [&]() {
            output += type + "() {\n";
            for (std::size_t j = 0; j < config.members; ++j) {
                output += "m" + std::to_string(j) + " = 7;\n";
            }
            output += "}\n";
        };
        if (config.shape == "ctor-before") constructor();
        for (std::size_t j = 0; j < config.members; ++j) {
            output += "int m" + std::to_string(j) + " = 1;\n";
        }
        if (config.shape == "ctor-after") constructor();
        output += "};\n";
    }
    return output;
}

class source_file final {
public:
    explicit source_file(const std::string& content) {
        const auto nonce = clock_type::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("server_engine_v4_parser_perf_" + std::to_string(nonce) + ".hpp");
        std::ofstream out{path_, std::ios::binary | std::ios::trunc};
        if (!out) throw std::runtime_error{"Cannot create benchmark header"};
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) throw std::runtime_error{"Cannot write benchmark header"};
    }
    ~source_file() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    source_file(const source_file&) = delete;
    source_file& operator=(const source_file&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

[[nodiscard]] double milliseconds(clock_type::time_point begin,
                                  clock_type::time_point end) noexcept {
    return std::chrono::duration<double, std::milli>{end - begin}.count();
}

struct sample final {
    double physical_ms = 0;
    double preprocessor_start_ms = 0;
    double parse_ms = 0;
    std::size_t prepared_files = 0;
};

[[nodiscard]] sample run_one(const options& config,
                             const std::filesystem::path& source_path) {
    file_context files;
    preprocessor_configuration preprocessor_config;
    preprocessor_config.root_directory = source_path.parent_path();
    prepared_include_closure_v2 closure;
    prepared_include_failure_v2 physical_failure;
    string_table strings;
    identity_space ids{strings};
    graph G;
    preprocessor_v2_failure preprocessor_failure;
    parser_v2_failure parser_failure;
    file_id root;

    const auto t0 = clock_type::now();
    if (!succeeded(files.resolve(source_path, file_kind::header, root)) || !root)
        throw std::runtime_error{"file_context.resolve failed"};
    const std::array<file_id, 1> roots{root};
    if (!succeeded(prepare_header_include_closure_v2(
            files, roots, preprocessor_config, closure, &physical_failure)))
        throw std::runtime_error{"physical closure preparation failed"};
    const auto t1 = clock_type::now();

    semantic_preprocessor_v2 input{
        files, closure.view(), preprocessor_config, strings, &preprocessor_failure};
    const auto t2 = clock_type::now();
    if (!succeeded(input.start(root)))
        throw std::runtime_error{"semantic preprocessor.start failed"};
    const auto t3 = clock_type::now();

    header_parser_v2 parser{input, ids, G, &parser_failure};
    if (!succeeded(parser.parse()) || !input.finished()) {
        std::cerr << "Parser failure: " << parser_failure.detail
                  << " source_offset=" << parser_failure.source.offset << '\n';
        throw std::runtime_error{"Header Parser V2 failed"};
    }
    const auto t4 = clock_type::now();

    // Validate results OUTSIDE all timers, preventing dead-code benchmarks.
    for (std::size_t i : {std::size_t{0}, config.types - 1}) {
        const auto type_name = "T" + std::to_string(i);
        const auto id = strings.find(type_name);
        const auto identity = ids.find(ids.root(), id, identity_kind::type);
        const auto handle = G.find_type(identity);
        if (!handle || G.members(handle).size() != config.members)
            throw std::runtime_error{"Graph record/member validation failed"};
        const auto first = G.find_member(handle, strings.find("m0"));
        const auto* initial = G.construction(handle, first);
        const std::uint64_t expected = config.shape == "fields" ? 1 : 7;
        if (initial == nullptr || initial->bits() != expected)
            throw std::runtime_error{"Graph normalized initializer mismatch"};
    }

    return {milliseconds(t0, t1), milliseconds(t2, t3),
            milliseconds(t3, t4), closure.file_count()};
}
} // namespace

int main(int argc, char** argv) {
    try {
        options config;
        if (!read_args(argc, argv, config)) {
            std::cerr << "Usage: ServerEngineV4ParserV2Benchmark "
                      << "<fields|ctor-after|ctor-before> <types> <members> "
                      << "<runs> <warmup>\n";
            return 2;
        }
        const auto source = generate(config);
        source_file temporary{source};
        std::cout << "shape,run,types,members,source_bytes,prepared_files,"
                     "physical_prepare_ms,preprocessor_start_ms,"
                     "combined_semantic_parse_graph_ms,measured_sum_ms\n";
        std::cout << std::fixed << std::setprecision(4);
        for (std::size_t i = 0; i < config.warmup + config.runs; ++i) {
            const auto sample = run_one(config, temporary.path());
            if (i < config.warmup) continue;
            const auto total = sample.physical_ms +
                sample.preprocessor_start_ms + sample.parse_ms;
            std::cout << config.shape << ',' << (i - config.warmup + 1)
                      << ',' << config.types << ',' << config.members
                      << ',' << source.size() << ',' << sample.prepared_files
                      << ',' << sample.physical_ms
                      << ',' << sample.preprocessor_start_ms
                      << ',' << sample.parse_ms << ',' << total << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "PARSER-V2-PERF-01: " << error.what() << '\n';
        return 1;
    }
}
