/*
 * PARSER-V2-PERF-02: real project.json Header-only benchmark.
 * This executable never PUBLISHes, modifies Project artifacts or executes SHM.
 * Composition uses the production manifest loader; source/assign roots are
 * intentionally excluded until independent Source Parser V2 is implemented.
 */
#include "project/project_configuration_manifest.hpp"
#include "project/file/file_context.hpp"
#include "project/file/file_kind.hpp"
#include "project/frontend/source_discovery.hpp"
#include "project/frontend/prepared_include_v2.hpp"
#include "project/frontend/preprocessor_v2.hpp"
#include "project/frontend/lexical_generation.hpp"
#include "project/parser/header_parser_v2.hpp"
#include "project/parser/parser.hpp"
#include "project/preprocessor_configuration.hpp"
#include "project/graph/graph.hpp"
#include "project/semantic/identity.hpp"
#include "project/string/string_table.hpp"
#include "project/source/source_map.hpp"
#include "diagnostics/diagnostic_collection.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#endif

namespace {
using namespace cw::server;
using clock_type = std::chrono::steady_clock;

[[nodiscard]] double ms(clock_type::time_point a,
                        clock_type::time_point b) noexcept {
    return std::chrono::duration<double, std::milli>{b - a}.count();
}

[[nodiscard]] std::size_t peak_working_set() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<std::size_t>(counters.PeakWorkingSetSize);
#endif
    return 0;
}

[[nodiscard]] std::size_t file_bytes(const file_context& files,
                                      file_id id) noexcept {
    // Reading every file just to obtain a byte count would distort the profile;
    // file contents already reside in File Context after physical preparation.
    return files.content_available(id) ? files.content(id).size() : 0;
}

struct sample final {
    // PARSER-V2-PERF-03: populated only by the explicit 'profile' mode.
    prepared_include_profile_v2 physical_profile;
    header_parser_v2_profile semantic_profile;
    semantic_preprocessor_v2_perf07_profile cursor_profile;
    double manifest_ms = 0;
    double physical_ms = 0;
    double preprocessor_start_ms = 0;
    double semantic_ms = 0;
    std::size_t project_files = 0;
    std::size_t header_roots = 0;
    std::size_t source_roots = 0;
    std::size_t assign_roots = 0;
    std::size_t prepared_files = 0;
    std::size_t header_bytes = 0;
    std::size_t types = 0;
    std::size_t members = 0;
    std::size_t peak_bytes = 0;
};

// One semantic world per trial, lifetime of all buffers spans all parses.
struct trial final {
    file_context files;
    project_configuration_manifest manifest;
    preprocessor_configuration config;
    diagnostic_collection diagnostics;
    std::vector<file_id> headers;
    std::vector<file_id> sources;
    std::vector<file_id> assigns;
    string_table strings;
    identity_space ids{strings};
    graph G;
    source_map source_records;
    sample metrics;
};

[[nodiscard]] std::filesystem::path file_path(const file_context& files,
                                               file_id id) {
    // file_context::path() asserts on invalid file_id, including diagnostic
    // defaults that may be unset when an error happens before a file is known.
    if (!id || !files.contains(id)) return {};
    const auto name = files.path(id);
    return std::filesystem::path{
        std::basic_string<file_path_char>{name.begin(), name.end()}};
}

void compose(trial& world, const std::filesystem::path& project) {
    const auto start = clock_type::now();
    const auto status = compose_project_configuration(
        project, operation_id{1}, world.diagnostics, world.manifest,
        world.files, world.config);
    if (!succeeded(status))
        throw std::runtime_error{"Project composition failed; check project.json/manifest"};
    for (std::uint32_t n = 1; n <= world.files.size(); ++n) {
        const file_id id{n};
        switch (world.files.kind(id)) {
        case file_kind::header: world.headers.push_back(id); break;
        case file_kind::source: world.sources.push_back(id); break;
        case file_kind::assign: world.assigns.push_back(id); break;
        default: break;
        }
    }
    world.metrics.manifest_ms = ms(start, clock_type::now());
    world.metrics.project_files = world.manifest.files.size();
    world.metrics.header_roots = world.headers.size();
    world.metrics.source_roots = world.sources.size();
    world.metrics.assign_roots = world.assigns.size();
    if (world.headers.empty())
        throw std::runtime_error{"Project manifest has no Header roots"};
}

// Same normalization contract as tests/frontend_regression_tests.cpp's
// differential_projection: semantic WHO and spelled members, never WHERE.
[[nodiscard]] std::string identity_name(const identity_space& ids,
                                        const string_table& strings,
                                        identity_ref id, unsigned depth = 0) {
    if (!id) return "<invalid-identity>";
    if (id == ids.root()) return "::";
    if (depth >= 256) return "<identity-depth-limit>";
    identity_record value;
    if (!ids.record(id, value)) return "<unknown-identity>";
    return identity_name(ids, strings, value.parent, depth + 1) +
        std::string{strings.get(value.name)} +
        (id.kind() == identity_kind::namespace_scope ? "::" : "");
}

[[nodiscard]] std::string type_name(const graph& G, const identity_space& ids,
                                    const string_table& strings, type_ref type,
                                    unsigned depth = 0) {
    if (depth > 128) return "<type-depth-limit>";
    intrinsic_type intrinsic;
    if (G.intrinsic(type, intrinsic))
        return "intrinsic(" + std::to_string(static_cast<unsigned>(intrinsic)) + ")";
    if (type.kind() == type_ref_kind::named)
        return "named(" + identity_name(ids, strings, ids.at_slot(type.payload())) + ")";
    derived_type_record value;
    if (G.derived(type, value))
        return "derived(" + std::to_string(static_cast<unsigned>(value.kind)) +
            "," + std::to_string(value.payload) + "," +
            type_name(G, ids, strings, value.child, depth + 1) + ")";
    return "<invalid-type>";
}

[[nodiscard]] std::vector<std::string> projection(const trial& t) {
    std::vector<std::string> rows;
    for (std::size_t slot = 2; slot <= t.ids.size(); ++slot) {
        const auto id = t.ids.at_slot(static_cast<std::uint32_t>(slot));
        if (!id || id.kind() != identity_kind::type) continue;
        const auto name = identity_name(t.ids, t.strings, id);
        rows.push_back("who:" + name);
        const auto handle = t.G.find_type(id);
        const auto* entry = t.G.find(handle);
        if (entry == nullptr || !entry->defined()) continue;
        rows.push_back("record:" + name + ":" +
            std::to_string(static_cast<unsigned>(entry->kind)) + ":" +
            std::to_string(static_cast<unsigned>(entry->record_kind)));
        rows.push_back("polymorphic:" + name + ":" +
            (entry->polymorphic() ? "1" : "0"));
        for (const auto& base : t.G.bases(handle))
            rows.push_back("base:" + name + ":" +
                identity_name(t.ids, t.strings, base.type) + ":" +
                std::to_string(static_cast<unsigned>(base.access)) + ":" +
                std::to_string(static_cast<unsigned>(base.flags)));
        for (const auto& member : t.G.members(handle)) {
            auto row = "member:" + name + ":" +
                std::string{t.strings.get(member.name)} + ":" +
                type_name(t.G, t.ids, t.strings, member.type) + ":" +
                std::to_string(static_cast<unsigned>(member.access));
            const auto* init = t.G.construction(
                handle, t.G.find_member(handle, member.name));
            if (init)
                row += ":init=" + std::to_string(static_cast<unsigned>(init->kind)) +
                    ":" + std::to_string(init->bits()) +
                    ":" + std::to_string(init->operand);
            else row += ":init=<missing>";
            rows.push_back(std::move(row));
        }
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void parse_v2(trial& t, bool with_profile = false,
              bool semantic_measure = false, bool cursor_measure = false) {
    prepared_include_closure_v2 closure;
    prepared_include_failure_v2 physical_error;
    const auto begin = clock_type::now();
    const auto prepared = prepare_header_include_closure_v2(
        t.files, t.headers, t.config, closure, &physical_error,
        with_profile ? &t.metrics.physical_profile : nullptr);
    t.metrics.physical_ms = ms(begin, clock_type::now());
    if (!succeeded(prepared)) {
        std::cerr << "V2 physical failure: file=" <<
            file_path(t.files, physical_error.file).string() <<
            " offset=" << physical_error.source.offset <<
            " state=" << static_cast<unsigned>(physical_error.state) << '\n';
        throw std::runtime_error{"V2 physical preparation failed"};
    }
    t.metrics.prepared_files = closure.file_count();
    for (const auto header : t.headers)
        t.metrics.header_bytes += file_bytes(t.files, header);
    preprocessor_v2_failure preprocessing_error;
    parser_v2_failure syntax_error;
    // PARSER-V2-CURSOR-PERF-07: only cursor-detail supplies a profile.
    semantic_preprocessor_v2 input{
        t.files, closure.view(), t.config, t.strings, &preprocessing_error,
        cursor_measure ? &t.metrics.cursor_profile : nullptr};
    for (const auto header : t.headers) {
        auto start = clock_type::now();
        const auto started = input.start(header);
        t.metrics.preprocessor_start_ms += ms(start, clock_type::now());
        if (!succeeded(started)) {
            std::cerr << "V2 preprocessor failure: root=" <<
                file_path(t.files, header).string() <<
                " detail=" << preprocessing_error.detail <<
                " offset=" << preprocessing_error.source.offset << '\n';
            throw std::runtime_error{"V2 preprocessor.start failed"};
        }
        header_parser_v2 parser{input, t.ids, t.G, &syntax_error,
            semantic_measure ? &t.metrics.semantic_profile : nullptr};
        start = clock_type::now();
        const auto status = parser.parse();
        t.metrics.semantic_ms += ms(start, clock_type::now());
        if (!succeeded(status) || !input.finished()) {
            const auto failed_file = syntax_error.file
                ? syntax_error.file : preprocessing_error.file;
            std::cerr << "V2 Header grammar gap/failure: root=" <<
                file_path(t.files, header).string() <<
                " file=" << file_path(t.files, failed_file).string() <<
                " kind=" << static_cast<unsigned>(syntax_error.kind) <<
                " offset=" << syntax_error.source.offset <<
                " length=" << syntax_error.source.length <<
                " detail=" << syntax_error.detail <<
                " preprocessing=" << preprocessing_error.detail << '\n';
            // HEADER-V2-TYPEDEF-07: render the actual source token on failure.
            if (failed_file && t.files.contains(failed_file) &&
                t.files.content_available(failed_file)) {
                const auto data = t.files.content(failed_file);
                const auto begin = static_cast<std::size_t>(syntax_error.source.offset);
                const auto len = static_cast<std::size_t>(syntax_error.source.length);
                if (begin <= data.size() && len <= data.size() - begin) {
                    std::cerr << "V2 failing token=\"";
                    for (std::size_t i = begin; i < begin + len; ++i) {
                        const auto c = static_cast<unsigned char>(data[i]);
                        if (c == '\n') std::cerr << "\\n";
                        else if (c == '\r') std::cerr << "\\r";
                        else if (c == '\t') std::cerr << "\\t";
                        else if (c >= 32 && c < 127) std::cerr << static_cast<char>(c);
                        else std::cerr << "?";
                    }
                    std::cerr << "\"\n";
                }
            }
            throw std::runtime_error{"V2 Header parsing failed; no fallback to OLD"};
        }
    }
    t.metrics.types = t.G.type_count();
    t.metrics.members = t.G.member_count();
    t.metrics.peak_bytes = peak_working_set();
    if (semantic_measure && t.metrics.semantic_profile.graph_define_calls == 0)
        throw std::runtime_error{"Semantic profile recorded no Graph definitions"};
}

void parse_old(trial& t) {
    // OLD must see ONLY the same explicitly declared Header roots. The fresh
    // File Context assigns them contiguous root IDs and discovers includes
    // naturally during its own semantic preprocessing.
    file_context old_files;
    for (const auto root : t.headers) {
        file_id id;
        if (!succeeded(old_files.resolve(
                file_path(t.files, root), file_kind::header, id)))
            throw std::runtime_error{"OLD Header registration failed"};
    }
    const auto root_count = old_files.size();
    lexical_generation lex;
    source_preparation_failure physical_failure;
    const auto physical_begin = clock_type::now();
    const auto prepared = prepare_source_lexical_state(
        old_files, lex, &physical_failure);
    t.metrics.physical_ms = ms(physical_begin, clock_type::now());
    if (!succeeded(prepared)) {
        std::cerr << "OLD physical failure: file=" <<
            file_path(old_files, physical_failure.file).string() << '\n';
        throw std::runtime_error{"OLD lexical preparation failed"};
    }
    t.metrics.prepared_files = root_count; // Includes discovered during OLD parse.
    for (std::uint32_t i = 1; i <= root_count; ++i)
        t.metrics.header_bytes += file_bytes(old_files, file_id{i});
    parser_failure failure;
    const auto parse_begin = clock_type::now();
    const auto parsed = parse_semantic_project(
        old_files, lex, root_count, t.config, t.strings, t.ids,
        t.G, t.source_records, &failure);
    t.metrics.semantic_ms = ms(parse_begin, clock_type::now());
    if (!succeeded(parsed)) {
        std::cerr << "OLD Header failure: file=" <<
            file_path(old_files, failure.file).string() <<
            " offset=" << failure.source.offset <<
            " detail=" << failure.detail << '\n';
        throw std::runtime_error{"OLD Header parsing failed"};
    }
    t.metrics.types = t.G.type_count();
    t.metrics.members = t.G.member_count();
    t.metrics.peak_bytes = peak_working_set();
}

[[nodiscard]] std::unique_ptr<trial> run(const std::filesystem::path& project,
                                         bool old, bool profile = false,
                                         bool semantic_measure = false,
                                         bool cursor_measure = false) {
    auto value = std::make_unique<trial>();
    compose(*value, project);
    if (old) parse_old(*value);
    else parse_v2(*value, profile, semantic_measure, cursor_measure);
    return value;
}

[[nodiscard]] bool parse_size(const char* arg, unsigned& output) noexcept {
    if (arg == nullptr || *arg == '\0' || *arg == '-') return false;
    char* end = nullptr;
    const auto parsed = std::strtoul(arg, &end, 10);
    if (end == arg || *end != '\0' || parsed > 1000) return false;
    output = static_cast<unsigned>(parsed);
    return true;
}

void header(bool profile = false, bool semantic_measure = false,
            bool semantic_detail = false, bool cursor_detail = false) {
    std::cout << "mode,run,manifest_ms,physical_prepare_ms,preprocessor_start_ms,"
                 "header_semantic_parse_graph_ms,measured_sum_ms,"
                 "project_configuration_files,header_roots,source_roots_ignored,"
                 "assign_roots_ignored,prepared_files,header_source_bytes,"
                 "graph_types,graph_members,process_peak_working_set_bytes";
    if (profile) {
        std::cout << ",perf03_total_ms,workers_start_ms,materialize_wall_ms,"
                     "acquisition_prepare_ms,acquisition_read_wall_ms,"
                     "acquisition_apply_ms,lexical_wall_ms,include_scan_ms,"
                     "frontier_sort_ms,finalize_ms,read_task_elapsed_sum_ms,"
                     "lexer_task_elapsed_sum_ms,literal_task_elapsed_sum_ms,"
                     "frontiers,peak_frontier_files,read_tasks,lexed_files,"
                     "ready_files,include_records,other_wall_ms";
        // FRONTEND-PHYSICAL-11: mutually exclusive walk and candidate time.
        std::cout << ",perf11_include_walk_ms,perf11_candidate_ms,"
                     "perf11_filesystem_probe_ms,perf11_file_resolve_ms,"
                     "perf11_ensure_file_ms,perf11_candidate_calls,"
                     "perf11_filesystem_probe_calls,perf11_file_resolve_calls,"
                     "perf11_ensure_file_calls";
        // FRONTEND-PHYSICAL-12: profile-only positive resolution reuse.
        std::cout << ",perf12_cache_lookups,perf12_cache_misses,"
                     "perf12_positive_cache_hits,perf12_positive_cache_entries";
    }
    if (semantic_measure) {
        std::cout << ",perf05_type_calls,perf05_type_samples,perf05_type_sample_ms,"
                     "perf05_member_calls,perf05_member_samples,perf05_member_sample_ms,"
                     "perf05_constructor_calls,perf05_constructor_ms,"
                     "perf05_graph_define_calls,perf05_graph_defined_members,"
                     "perf05_graph_define_ms,perf05_type_estimated_ms,"
                     "perf05_member_estimated_ms";
    }
    if (semantic_detail) {
        // PARSER-V2-SEMANTIC-DETAIL-06: each operation has calls, samples,
        // sampled elapsed milliseconds and an extrapolated estimate.
        std::cout << ",perf06_identity_lookup_calls,perf06_identity_lookup_samples,"
                     "perf06_identity_lookup_sample_ms,perf06_identity_lookup_estimated_ms";
        std::cout << ",perf06_named_resolution_calls,perf06_named_resolution_samples,"
                     "perf06_named_resolution_sample_ms,perf06_named_resolution_estimated_ms";
        std::cout << ",perf06_intrinsic_parse_calls,perf06_intrinsic_parse_samples,"
                     "perf06_intrinsic_parse_sample_ms,perf06_intrinsic_parse_estimated_ms";
        std::cout << ",perf06_type_tail_calls,perf06_type_tail_samples,"
                     "perf06_type_tail_sample_ms,perf06_type_tail_estimated_ms";
        std::cout << ",perf06_member_name_calls,perf06_member_name_samples,"
                     "perf06_member_name_sample_ms,perf06_member_name_estimated_ms";
        std::cout << ",perf06_member_initializer_calls,perf06_member_initializer_samples,"
                     "perf06_member_initializer_sample_ms,perf06_member_initializer_estimated_ms";
        std::cout << ",perf06_member_append_calls,perf06_member_append_samples,"
                     "perf06_member_append_sample_ms,perf06_member_append_estimated_ms";
        std::cout << ",perf06_advance_calls,perf06_advance_samples,"
                     "perf06_advance_sample_ms,perf06_advance_estimated_ms";
    }
    if (cursor_detail) {
        std::cout << ",perf07_decode_calls,perf07_decode_samples,"
                     "perf07_decode_sample_ms,perf07_decode_estimated_ms";
        std::cout << ",perf07_cursor_advance_calls,perf07_cursor_advance_samples,"
                     "perf07_cursor_advance_sample_ms,perf07_cursor_advance_estimated_ms";
        std::cout << ",perf07_seek_next_calls,perf07_seek_next_samples,"
                     "perf07_seek_next_sample_ms,perf07_seek_next_estimated_ms";
        std::cout << ",perf07_macro_expand_calls,perf07_macro_expand_samples,"
                     "perf07_macro_expand_sample_ms,perf07_macro_expand_estimated_ms";
        std::cout << ",perf07_directive_calls,perf07_directive_samples,"
                     "perf07_directive_sample_ms,perf07_directive_estimated_ms";
        std::cout << ",perf07_raw_tokens_seen,perf07_identifiers_seen,"
                     "perf07_identifiers_unchanged,perf07_identifiers_replaced,"
                     "perf07_identifiers_elided,perf07_identifiers_with_definitions,"
                     "perf07_inactive_tokens_skipped";
        // PARSER-V2-CURSOR-DECODE-09: exact decode-path counts.
        std::cout << ",perf09_inline_tokens,perf09_fallback_tokens,"
                     "perf09_extended_delta_tokens,perf09_extended_length_tokens";
        // PARSER-V2-CURSOR-AB-DIRECTIVE-10: precise directive groups.
        std::cout << ",perf10_include,perf10_pragma,perf10_define_undef,"
                     "perf10_conditional,perf10_other";
    }
    std::cout << '\n';
}

void print(const char* mode, unsigned run_index, const sample& s,
           bool profile = false, bool semantic_measure = false,
           bool semantic_detail = false, bool cursor_detail = false) {
    const auto sum = s.manifest_ms + s.physical_ms +
        s.preprocessor_start_ms + s.semantic_ms;
    std::cout << mode << ',' << run_index << ',' << s.manifest_ms << ','
              << s.physical_ms << ',' << s.preprocessor_start_ms << ','
              << s.semantic_ms << ',' << sum << ',' << s.project_files << ','
              << s.header_roots << ',' << s.source_roots << ','
              << s.assign_roots << ',' << s.prepared_files << ','
              << s.header_bytes << ',' << s.types << ',' << s.members << ','
              << s.peak_bytes;
    if (profile) {
        const auto& p = s.physical_profile;
        const auto measured = p.workers_start_ms + p.materialize_wall_ms +
            p.lexical_wall_ms + p.include_scan_ms + p.frontier_sort_ms +
            p.finalize_ms;
        std::cout << ',' << p.total_ms << ',' << p.workers_start_ms << ','
                  << p.materialize_wall_ms << ',' << p.acquisition_prepare_ms
                  << ',' << p.acquisition_read_wall_ms << ','
                  << p.acquisition_apply_ms << ',' << p.lexical_wall_ms
                  << ',' << p.include_scan_ms << ',' << p.frontier_sort_ms
                  << ',' << p.finalize_ms << ',' << p.read_task_elapsed_sum_ms
                  << ',' << p.lexer_task_elapsed_sum_ms << ','
                  << p.literal_task_elapsed_sum_ms << ',' << p.frontiers << ','
                  << p.peak_frontier_files << ',' << p.read_tasks << ','
                  << p.lexed_files << ',' << p.ready_files << ','
                  << p.include_records << ',' << (p.total_ms - measured);
        std::cout << ',' << p.include_stream_walk_wall_ms
                  << ',' << p.include_candidate_wall_ms
                  << ',' << p.include_filesystem_probe_wall_ms
                  << ',' << p.include_file_resolve_wall_ms
                  << ',' << p.include_ensure_file_wall_ms
                  << ',' << p.include_candidate_calls
                  << ',' << p.include_filesystem_probe_calls
                  << ',' << p.include_file_resolve_calls
                  << ',' << p.include_ensure_file_calls;
        std::cout << ',' << p.include_cache_lookups
                  << ',' << p.include_cache_misses
                  << ',' << p.include_positive_cache_hits
                  << ',' << p.include_positive_cache_entries;
    }
    if (semantic_measure) {
        const auto& p = s.semantic_profile;
        const auto type_estimate = p.type_samples != 0
            ? p.type_sample_ms * static_cast<double>(p.type_calls) /
                static_cast<double>(p.type_samples) : 0.0;
        const auto member_estimate = p.member_samples != 0
            ? p.member_sample_ms * static_cast<double>(p.member_calls) /
                static_cast<double>(p.member_samples) : 0.0;
        std::cout << ',' << p.type_calls << ',' << p.type_samples
                  << ',' << p.type_sample_ms << ',' << p.member_calls
                  << ',' << p.member_samples << ',' << p.member_sample_ms
                  << ',' << p.constructor_calls << ',' << p.constructor_ms
                  << ',' << p.graph_define_calls << ',' << p.graph_defined_members
                  << ',' << p.graph_define_ms << ',' << type_estimate
                  << ',' << member_estimate;
    }
    if (semantic_detail) {
        // Every subphase is sampled independently; do not sum estimates.
        const auto& p = s.semantic_profile;
        const auto detail = [](std::uint64_t calls,
                               std::uint64_t samples,
                               double sampled_ms) {
            const double estimate = samples != 0
                ? sampled_ms * static_cast<double>(calls) /
                    static_cast<double>(samples)
                : 0.0;
            std::cout << ',' << calls << ',' << samples << ','
                      << sampled_ms << ',' << estimate;
        };
        detail(p.identity_lookup_calls, p.identity_lookup_samples, p.identity_lookup_sample_ms);
        detail(p.named_resolution_calls, p.named_resolution_samples, p.named_resolution_sample_ms);
        detail(p.intrinsic_parse_calls, p.intrinsic_parse_samples, p.intrinsic_parse_sample_ms);
        detail(p.type_tail_calls, p.type_tail_samples, p.type_tail_sample_ms);
        detail(p.member_name_calls, p.member_name_samples, p.member_name_sample_ms);
        detail(p.member_initializer_calls, p.member_initializer_samples, p.member_initializer_sample_ms);
        detail(p.member_append_calls, p.member_append_samples, p.member_append_sample_ms);
        detail(p.advance_calls, p.advance_samples, p.advance_sample_ms);
    }
    if (cursor_detail) {
        const auto& p = s.cursor_profile;
        const auto emit = [](const cursor_v2_perf07_counter& value) {
            const double estimate = value.samples != 0
                ? value.sample_ms * static_cast<double>(value.calls) /
                    static_cast<double>(value.samples)
                : 0.0;
            std::cout << ',' << value.calls << ',' << value.samples
                      << ',' << value.sample_ms << ',' << estimate;
        };
        emit(p.decode);
        emit(p.cursor_advance);
        emit(p.seek_next);
        emit(p.macro_expand);
        emit(p.directive);
        std::cout << ',' << p.raw_tokens_seen << ',' << p.identifiers_seen
                  << ',' << p.identifiers_unchanged << ',' << p.identifiers_replaced
                  << ',' << p.identifiers_elided << ',' << p.identifiers_with_definitions
                  << ',' << p.inactive_tokens_skipped;
        const auto& decode = p.decode;
        std::cout << ',' << decode.perf09_inline_tokens
                  << ',' << decode.perf09_fallback_tokens
                  << ',' << decode.perf09_extended_delta_tokens
                  << ',' << decode.perf09_extended_length_tokens;
        std::cout << ',' << p.perf10_include << ',' << p.perf10_pragma
                  << ',' << p.perf10_define_undef << ',' << p.perf10_conditional
                  << ',' << p.perf10_other;
    }
    std::cout << '\n';
}

void compare(const std::filesystem::path& project) {
    const auto old = run(project, true);
    const auto v2 = run(project, false);
    if (old->metrics.header_roots != v2->metrics.header_roots)
        throw std::runtime_error{"Root count differs between OLD and V2"};
    const auto old_rows = projection(*old);
    const auto new_rows = projection(*v2);
    if (old_rows == new_rows) {
        std::cerr << "PARSER-V2-PERF-02: HEADER_GRAPH_PARITY_PASS rows="
                  << old_rows.size() << '\n';
        return;
    }
    auto left = old_rows.begin();
    auto right = new_rows.begin();
    std::size_t reported = 0;
    while ((left != old_rows.end() || right != new_rows.end()) && reported < 15) {
        if (left == old_rows.end()) {
            std::cerr << "V2+ " << *right++ << '\n';
        } else if (right == new_rows.end()) {
            std::cerr << "OLD+ " << *left++ << '\n';
        } else if (*left == *right) {
            ++left; ++right; continue;
        } else if (*left < *right) {
            std::cerr << "OLD+ " << *left++ << '\n';
        } else {
            std::cerr << "V2+ " << *right++ << '\n';
        }
        ++reported;
    }
    throw std::runtime_error{"OLD/V2 Header Graph semantic parity FAILED"};
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5 ||
            (std::string_view{argv[1]} != "v2" &&
             std::string_view{argv[1]} != "old" &&
             std::string_view{argv[1]} != "compare" &&
             std::string_view{argv[1]} != "profile" &&
             std::string_view{argv[1]} != "semantic-profile" &&
             std::string_view{argv[1]} != "semantic-detail" &&
             std::string_view{argv[1]} != "cursor-detail")) {
            std::cerr << "Usage: ServerEngineV4ParserV2ProjectBenchmark "
                         "<v2|old|compare|profile|semantic-profile|semantic-detail|cursor-detail> <project.json> [runs warmup]\n";
            return 2;
        }
        const std::filesystem::path project = std::filesystem::absolute(argv[2]);
        if (!std::filesystem::is_regular_file(project))
            throw std::runtime_error{"ProjectPath does not exist"};
        if (std::string_view{argv[1]} == "compare") {
            if (argc != 3)
                throw std::runtime_error{"compare accepts only ProjectPath"};
            compare(project);
            return 0;
        }
        unsigned runs = 1, warmup = 0;
        if (argc == 5 &&
            (!parse_size(argv[3], runs) || !parse_size(argv[4], warmup) ||
             runs == 0))
            throw std::runtime_error{"Invalid run/warmup dimensions"};
        if (argc != 3 && argc != 5)
            throw std::runtime_error{"Provide both runs and warmup"};
        const auto old = std::string_view{argv[1]} == "old";
        const auto profile = std::string_view{argv[1]} == "profile";
        const auto cursor_detail = std::string_view{argv[1]} == "cursor-detail";
        const auto semantic_detail = cursor_detail ||
            std::string_view{argv[1]} == "semantic-detail";
        const auto semantic_measure = semantic_detail ||
            std::string_view{argv[1]} == "semantic-profile";
        std::cout << std::fixed << std::setprecision(5);
        header(profile, semantic_measure, semantic_detail, cursor_detail);
        for (unsigned i = 0; i < runs + warmup; ++i) {
            auto result = run(project, old, profile, semantic_measure, cursor_detail);
            if (i >= warmup)
                print(cursor_detail ? "v2-cursor-detail" :
                      semantic_detail ? "v2-semantic-detail" :
                      semantic_measure ? "v2-semantic-profile" :
                      profile ? "v2-header-profile" :
                      old ? "old-header-only" : "v2-header-only",
                      i - warmup + 1, result->metrics, profile,
                      semantic_measure, semantic_detail, cursor_detail);
        }
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "PARSER-V2-PERF-02: " << error.what() << '\n';
        return 1;
    }
}
