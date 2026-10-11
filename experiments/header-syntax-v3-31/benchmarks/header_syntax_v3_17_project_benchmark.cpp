// HEADER-SYNTAX-V3-16: semantic classification audit.
// HEADER-SYNTAX-V3-08: benchmark-only effective semantic stream integration.
// The production Header Parser, preprocessor, Graph and persistence are unmodified.
#include "project/project_configuration_manifest.hpp"
#include "project/file/file_context.hpp"
#include "project/file/file_kind.hpp"
#include "project/frontend/prepared_include_v2.hpp"
#include "project/frontend/preprocessor_v2.hpp"
#include "project/frontend/lexical_token.hpp"
#include "project/preprocessor_configuration.hpp"
#include "project/string/string_table.hpp"
#include "project/parser/header_parser_v2.hpp"
#include "project/graph/graph.hpp"
#include "project/semantic/identity.hpp"
#include "diagnostics/diagnostic_collection.hpp"
#include "header_syntax_v3_06.hpp"
#include "header_syntax_v3_04_reference.hpp"
#include "v3_12_classification.hpp"
#include "v3_16_canonical.hpp"
#include "v3_16_operators.hpp"
#include "v3_17_constructor.hpp"
#include "v3_18_field_initializer.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <map>
#include <array>

namespace {
using namespace cw::server;
namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;
using reference_result = v304_reference::syntax_result_03;
using candidate_result = syntax_result_03;

// A physical location is not inferred from a flattened token index. Includes
// and macro substitutions may jump between files and offsets.
struct token_origin final {
    std::uint32_t file = 0;
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
};
static_assert(sizeof(token_origin) == 12);

// Exactly one physical sample per READY prepared file in the closure.
// An include-only root can have zero syntax tokens in its physical file.
struct physical_file_tokens final {
    file_id file{};
    std::size_t raw_words = 0;
    std::size_t source_bytes = 0;
    std::vector<token_kind> tokens;
};

// An effective sample contains the expanded stream of one manifest Header root.
// This can reference many physical files, multiple times, in include order.
struct root_tokens final {
    file_id root{};
    std::size_t raw_words = 0;
    std::size_t source_bytes = 0;
    std::vector<token_kind> effective;
    std::vector<string_id> identifiers; // resolved by production preprocessor, 1:1 tokens
    std::vector<token_origin> origins; // exactly aligned to effective
    std::vector<std::uint64_t> positive_numbers; // prepared decimal positive array bounds only
    std::vector<prepared_number_v2> decoded_numbers; // source semantics for independent constructor operand decoding
};

struct world final {
    file_context files;
    project_configuration_manifest manifest;
    preprocessor_configuration config;
    diagnostic_collection diagnostics;
    std::vector<file_id> roots;
    string_table strings;
    prepared_include_closure_v2 closure;
    std::vector<physical_file_tokens> physical_files;
    std::vector<root_tokens> streams;
};

[[nodiscard]] double elapsed_ms(clock_type::time_point begin) noexcept {
    return std::chrono::duration<double, std::milli>{clock_type::now() - begin}.count();
}

[[nodiscard]] bool directive_begin(token_kind kind) noexcept {
    return kind >= token_kind::pp_include && kind < token_kind::pp_end;
}

// Physical-only baseline: same directive removal convention as FAST-08/V3-06.
// This is NOT the effective stream and is never labelled as such.
[[nodiscard]] bool decode_physical(std::span<const std::uint32_t> words,
                                   std::vector<token_kind>& output) {
    output.clear();
    bool first = true, inside_directive = false;
    std::uint32_t offset = 0;
    for (std::size_t i = 0; i < words.size();) {
        const auto token = lexical_token::from_value(words[i++]);
        if (!token) return false;
        auto delta = token.delta();
        auto length = token.length();
        if (delta == lexical_token::extended_delta) {
            if (i == words.size()) return false;
            delta = words[i++];
        }
        if (length == lexical_token::extended_length) {
            if (i == words.size()) return false;
            length = words[i++];
        }
        if (!first && delta > (std::numeric_limits<std::uint32_t>::max)() - offset)
            return false;
        offset += delta;
        if (length > (std::numeric_limits<std::uint32_t>::max)() - offset)
            return false;
        first = false;
        const auto kind = token.kind();
        if (directive_begin(kind)) inside_directive = true;
        if (inside_directive || kind == token_kind::pp_end) {
            if (kind == token_kind::pp_end) inside_directive = false;
            continue;
        }
        output.push_back(kind);
    }
    return !inside_directive;
}

[[nodiscard]] std::size_t parse_count(const char* text, const char* what,
                                      std::size_t minimum, std::size_t maximum) {
    if (!text || *text == '\0' || *text == '-')
        throw std::runtime_error{std::string{"invalid "} + what};
    char* end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    if (*end != '\0' || end == text || value < minimum || value > maximum)
        throw std::runtime_error{std::string{"invalid "} + what};
    return static_cast<std::size_t>(value);
}

[[nodiscard]] double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    if (values.empty()) throw std::runtime_error{"empty timing sample"};
    const auto n = values.size();
    return n & 1 ? values[n/2] : (values[n/2-1]+values[n/2])*0.5;
}

[[nodiscard]] std::uint64_t checksum_tokens(const root_tokens& value) noexcept {
    std::uint64_t result = 1469598103934665603ull;
    for (std::size_t i = 0; i < value.effective.size(); ++i) {
        const auto& origin = value.origins[i];
        for (auto part : {static_cast<std::uint32_t>(value.effective[i]),
                          origin.file, origin.source_offset, origin.source_length}) {
            result = (result ^ part) * 1099511628211ull;
        }
    }
    return result;
}

struct counters final {
    std::uint64_t records = 0, declarations = 0, declarators = 0;
    std::uint64_t unresolved = 0, rejected = 0, tokens = 0;
    std::uint64_t observed = 0;
    friend bool operator==(const counters&, const counters&) = default;
};

void accumulate(const candidate_result& result, bool valid, counters& out) {
    out.records += result.records.size();
    out.declarations += result.declarations.size();
    out.declarators += result.declarators.size();
    out.tokens += result.stats.tokens;
    out.rejected += !valid;
    for (const auto& d : result.declarators) out.unresolved += (d.flags & 4u) != 0;
    // Observable work: descriptor identity and count enter the checksum.
    for (const auto& d : result.declarators)
        out.observed = (out.observed * 1099511628211ull) ^
            (static_cast<std::uint64_t>(d.name) << 32) ^ d.type_end ^ d.flags;
}

void check_equal(const candidate_result& candidate,
                 const reference_result& reference,
                 file_id root, bool accepted_candidate, bool accepted_reference) {
    if (accepted_candidate != accepted_reference ||
        candidate.records.size() != reference.records.size() ||
        candidate.declarations.size() != reference.declarations.size() ||
        candidate.declarators.size() != reference.declarators.size())
        throw std::runtime_error{"Effective descriptor parity: root " +
                                 std::to_string(root.value()) + " shape/acceptance mismatch"};
    const auto mismatch = [root](std::string_view kind, std::size_t index) {
        throw std::runtime_error{"Effective descriptor parity: root " +
            std::to_string(root.value()) + " " + std::string{kind} + "[" +
            std::to_string(index) + "] mismatch"};
    };
    for (std::size_t i = 0; i < candidate.records.size(); ++i) {
        const auto& a = candidate.records[i];
        const auto& b = reference.records[i];
        if (a.begin != b.begin || a.end != b.end || a.parent != b.parent ||
            a.declaration_first != b.declaration_first ||
            a.declaration_count != b.declaration_count ||
            a.base_begin != b.base_begin || a.base_end != b.base_end ||
            a.kind != b.kind || a.forward != b.forward) mismatch("record", i);
    }
    for (std::size_t i = 0; i < candidate.declarations.size(); ++i) {
        const auto& a = candidate.declarations[i];
        const auto& b = reference.declarations[i];
        if (a.begin != b.begin || a.end != b.end || a.owner != b.owner ||
            static_cast<unsigned>(a.kind) != static_cast<unsigned>(b.kind))
            mismatch("declaration", i);
    }
    for (std::size_t i = 0; i < candidate.declarators.size(); ++i) {
        const auto& a = candidate.declarators[i];
        const auto& b = reference.declarators[i];
        if (a.declaration != b.declaration || a.type_end != b.type_end ||
            a.name != b.name || a.initializer_begin != b.initializer_begin ||
            a.pointers != b.pointers || a.references != b.references ||
            a.arrays != b.arrays || a.flags != b.flags) mismatch("declarator", i);
    }
}

// Run V3-04 and V3-06 against the *same* flattened effective root stream.
// This is V3 implementation parity, NOT semantic/Graph parity with Header V2.
[[nodiscard]] counters effective_parity(const world& input) {
    counters sums;
    candidate_result candidate;
    reference_result reference;
    for (const auto& sample : input.streams) {
        const auto a = header_syntax_v3_06::scan(sample.effective, candidate);
        const auto b = v304_reference::header_syntax_v3_04::scan(sample.effective, reference);
        check_equal(candidate, reference, sample.root, a, b);
        accumulate(candidate, a, sums);
    }
    return sums;
}

// V3-09: every measured pass is ONE independently allocated PUBLISH-syntax
// phase. No result memory survives across passes, pairs, or publications.
// The "oracle" capacity plan is for diagnostics ONLY; real PUBLISH cannot
// know exact descriptor counts without pre-scanning.
struct vector_shape final {
    std::size_t records = 0, declarations = 0, declarators = 0;
    [[nodiscard]] std::size_t bytes() const noexcept {
        return records * sizeof(syntax_record_03) +
            declarations * sizeof(syntax_declaration_03) +
            declarators * sizeof(syntax_declarator_08);
    }
    void max_with(const vector_shape& other) noexcept {
        records = std::max(records,other.records);
        declarations = std::max(declarations,other.declarations);
        declarators = std::max(declarators,other.declarators);
    }
};
[[nodiscard]] vector_shape used_shape(const candidate_result& r) noexcept {
    return {r.records.size(),r.declarations.size(),r.declarators.size()};
}
[[nodiscard]] vector_shape capacity_shape(const candidate_result& r) noexcept {
    return {r.records.capacity(),r.declarations.capacity(),r.declarators.capacity()};
}
void reserve_shape(candidate_result& r, vector_shape plan) {
    r.records.reserve(plan.records);
    r.declarations.reserve(plan.declarations);
    r.declarators.reserve(plan.declarators);
}
struct preflight_report final {
    vector_shape maximum_used{}, grown_capacity{};
    counters totals{};
};
[[nodiscard]] preflight_report inspect_effective(const world& input) {
    preflight_report report;
    candidate_result result;
    for (const auto& root : input.streams) {
        const bool accepted=header_syntax_v3_06::scan(root.effective,result);
        report.maximum_used.max_with(used_shape(result));
        report.grown_capacity.max_with(capacity_shape(result));
        accumulate(result,accepted,report.totals);
    }
    return report;
}
// Token-count-only hint available from the captured input without running a
// parser/semantic pass. Under-estimation is safe: vector grows as necessary.
// Fixed heuristic, not an experimentally derived optimal production policy.
[[nodiscard]] vector_shape token_hint_plan(const world& input) noexcept {
    std::size_t biggest=0;
    for(const auto& root:input.streams)
        biggest=std::max(biggest,root.effective.size());
    return {biggest/128+32,biggest/4+32,biggest/4+32};
}
enum class publish_mode : std::uint8_t {
    fresh_root, publish_scratch, token_reserve, oracle_reserve
};
[[nodiscard]] constexpr const char* mode_name(publish_mode mode) noexcept {
    switch(mode) {
    case publish_mode::fresh_root: return "fresh-root";
    case publish_mode::publish_scratch: return "publish-scratch";
    case publish_mode::token_reserve: return "token-reserve";
    case publish_mode::oracle_reserve: return "oracle-reserve";
    }
    return "invalid";
}
struct measured_pass final {
    counters totals{};
    vector_shape peak_capacity{};
    vector_shape requested_reserve{};
};
// Allocation, token-only reserve, parsing, model observation and destruction
// happen inside run_publish_syntax_once(); caller TIMES the entire call.
// IMPORTANT: This is a syntax-phase ablation, not actual persisted PUBLISH.
[[nodiscard]] measured_pass run_publish_syntax_once(
    const world& input,publish_mode mode,vector_shape oracle) {
    measured_pass out;
    if(mode==publish_mode::fresh_root) {
        for(const auto& root:input.streams) {
            candidate_result model;
            const bool ok=header_syntax_v3_06::scan(root.effective,model);
            accumulate(model,ok,out.totals);
            out.peak_capacity.max_with(capacity_shape(model));
        }
    } else {
        // Lifetime intentionally bounded by ONE pass.
        candidate_result model;
        if(mode==publish_mode::token_reserve) {
            out.requested_reserve=token_hint_plan(input);
            reserve_shape(model,out.requested_reserve);
        } else if(mode==publish_mode::oracle_reserve) {
            out.requested_reserve=oracle;
            reserve_shape(model,out.requested_reserve);
        }
        for(const auto& root:input.streams) {
            const bool ok=header_syntax_v3_06::scan(root.effective,model);
            accumulate(model,ok,out.totals);
            out.peak_capacity.max_with(capacity_shape(model));
        }
    }
    return out;
}
volatile std::uint64_t benchmark_sink=0;
[[nodiscard]] double timed_cold_publish_syntax(
    const world& input,publish_mode mode,vector_shape oracle,
    const counters& expected,std::size_t iterations,
    vector_shape& peak_capacity,vector_shape& requested) {
    std::vector<double> times;
    times.reserve(iterations);
    for(std::size_t i=0;i<iterations;++i) {
        const auto t0=clock_type::now();
        const auto result=run_publish_syntax_once(input,mode,oracle);
        const auto ms=elapsed_ms(t0);
        if(!(result.totals==expected))
            throw std::runtime_error{std::string{"PUBLISH-syntax model mismatch in "}+mode_name(mode)};
        if(ms<=0 || !std::isfinite(ms))
            throw std::runtime_error{"Invalid timed PUBLISH-syntax pass"};
        benchmark_sink=benchmark_sink^result.totals.observed^result.totals.declarators;
        peak_capacity.max_with(result.peak_capacity);
        requested.max_with(result.requested_reserve);
        times.push_back(ms);
    }
    return median(std::move(times));
}

void build_inputs(world& output, const fs::path& project,
                  double& compose_ms, double& physical_ms,
                  double& physical_decode_ms, double& effective_ms,
                  std::uint64_t& origin_checksum) {
    const auto t0 = clock_type::now();
    const auto composed = compose_project_configuration(
        project, operation_id{1}, output.diagnostics, output.manifest,
        output.files, output.config);
    if (!succeeded(composed)) throw std::runtime_error{"compose_project_configuration failed"};
    for (std::uint32_t i = 1; i <= output.files.size(); ++i) {
        const file_id file{i};
        if (output.files.kind(file) == file_kind::header) output.roots.push_back(file);
    }
    if (output.roots.empty()) throw std::runtime_error{"No Header roots in project manifest"};
    compose_ms = elapsed_ms(t0);

    prepared_include_failure_v2 failure;
    const auto t1 = clock_type::now();
    if (!succeeded(prepare_header_include_closure_v2(
            output.files, output.roots, output.config, output.closure, &failure)))
        throw std::runtime_error{"Physical include closure failed at file_id " +
                                 std::to_string(failure.file.value())};
    physical_ms = elapsed_ms(t1);

    const auto view = output.closure.view();

    // PHYSICAL CORPUS: decode each distinct READY file of the prepared
    // include closure, not just manifest roots. A root containing only
    // #include directives has no non-directive physical tokens, but its
    // included files may contribute millions of real physical tokens.
    const auto decode_begin = clock_type::now();
    output.physical_files.reserve(output.closure.file_count());
    for (std::uint32_t index = 1; index <= output.files.size(); ++index) {
        const file_id id{index};
        const auto* prepared = view.file(id);
        if (!prepared) continue; // Not a READY prepared physical file.
        physical_file_tokens entry;
        entry.file = id;
        entry.raw_words = prepared->words.size();
        entry.source_bytes = output.files.content_available(id)
            ? output.files.content(id).size() : 0;
        if (!decode_physical(prepared->words, entry.tokens))
            throw std::runtime_error{"Physical decode failed at file_id " +
                                     std::to_string(id.value())};
        output.physical_files.push_back(std::move(entry));
    }
    physical_decode_ms = elapsed_ms(decode_begin);
    if (output.physical_files.empty())
        throw std::runtime_error{"No READY physical files found in prepared closure"};

    // EFFECTIVE CORPUS: execute the production semantic preprocessor once
    // for each manifest root; keep root-local include expansion and origin.
    output.streams.reserve(output.roots.size());
    const auto capture_begin = clock_type::now();
    for (const auto root : output.roots) {
        root_tokens entry;
        entry.root = root;
        const auto* file = view.file(root);
        if (!file)
            throw std::runtime_error{"Prepared root missing at file_id " +
                                     std::to_string(root.value())};
        entry.raw_words = file->words.size();
        entry.source_bytes = output.files.content_available(root)
            ? output.files.content(root).size() : 0;

        preprocessor_v2_failure semantic_failure;
        semantic_preprocessor_v2 preprocessor{
            output.files, view, output.config, output.strings, &semantic_failure};
        const auto start = preprocessor.start(root);
        if (!succeeded(start))
            throw std::runtime_error{"Effective preprocessing start failed root_id=" +
                                     std::to_string(root.value()) +
                                     " failure_kind=" + std::to_string(static_cast<int>(semantic_failure.kind))};
        while (const auto* token = preprocessor.current()) {
            // Semantic output already has active macro/include/conditional state.
            // Preserve one file/offset/length tuple per output token, including
            // tokens originating in included files or macro substitutions.
            if (!token->file || !output.files.contains(token->file) ||
                !output.files.content_available(token->file) ||
                token->source_offset > output.files.content(token->file).size() ||
                token->source_length > output.files.content(token->file).size() - token->source_offset)
                throw std::runtime_error{"Invalid effective token provenance"};
            entry.effective.push_back(token->kind);
            entry.identifiers.push_back(token->identifier);
            entry.decoded_numbers.push_back(token->number);
            entry.positive_numbers.push_back(
                token->kind == token_kind::pp_number &&
                token->number.kind == prepared_number_kind_v2::unsigned_integer
                ? token->number.bits : 0);
            entry.origins.push_back({token->file.value(), token->source_offset,
                                     token->source_length});
            if (!succeeded(preprocessor.advance()))
                throw std::runtime_error{"Effective preprocessing advance failed root_id=" +
                                         std::to_string(root.value()) +
                                         " failure_kind=" + std::to_string(static_cast<int>(semantic_failure.kind))};
        }
        if (!preprocessor.finished() || (entry.origins.size() != entry.effective.size() || entry.identifiers.size() != entry.effective.size() || entry.positive_numbers.size() != entry.effective.size() || entry.decoded_numbers.size()!=entry.effective.size()))
            throw std::runtime_error{"Incomplete effective stream"};
        origin_checksum ^= checksum_tokens(entry);
        output.streams.push_back(std::move(entry));
    }
    effective_ms = elapsed_ms(capture_begin);
}

// Account for corpus size at the input-vector boundary, independently of
// parser statistics. A parser cannot manufacture (or erase) a corpus size.
struct corpus_observation final {
    std::size_t raw_words = 0;
    std::size_t physical_tokens = 0;
    std::size_t effective_tokens = 0;
    std::size_t nonempty_physical_files = 0;
    std::size_t nonempty_effective_roots = 0;
};

[[nodiscard]] corpus_observation observe_corpus(const world& input) noexcept {
    corpus_observation report;
    for (const auto& file : input.physical_files) {
        report.raw_words += file.raw_words;
        report.physical_tokens += file.tokens.size();
        report.nonempty_physical_files += !file.tokens.empty();
    }
    for (const auto& root : input.streams) {
        report.effective_tokens += root.effective.size();
        report.nonempty_effective_roots += !root.effective.empty();
    }
    return report;
}

void print_corpus_probe(const world& input,
                        const corpus_observation& c,
                        const counters* physical = nullptr,
                        const counters* effective = nullptr) {
    std::cerr << "corpus_probe roots=" << input.roots.size()
              << " prepared_files=" << input.closure.file_count()
              << " physical_files=" << input.physical_files.size()
              << " raw_words_all_prepared=" << c.raw_words
              << " physical_input_tokens=" << c.physical_tokens
              << " effective_input_tokens=" << c.effective_tokens
              << " nonempty_physical_files=" << c.nonempty_physical_files
              << " nonempty_effective_roots=" << c.nonempty_effective_roots;
    if (physical) std::cerr << " physical_parser_tokens=" << physical->tokens;
    if (effective) std::cerr << " effective_parser_tokens=" << effective->tokens;
    std::cerr << '\n';
    for (std::size_t i = 0; i < std::min<std::size_t>(input.streams.size(), 5); ++i) {
        const auto& r = input.streams[i];
        std::cerr << "corpus_root id=" << r.root.value()
                  << " source_bytes=" << r.source_bytes
                  << " raw_words=" << r.raw_words
                  << " effective=" << r.effective.size() << '\n';
    }
    std::size_t displayed = 0;
    for (const auto& file : input.physical_files) {
        if (file.tokens.empty()) continue;
        std::cerr << "corpus_physical_file id=" << file.file.value()
                  << " source_bytes=" << file.source_bytes
                  << " raw_words=" << file.raw_words
                  << " physical=" << file.tokens.size() << '\n';
        if (++displayed == 5) break;
    }
}

// This is a SHADOW integration, not a replacement parser. The semantic Graph
// is constructed exclusively by production Header V2. V3 is a syntax observer.
struct graph_observation final {
    std::size_t types=0, members=0, objects=0, links=0;
    double v2_parse_ms=0;
};

struct semantic_world final {
    string_table strings;
    identity_space identities{strings};
    graph G;
    graph_observation metrics;
};

void run_header_v2_semantic(world& input, semantic_world& output) {
    const auto start=clock_type::now();
    for(const auto root : input.roots) {
        preprocessor_v2_failure preproc_failure;
        parser_v2_failure parser_failure;
        semantic_preprocessor_v2 preprocessor{
            input.files,input.closure.view(),input.config,output.strings,&preproc_failure};
        const auto started=preprocessor.start(root);
        if(!succeeded(started)) {
            throw std::runtime_error{"Header V2 preprocessor start failed, root="+
                std::to_string(root.value())+", kind="+
                std::to_string(static_cast<int>(preproc_failure.kind))};
        }
        header_parser_v2 parser{preprocessor,output.identities,output.G,&parser_failure};
        if(!succeeded(parser.parse()) || !preprocessor.finished()) {
            throw std::runtime_error{"Header V2 semantic Graph FAILED, root="+
                std::to_string(root.value())+", failure="+
                std::string{parser_failure.detail}};
        }
    }
    output.metrics.v2_parse_ms=elapsed_ms(start);
    output.metrics.types=output.G.type_count();
    output.metrics.members=output.G.member_count();
    output.metrics.objects=output.G.object_count();
    output.metrics.links=output.G.link_count();
}

// Bridge audit: V3 declarator offset refers to an identifier in the *same*
// effective root stream, and that token contains the canonical identifier
// resolved by the real preprocessor. Names are NOT inferred from text offsets.
struct bridge_observation final {
    std::uint64_t declarators=0, named=0, unresolved=0;
    std::uint64_t invalid_indices=0, invalid_names=0, token_identity_checksum=0;
};

[[nodiscard]] bridge_observation inspect_bridge(const world& input) {
    bridge_observation out;
    syntax_result_03 result;
    reserve_shape(result,token_hint_plan(input));
    for(const auto& root : input.streams) {
        if(root.identifiers.size()!=root.effective.size() ||
           root.origins.size()!=root.effective.size())
            throw std::runtime_error{"Effective token/identity/provenance arrays not aligned"};
        if(!header_syntax_v3_06::scan(root.effective,result))
            throw std::runtime_error{"V3-06 rejected effective root"};
        for(const auto& d : result.declarators) {
            ++out.declarators;
            out.unresolved += (d.flags & 4u)!=0;
            if(d.declaration>=result.declarations.size() ||
               d.type_end>root.effective.size() ||
               d.initializer_begin>root.effective.size() ||
               (d.name!=0xffffffffu && d.name>=root.effective.size())) {
                ++out.invalid_indices;
                continue;
            }
            if(d.name==0xffffffffu) continue;
            ++out.named;
            const auto id=root.identifiers[d.name];
            if(root.effective[d.name]!=token_kind::identifier || !id) {
                ++out.invalid_names;
                continue;
            }
            out.token_identity_checksum=(out.token_identity_checksum*1099511628211ull) ^
                (static_cast<std::uint64_t>(id.value())<<32) ^ d.name ^
                root.origins[d.name].file;
        }
    }
    return out;
}

// V3-12: owner/name semantic matching against authoritative Header V2 G.
// A match proves only that V3 found a spelled member within a uniquely
// identified record. It does NOT prove type, initializer, constructor or G parity.
struct diagnostic_row final {
    std::string reason, owner, symbol, detail;
    std::uint32_t file=0, offset=0, length=0;
};
struct classification_totals final {
    std::uint64_t raw_unmatched_graph_members=0, raw_callable_graph_collisions=0;
    std::uint64_t reclassified_parenthesized_arrays=0, reclassified_graph_matches=0;
    std::uint64_t graph_record_types=0, graph_members=0;
    std::uint64_t v3_records=0, matched_records=0, unknown_records=0, ambiguous_records=0;
    std::uint64_t record_kind_mismatches=0;
    std::uint64_t v3_fields=0, matched_fields=0, unmatched_fields=0;
    std::uint64_t unmatched_graph_members=0, duplicate_v3_members=0;
    std::uint64_t callable=0, constructor_like=0, other_callable=0;
    std::uint64_t callable_graph_collisions=0;
    std::uint64_t unresolved=0, unnamed_fields=0, invalid_names=0, invalid_owners=0;
    std::uint64_t graph_record_unmapped=0;
    std::uint64_t member_type_checks=0; // Intentionally zero: not implemented.
    std::uint64_t sampled_diagnostics=0;
};
struct graph_type_lookup final {
    std::string short_name;
    std::size_t graph_index=0;
    std::unordered_map<std::string,std::size_t> members;
    std::unordered_set<std::size_t> seen;
    std::unordered_set<std::size_t> seen_original;
    token_origin record_origin{};
    struct candidate_note final {
        token_origin origin{};
        std::uint32_t begin=0;
        std::uint32_t flags=0;
        bool array_shape=false;
    };
    std::unordered_map<std::size_t,candidate_note> candidate_for_member;
};

[[nodiscard]] std::string escape_csv(std::string_view text) {
    std::string result{"\""};
    for (char c:text) {
        if (c=='\"') result += "\"\"";
        else if (c=='\r' || c=='\n') result += ' ';
        else result += c;
    }
    result += '\"';
    return result;
}

[[nodiscard]] std::string record_name(const root_tokens& root,
                                      const syntax_record_03& rec,
                                      const string_table& strings) {
    const std::size_t index=static_cast<std::size_t>(rec.begin)+1;
    if(index>=root.identifiers.size() || root.effective[index]!=token_kind::identifier ||
       !root.identifiers[index]) return {};
    return std::string{strings.get(root.identifiers[index])};
}

[[nodiscard]] bool constructor_shaped(const root_tokens& root,
                                      const syntax_declaration_03& decl,
                                      std::string_view owner_name,
                                      const string_table& strings) {
    if(owner_name.empty() || decl.begin>=decl.end || decl.end>root.effective.size())
        return false;
    // A constructor begins with RecordName(. Distinguish it from methods
    // with return types; do not claim constructor body semantic validation.
    const auto begin=static_cast<std::size_t>(decl.begin);
    if(begin+1>=decl.end || root.effective[begin]!=token_kind::identifier ||
       root.effective[begin+1]!=token_kind::l_paren || !root.identifiers[begin])
        return false;
    return strings.get(root.identifiers[begin])==owner_name;
}

struct classifier_result final {
    classification_totals totals;
    std::vector<diagnostic_row> examples;
    struct missing_member_row final {
        std::string stage, reason, owner, symbol, detail;
        token_origin origin{};
    };
    std::vector<missing_member_row> all_missing;
};

[[nodiscard]] classifier_result audit_graph_fields(const world& input,
                                                    const semantic_world& semantic) {
    classifier_result result;
    auto& n=result.totals;
    constexpr std::size_t example_limit=300;
    const auto& type_entries=semantic.G.type_entries();
    const auto& type_ids=semantic.G.type_identity_entries();
    const auto& member_entries=semantic.G.member_entries();
    if(type_entries.size()!=type_ids.size())
        throw std::runtime_error{"Graph types/type identities size mismatch"};
    std::vector<graph_type_lookup> graph_types;
    graph_types.reserve(type_entries.size());
    std::unordered_map<std::string,std::vector<std::size_t>> by_short_name;
    for(std::size_t i=0;i<type_entries.size();++i) {
        const auto& type=type_entries[i];
        if(type.kind!=graph_type_kind::record || !type.defined()) continue;
        identity_record identity;
        if(!semantic.identities.record(type_ids[i],identity) || !identity.name)
            throw std::runtime_error{"Graph record identity invalid"};
        graph_type_lookup entry;
        entry.short_name=std::string{semantic.strings.get(identity.name)};
        entry.graph_index=i;
        if(type.members.begin>member_entries.size() ||
           type.members.count>member_entries.size()-type.members.begin)
            throw std::runtime_error{"Graph member range invalid"};
        for(std::size_t j=type.members.begin;j<static_cast<std::size_t>(type.members.begin)+type.members.count;++j) {
            if(!member_entries[j].name) throw std::runtime_error{"Graph member has no name"};
            auto name=std::string{semantic.strings.get(member_entries[j].name)};
            if(!entry.members.emplace(std::move(name),j).second)
                throw std::runtime_error{"Duplicate member name in authoritative Graph"};
            ++n.graph_members;
        }
        by_short_name[entry.short_name].push_back(graph_types.size());
        graph_types.push_back(std::move(entry));
        ++n.graph_record_types;
    }
    auto add=[&](std::string reason, std::string owner, std::string symbol,
                 const root_tokens& root,std::uint32_t position, std::string detail) {
        if(result.examples.size()>=example_limit) return;
        token_origin o{};
        if(position<root.origins.size()) o=root.origins[position];
        result.examples.push_back({std::move(reason),std::move(owner),std::move(symbol),
                                   std::move(detail),o.file,o.source_offset,o.source_length});
    };
    std::vector<std::uint8_t> mapped_graph_records(graph_types.size(),0);
    syntax_result_03 model;
    reserve_shape(model,token_hint_plan(input));
    for(const auto& root:input.streams) {
        if(!header_syntax_v3_06::scan(root.effective,model))
            throw std::runtime_error{"V3-06 rejected effective stream during V3-12 audit"};
        std::vector<std::string> owners(model.records.size());
        std::vector<std::size_t> graph_owners(model.records.size(),std::numeric_limits<std::size_t>::max());
        for(std::size_t r=0;r<model.records.size();++r) {
            const auto& rec=model.records[r];
            if(rec.forward) continue;
            ++n.v3_records;
            owners[r]=record_name(root,rec,input.strings);
            const auto it=by_short_name.find(owners[r]);
            if(it==by_short_name.end()) {
                ++n.unknown_records;
                add("record_not_in_graph",owners[r],"",root,rec.begin,"No Graph record with this spelling");
                continue;
            }
            if(it->second.size()!=1) {
                ++n.ambiguous_records;
                add("ambiguous_record_owner",owners[r],"",root,rec.begin,
                    "Multiple scoped Graph records share this leaf name");
                continue;
            }
            const auto lookup_index=it->second.front();
            graph_owners[r]=lookup_index;
            mapped_graph_records[lookup_index]=1;
            graph_types[lookup_index].record_origin=root.origins[rec.begin];
            ++n.matched_records;
            const auto& graph_type=type_entries[graph_types[lookup_index].graph_index];
            const auto kind=rec.kind==token_kind::kw_struct ? graph_record_kind::struct_type :
                rec.kind==token_kind::kw_class ? graph_record_kind::class_type :graph_record_kind::union_type;
            if(kind!=graph_type.record_kind) {
                ++n.record_kind_mismatches;
                add("record_kind_mismatch",owners[r],"",root,rec.begin,"V3 and V2 record kind differ");
            }
        }
        for(const auto& d:model.declarators) {
            if(d.declaration>=model.declarations.size())
                throw std::runtime_error{"V3 descriptor invalid declaration index"};
            const auto& decl=model.declarations[d.declaration];
            if(decl.owner>=model.records.size()) {
                ++n.invalid_owners;
                add("invalid_owner","","",root,decl.begin,"Declaration owner index invalid");
                continue;
            }
            const auto& owner=owners[decl.owner];
            const auto owner_id=graph_owners[decl.owner];
            const bool original_field=decl.kind==syntax_decl_03::field_candidate;
            const bool originally_callable=decl.kind==syntax_decl_03::method_candidate ||
                decl.kind==syntax_decl_03::ambiguous_callable;
            const bool parenthesized_array=v312::parenthesized_array_field(
                root.effective,decl,d);
            const bool is_field=original_field || parenthesized_array;
            const bool is_callable=originally_callable && !parenthesized_array;
            if(parenthesized_array) ++n.reclassified_parenthesized_arrays;
            if(!is_field && !is_callable) continue;
            n.unresolved+=(d.flags&4u)!=0;
            if(is_field) ++n.v3_fields;
            else {
                ++n.callable;
                if(constructor_shaped(root,decl,owner,input.strings))++n.constructor_like;
                else ++n.other_callable;
            }
            if(d.name==0xffffffffu) {
                if(is_field) {
                    ++n.unnamed_fields;
                    add("field_name_unresolved",owner,"",root,decl.begin,
                        "No resolved name in V3 declarator");
                }
                continue;
            }
            if(d.name>=root.identifiers.size() || root.effective[d.name]!=token_kind::identifier ||
               !root.identifiers[d.name]) {
                ++n.invalid_names;
                add("invalid_declarator_name",owner,"",root,decl.begin,"Name not a valid effective identifier");
                continue;
            }
            const std::string spelling{input.strings.get(root.identifiers[d.name])};
            if(owner_id==std::numeric_limits<std::size_t>::max()) continue;
            auto& graph_owner=graph_types[owner_id];
            const auto it=graph_owner.members.find(spelling);
            if(it!=graph_owner.members.end()) {
                // For the complete pre-repair missing-member report, retain the
                // first lexical candidate (including formerly-callable arrays).
                const auto origin=root.origins[d.name];
                graph_owner.candidate_for_member.try_emplace(it->second,
                    graph_type_lookup::candidate_note{
                        origin,decl.begin,d.flags,parenthesized_array});
                if(original_field) graph_owner.seen_original.insert(it->second);
                if(originally_callable) ++n.raw_callable_graph_collisions;
            }
            if(is_field) {
                if(it==graph_owner.members.end()) {
                    ++n.unmatched_fields;
                    add("field_not_in_graph",owner,spelling,root,d.name,
                        "V3 field candidate name not found in V2 Graph owner");
                } else if(!graph_owner.seen.insert(it->second).second) {
                    ++n.duplicate_v3_members;
                    add("duplicate_v3_field",owner,spelling,root,d.name,
                        "Same V2 member matched multiple V3 declarators");
                } else {
                    ++n.matched_fields;
                    if(parenthesized_array) ++n.reclassified_graph_matches;
                }
            } else if(it!=graph_owner.members.end()) {
                ++n.callable_graph_collisions;
                add("callable_is_v2_member",owner,spelling,root,d.name,
                    "V3 marked callable but V2 Graph contains a member with this name");
            }
        }
    }
    for(std::size_t i=0;i<graph_types.size();++i) {
        auto& graph_owner=graph_types[i];
        n.graph_record_unmapped+=mapped_graph_records[i]==0;
        if(graph_owner.seen.size()>graph_owner.members.size() ||
           graph_owner.seen_original.size()>graph_owner.members.size())
            throw std::runtime_error{"Invalid seen Graph member cardinality"};
        n.unmatched_graph_members+=graph_owner.members.size()-graph_owner.seen.size();
        n.raw_unmatched_graph_members+=graph_owner.members.size()-graph_owner.seen_original.size();
        for(const auto& [symbol,member_index]:graph_owner.members) {
            const auto emit_missing=[&](std::string_view stage){
                const auto it=graph_owner.candidate_for_member.find(member_index);
                const bool has_candidate=it!=graph_owner.candidate_for_member.end();
                const auto origin=has_candidate ? it->second.origin : graph_owner.record_origin;
                std::string description=has_candidate
                    ? "name candidate; declaration_begin="+std::to_string(it->second.begin)+
                      "; descriptor_flags="+std::to_string(it->second.flags)+
                      "; recognized_parenthesized_array="+
                      (it->second.array_shape?"yes":"no")
                    : "No V3 candidate with this name; location is record keyword";
                result.all_missing.push_back({std::string{stage},"missing_graph_member",
                    graph_owner.short_name,symbol,std::move(description),origin});
            };
            if(!graph_owner.seen_original.contains(member_index)) emit_missing("before");
            if(!graph_owner.seen.contains(member_index)) emit_missing("after");
        }
    }
    std::sort(result.all_missing.begin(),result.all_missing.end(),
        [](const auto& a,const auto& b){
            if(a.stage!=b.stage)return a.stage<b.stage;
            if(a.owner!=b.owner)return a.owner<b.owner;
            return a.symbol<b.symbol;
        });
    n.sampled_diagnostics=result.examples.size();
    return result;
}


// V3-13: this is an observation of the existing V3 syntax and the V2 Graph.
// It NEVER assigns Graph handles or mutates V3/Graph. In particular, a
// successful type-shape check does NOT establish initializer/constructor parity.
struct semantic_detail_row final {
    std::string reason, owner, symbol, category, detail;
    token_origin origin{};
};
struct semantic_detail_totals final {
    std::uint64_t checked_graph_fields=0, missing_graph_fields=0;
    std::uint64_t derived_shape_checks=0, derived_shape_matches=0,
        derived_shape_mismatches=0, derived_shape_not_checked=0;
    std::uint64_t builtin_checks=0, builtin_matches=0, builtin_mismatches=0,
        builtin_not_checked=0;
    std::uint64_t unresolved=0, unresolved_fields=0, unresolved_callable=0,
        unresolved_other=0;
    std::uint64_t callable=0, callable_graph_collisions=0,
        constructor_shape=0, method_body_shape=0, other_callable_shape=0;
    std::uint64_t invalid_graph_type_chain=0;
};
struct semantic_detail_report final {
    semantic_detail_totals totals;
    std::map<std::string,std::size_t> unresolved_reasons;
    std::map<std::string,std::size_t> collision_reasons;
    std::vector<semantic_detail_row> unresolved_rows;
    std::vector<semantic_detail_row> collision_rows;
    std::vector<semantic_detail_row> type_examples;
};
struct graph_derived_shape final {
    unsigned pointers=0, references=0, arrays=0;
    unsigned consts=0, volatiles=0;
    type_ref base{};
    bool valid=true;
};
[[nodiscard]] graph_derived_shape graph_shape(const graph& G,type_ref type) {
    graph_derived_shape out;
    for(unsigned depth=0; depth<256; ++depth) {
        if(!type || type.kind()!=type_ref_kind::derived) {
            out.base=type;
            out.valid=static_cast<bool>(type);
            return out;
        }
        derived_type_record d;
        if(!G.derived(type,d) || !d.child) { out.valid=false;return out; }
        switch(d.kind) {
            case derived_type_kind::pointer: ++out.pointers;break;
            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference: ++out.references;break;
            case derived_type_kind::bounded_array:
            case derived_type_kind::unbounded_array: ++out.arrays;break;
            case derived_type_kind::const_qualified: ++out.consts;break;
            case derived_type_kind::volatile_qualified: ++out.volatiles;break;
            default:out.valid=false;return out;
        }
        type=d.child;
    }
    out.valid=false;
    return out;
}

// Recognize only unambiguous intrinsic spellings in the V3 type prefix.
// User-defined aliases and dependent type syntax are marked NOT_CHECKED.
[[nodiscard]] intrinsic_type unambiguous_builtin_type(
    std::span<const token_kind> tokens,
    std::uint32_t begin,std::uint32_t end) noexcept {
    if(begin>=end || end>tokens.size())return intrinsic_type::none;
    unsigned signed_kw=0,unsigned_kw=0,short_kw=0,long_kw=0,int_kw=0;
    intrinsic_type isolated=intrinsic_type::none;
    for(std::uint32_t i=begin;i<end;++i) {
        switch(tokens[i]) {
            case token_kind::kw_const:case token_kind::kw_volatile:break;
            case token_kind::kw_signed:++signed_kw;break;
            case token_kind::kw_unsigned:++unsigned_kw;break;
            case token_kind::kw_short:++short_kw;break;
            case token_kind::kw_long:++long_kw;break;
            case token_kind::kw_int:++int_kw;break;
            case token_kind::kw_void: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::void_type;break;
            case token_kind::kw_bool: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::bool_type;break;
            case token_kind::kw_char: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::char_type;break;
            case token_kind::kw_float: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::float_type;break;
            case token_kind::kw_double: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::double_type;break;
            case token_kind::kw_wchar_t: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::wchar_type;break;
            case token_kind::kw_char8_t: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::char8_type;break;
            case token_kind::kw_char16_t: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::char16_type;break;
            case token_kind::kw_char32_t: if(isolated!=intrinsic_type::none)return intrinsic_type::none;isolated=intrinsic_type::char32_type;break;
            default:return intrinsic_type::none;
        }
    }
    if(signed_kw>1||unsigned_kw>1||short_kw>1||long_kw>2||int_kw>1||
       (signed_kw && unsigned_kw) || (short_kw && long_kw))return intrinsic_type::none;
    if(isolated!=intrinsic_type::none) {
        if(short_kw||long_kw||int_kw||signed_kw||unsigned_kw) {
            if(isolated==intrinsic_type::char_type && (signed_kw||unsigned_kw) &&
                !short_kw&&!long_kw&&!int_kw)
                return signed_kw?intrinsic_type::signed_char:intrinsic_type::unsigned_char;
            if(isolated==intrinsic_type::double_type && long_kw==1 &&
                !short_kw&&!int_kw&&!signed_kw&&!unsigned_kw)
                return intrinsic_type::long_double_type;
            return intrinsic_type::none;
        }
        return isolated;
    }
    if(short_kw) return unsigned_kw?intrinsic_type::unsigned_short:intrinsic_type::signed_short;
    if(long_kw==2)return unsigned_kw?intrinsic_type::unsigned_long_long:intrinsic_type::signed_long_long;
    if(long_kw==1)return unsigned_kw?intrinsic_type::unsigned_long:intrinsic_type::signed_long;
    if(signed_kw||unsigned_kw||int_kw)return unsigned_kw?intrinsic_type::unsigned_int:intrinsic_type::signed_int;
    return intrinsic_type::none;
}

[[nodiscard]] const char* declaration_kind_name(syntax_decl_03 kind) noexcept {
    switch(kind) {
        case syntax_decl_03::field_candidate:return "field_candidate";
        case syntax_decl_03::method_candidate:return "method_candidate";
        case syntax_decl_03::ambiguous_callable:return "ambiguous_callable";
        case syntax_decl_03::record:return "record";
        case syntax_decl_03::forward_record:return "forward_record";
        case syntax_decl_03::access:return "access";
        case syntax_decl_03::typedef_decl:return "typedef_decl";
        case syntax_decl_03::unsupported:return "unsupported";
    }
    return "unknown";
}

// A callable whose name is also in Graph remains ambiguous. A direct
// initializer cannot be identified reliably from token_kind alone when its
// operands are identifier expressions; annotate shapes rather than rewrite.
[[nodiscard]] std::string callable_shape(const root_tokens& root,
    const syntax_declaration_03& decl,std::string_view owner,
    const string_table& strings) {
    if(constructor_shaped(root,decl,owner,strings))return "constructor_shape";
    if(decl.kind==syntax_decl_03::method_candidate)return "method_body_candidate";
    if(decl.begin>=decl.end || decl.end>root.effective.size())return "malformed_span";
    const auto b=static_cast<std::size_t>(decl.begin);
    const auto e=static_cast<std::size_t>(decl.end);
    bool has_numeric=false,has_literal=false,has_equal=false;
    for(auto i=b;i<e;++i) {
        has_numeric|=(root.effective[i]==token_kind::pp_number);
        has_literal|=(root.effective[i]==token_kind::string_literal ||
                      root.effective[i]==token_kind::character_literal);
        has_equal|=(root.effective[i]==token_kind::assign);
    }
    if(has_numeric || has_literal)return "possible_direct_initializer_literal";
    if(has_equal)return "possible_initializer_expression";
    return "ambiguous_callable_or_direct_initializer";
}

[[nodiscard]] semantic_detail_report audit_semantic_details(
    const world& input,const semantic_world& semantic) {
    semantic_detail_report output;
    auto& n=output.totals;
    const auto types=semantic.G.type_entries();
    const auto tids=semantic.G.type_identity_entries();
    const auto members=semantic.G.member_entries();
    if(types.size()!=tids.size()) throw std::runtime_error{"V3-13 Graph type identity size mismatch"};
    struct owner_entry { std::size_t type_index; std::unordered_map<std::string,std::size_t> members; };
    std::unordered_map<std::string,std::vector<owner_entry>> by_name;
    for(std::size_t i=0;i<types.size();++i) {
        if(types[i].kind!=graph_type_kind::record || !types[i].defined())continue;
        identity_record identity;
        if(!semantic.identities.record(tids[i],identity)||!identity.name)
            throw std::runtime_error{"V3-13 Graph record identity invalid"};
        owner_entry owner{i,{}};
        const auto& range=types[i].members;
        if(range.begin>members.size()||range.count>members.size()-range.begin)
            throw std::runtime_error{"V3-13 Graph record member range invalid"};
        for(std::size_t j=range.begin;j<static_cast<std::size_t>(range.begin)+range.count;++j)
            owner.members.emplace(std::string{semantic.strings.get(members[j].name)},j);
        by_name[std::string{semantic.strings.get(identity.name)}].push_back(std::move(owner));
    }
    syntax_result_03 model;
    reserve_shape(model,token_hint_plan(input));
    for(const auto& root:input.streams) {
        if(!header_syntax_v3_06::scan(root.effective,model))
            throw std::runtime_error{"V3-13 syntax scan rejected effective corpus"};
        std::vector<std::string> owner_names(model.records.size());
        std::vector<const owner_entry*> resolved(model.records.size());
        for(std::size_t ri=0;ri<model.records.size();++ri) {
            const auto& rec=model.records[ri];
            if(rec.forward)continue;
            owner_names[ri]=record_name(root,rec,input.strings);
            auto it=by_name.find(owner_names[ri]);
            if(it!=by_name.end()&&it->second.size()==1)resolved[ri]=&it->second[0];
        }
        for(const auto& d:model.declarators) {
            if(d.declaration>=model.declarations.size())throw std::runtime_error{"V3-13 invalid declaration index"};
            const auto& decl=model.declarations[d.declaration];
            if(decl.owner>=model.records.size())throw std::runtime_error{"V3-13 invalid owner index"};
            const auto& owner=owner_names[decl.owner];
            const bool parenthesized=v312::parenthesized_array_field(root.effective,decl,d);
            const bool field=decl.kind==syntax_decl_03::field_candidate || parenthesized;
            const bool callable=(decl.kind==syntax_decl_03::method_candidate ||
                decl.kind==syntax_decl_03::ambiguous_callable) && !parenthesized;
            const bool unresolved=(d.flags&4u)!=0;
            const std::uint32_t origin_index=d.name!=0xffffffffu && d.name<root.origins.size() ? d.name : decl.begin;
            const token_origin origin=origin_index<root.origins.size()?root.origins[origin_index]:token_origin{};
            std::string member_name;
            if(d.name!=0xffffffffu && d.name<root.effective.size() &&
                root.effective[d.name]==token_kind::identifier && root.identifiers[d.name])
                member_name=std::string{input.strings.get(root.identifiers[d.name])};
            const owner_entry* owner_lookup=resolved[decl.owner];
            const std::size_t* graph_member=nullptr;
            if(owner_lookup && !member_name.empty()) {
                const auto found=owner_lookup->members.find(member_name);
                if(found!=owner_lookup->members.end()) graph_member=&found->second;
            }
            if(unresolved) {
                ++n.unresolved;
                n.unresolved_fields+=field;
                n.unresolved_callable+=callable;
                n.unresolved_other+=!field&&!callable;
                std::string reason=d.name==0xffffffffu?"no_name":
                    (d.type_end==decl.begin?"empty_type_prefix":"other_unresolved");
                if(callable && constructor_shaped(root,decl,owner,input.strings))reason="constructor_without_type";
                output.unresolved_reasons[reason]++;
                output.unresolved_rows.push_back({reason,owner,member_name,
                    declaration_kind_name(decl.kind),"type_end="+std::to_string(d.type_end)+
                    "; declaration_begin="+std::to_string(decl.begin)+
                    "; flags="+std::to_string(d.flags),origin});
            }
            if(callable) {
                ++n.callable;
                const auto category=callable_shape(root,decl,owner,input.strings);
                n.constructor_shape+=(category=="constructor_shape");
                n.method_body_shape+=(category=="method_body_candidate");
                n.other_callable_shape+=(category!="constructor_shape" && category!="method_body_candidate");
                if(graph_member) {
                    ++n.callable_graph_collisions;
                    output.collision_reasons[category]++;
                    output.collision_rows.push_back({"callable_graph_collision",owner,member_name,category,
                        "declaration_begin="+std::to_string(decl.begin)+
                        "; flags="+std::to_string(d.flags),origin});
                }
            }
            if(!field)continue;
            if(!graph_member){++n.missing_graph_fields;continue;}
            ++n.checked_graph_fields;
            const auto& graph_type=members[*graph_member].type;
            const auto shape=graph_shape(semantic.G,graph_type);
            if(!shape.valid) {++n.invalid_graph_type_chain;continue;}
            // V3 fields may contain trailing cv qualifiers and complex nested
            // declarators. Compare only pointer/reference/array topology here.
            // Modifier nesting order and array bounds are NOT checked.
            ++n.derived_shape_checks;
            if(shape.pointers==d.pointers && shape.references==d.references && shape.arrays==d.arrays) {
                ++n.derived_shape_matches;
            } else {
                ++n.derived_shape_mismatches;
                if(output.type_examples.size()<1000)output.type_examples.push_back({
                    "derived_shape_mismatch",owner,member_name,"shape_only",
                    "V3(p,r,a)="+std::to_string(d.pointers)+"/"+
                    std::to_string(d.references)+"/"+std::to_string(d.arrays)+
                    "; Graph(p,r,a)="+std::to_string(shape.pointers)+"/"+
                    std::to_string(shape.references)+"/"+std::to_string(shape.arrays),origin});
            }
            const auto expected=unambiguous_builtin_type(root.effective,decl.begin,d.type_end);
            if(expected==intrinsic_type::none || shape.base.kind()!=type_ref_kind::intrinsic) {
                ++n.builtin_not_checked;
            } else {
                ++n.builtin_checks;
                if(shape.base.payload()==static_cast<std::uint32_t>(expected))++n.builtin_matches;
                else {
                    ++n.builtin_mismatches;
                    if(output.type_examples.size()<1000)output.type_examples.push_back({
                        "builtin_base_mismatch",owner,member_name,"builtin_only",
                        "V3 intrinsic="+std::to_string(static_cast<unsigned>(expected))+
                        "; Graph intrinsic="+std::to_string(shape.base.payload()),origin});
                }
            }
        }
    }
    if(output.unresolved_rows.size()!=n.unresolved || output.collision_rows.size()!=n.callable_graph_collisions ||
       n.derived_shape_matches+n.derived_shape_mismatches!=n.derived_shape_checks ||
       n.builtin_matches+n.builtin_mismatches!=n.builtin_checks)
        throw std::runtime_error{"V3-13 invalid audit accounting"};
    return output;
}


// V3-16: independent reconstruction of canonical field-type expressions.
// Exactly the same field identity mapping as V3-13, but no Graph identity is
// reused as an "expected" source type. Unsupported syntax is NOT_CHECKED.
struct canonical_totals final {
    std::uint64_t checked=0, matched=0, mismatched=0, not_checked=0;
    std::uint64_t fields_seen=0, graph_missing=0, unnamed_count=0;
    std::map<std::string,std::uint64_t> not_checked_reasons;
    std::map<std::string,std::uint64_t> mismatch_reasons;
    std::vector<semantic_detail_row> examples;
    std::vector<semantic_detail_row> mismatch_samples;
    std::vector<semantic_detail_row> unchecked_samples;
    std::vector<semantic_detail_row> no_name;
    std::vector<semantic_detail_row> operator_rows;
    std::map<std::string,std::uint64_t> operator_categories;
    std::vector<semantic_detail_row> unresolved_name_rows;
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint64_t> full_base_pairs;
    std::map<std::string,std::uint64_t> no_name_categories;
    std::uint64_t constructor_without_type=0;
};
[[nodiscard]] std::string source_snippet(const world& world,const token_origin& origin) {
    if(!origin.file)return {};
    const file_id id{origin.file};
    if(!world.files.contains(id) || !world.files.content_available(id))return {};
    const auto bytes=world.files.content(id);
    if(origin.source_offset>bytes.size())return {};
    const auto from=origin.source_offset>24?origin.source_offset-24:0;
    const auto end=std::min<std::size_t>(bytes.size(),origin.source_offset+origin.source_length+100);
    std::string text{bytes.substr(from,end-from)};
    for(auto& ch:text)if(ch=='\r'||ch=='\n'||ch=='\t')ch=' ';
    return text;
}
[[nodiscard]] std::string base_spelling(const root_tokens& root,
    const string_table& strings,std::uint32_t begin,std::uint32_t end) {
    if(end>root.effective.size()||begin>end)return "<invalid-span>";
    std::string result;
    for(std::uint32_t i=begin;i<end;++i) {
        if(!result.empty())result+=' ';
        switch(root.effective[i]) {
        case token_kind::identifier:
            result+=std::string{strings.get(root.identifiers[i])};break;
        case token_kind::scope:result+="::";break;
        case token_kind::kw_const:result+="const";break;
        case token_kind::kw_volatile:result+="volatile";break;
        case token_kind::kw_void:result+="void";break;
        case token_kind::kw_bool:result+="bool";break;
        case token_kind::kw_char:result+="char";break;
        case token_kind::kw_wchar_t:result+="wchar_t";break;
        case token_kind::kw_signed:result+="signed";break;
        case token_kind::kw_unsigned:result+="unsigned";break;
        case token_kind::kw_short:result+="short";break;
        case token_kind::kw_long:result+="long";break;
        case token_kind::kw_int:result+="int";break;
        case token_kind::kw_float:result+="float";break;
        case token_kind::kw_double:result+="double";break;
        default:result+="token#"+std::to_string(static_cast<unsigned>(root.effective[i]));break;
        }
    }
    return result;
}
[[nodiscard]] std::string identity_scope_chain(const identity_space& ids,
    identity_ref owner) {
    std::string text;
    for(unsigned depth=0;owner && depth<256;++depth) {
        if(!text.empty())text+='>';
        text+=std::to_string(owner.value());
        identity_record record;
        if(!ids.record(owner,record) || !record.parent)break;
        owner=record.parent;
    }
    return text;
}
[[nodiscard]] canonical_totals audit_canonical_types(const world& input,const semantic_world& semantic) {
    canonical_totals totals;
    const auto types=semantic.G.type_entries();
    const auto identities=semantic.G.type_identity_entries();
    const auto members=semantic.G.member_entries();
    if(types.size()!=identities.size())throw std::runtime_error{"V3-16 type identity size mismatch"};
    struct owner_entry {
        identity_ref identity;
        std::unordered_map<std::string,std::size_t> members;
    };
    std::unordered_map<std::string,std::vector<owner_entry>> named_owners;
    for(std::size_t i=0;i<types.size();++i) {
        if(types[i].kind!=graph_type_kind::record || !types[i].defined())continue;
        identity_record record;
        if(!semantic.identities.record(identities[i],record)||!record.name)
            throw std::runtime_error{"V3-16 invalid V2 record identity"};
        owner_entry owner{identities[i],{}};
        const auto range=types[i].members;
        if(range.begin>members.size()||range.count>members.size()-range.begin)
            throw std::runtime_error{"V3-16 invalid V2 member range"};
        for(std::size_t j=range.begin;j<static_cast<std::size_t>(range.begin)+range.count;++j)
            owner.members.emplace(std::string{semantic.strings.get(members[j].name)},j);
        named_owners[std::string{semantic.strings.get(record.name)}].push_back(std::move(owner));
    }
    syntax_result_03 model;
    reserve_shape(model,token_hint_plan(input));
    for(const auto& root:input.streams) {
        if(!header_syntax_v3_06::scan(root.effective,model))
            throw std::runtime_error{"V3-16 syntax scan rejected effective stream"};
        std::vector<std::string> owner_names(model.records.size());
        std::vector<const owner_entry*> resolved(model.records.size());
        for(std::size_t ri=0;ri<model.records.size();++ri) {
            const auto& rec=model.records[ri];
            if(rec.forward)continue;
            owner_names[ri]=record_name(root,rec,input.strings);
            auto it=named_owners.find(owner_names[ri]);
            if(it!=named_owners.end()&&it->second.size()==1)resolved[ri]=&it->second[0];
        }
        const v316::source_view src{root.effective,root.identifiers,root.positive_numbers,
            &input.strings,&semantic.strings};
        for(const auto& d:model.declarators) {
            if(d.declaration>=model.declarations.size())throw std::runtime_error{"V3-16 invalid declarator index"};
            const auto& decl=model.declarations[d.declaration];
            if(decl.owner>=model.records.size())throw std::runtime_error{"V3-16 invalid owner index"};
            const auto& owner=owner_names[decl.owner];
            const bool grouped=v312::parenthesized_array_field(root.effective,decl,d);
            const bool field=decl.kind==syntax_decl_03::field_candidate||grouped;
            const bool callable=(decl.kind==syntax_decl_03::method_candidate ||
                decl.kind==syntax_decl_03::ambiguous_callable)&&!grouped;
            const std::uint32_t origin_index=d.name!=0xffffffffu&&d.name<root.origins.size()?d.name:decl.begin;
            const auto origin=origin_index<root.origins.size()?root.origins[origin_index]:token_origin{};
            if((d.flags&4u) && d.name==0xffffffffu && callable) {
                if(constructor_shaped(root,decl,owner,input.strings)) {
                    ++totals.constructor_without_type;
                } else {
                    const auto op=v316::classify_operator(root.effective,decl.begin,decl.end);
                    const auto location=origin_index<root.effective.size()?root.effective[origin_index]:token_kind::invalid;
                    const auto detail="declaration_begin="+std::to_string(decl.begin)+
                        "; declaration_end="+std::to_string(decl.end)+
                        "; token="+std::to_string(static_cast<unsigned>(location))+
                        "; source="+source_snippet(input,origin);
                    if(op!=v316::operator_form::not_operator &&
                       op!=v316::operator_form::unsupported) {
                        const std::string kind{v316::operator_form_name(op)};
                        ++totals.operator_categories[kind];
                        totals.operator_rows.push_back({"operator_declaration",owner,"",kind,detail,origin});
                    } else {
                        ++totals.unnamed_count;
                        const std::string category=op==v316::operator_form::unsupported?
                            "operator_unsupported":"unclassified_callable";
                        ++totals.no_name_categories[category];
                        totals.no_name.push_back({"no_name",owner,"",category,detail,origin});
                    }
                }
            }
            if(!field)continue;
            ++totals.fields_seen;
            const owner_entry* actual_owner=resolved[decl.owner];
            std::string symbol;
            if(d.name!=0xffffffffu && d.name<root.effective.size() &&
               root.effective[d.name]==token_kind::identifier && root.identifiers[d.name])
                symbol=std::string{input.strings.get(root.identifiers[d.name])};
            if(!actual_owner||symbol.empty()||!actual_owner->members.contains(symbol)) {
                ++totals.graph_missing;continue;
            }
            const auto idx=actual_owner->members.at(symbol);
            const auto intrinsic=unambiguous_builtin_type(root.effective,decl.begin,d.type_end);
            const auto out=v316::verify(src,decl.begin,d.type_end,d.name,
                d.initializer_begin,decl.end,grouped,actual_owner->identity,
                semantic.identities,semantic.G,members[idx].type,intrinsic);
            if(out.result==v316::verdict::not_checked) {
                ++totals.not_checked;
                ++totals.not_checked_reasons[std::string{v316::reason_name(out.why)}];
            } else {
                ++totals.checked;
                if(out.result==v316::verdict::match)++totals.matched;
                else {
                    ++totals.mismatched;
                    ++totals.mismatch_reasons[std::string{v316::reason_name(out.why)}];
                    if(out.why==v316::reason::base_mismatch)
                        ++totals.full_base_pairs[{out.expected_base,out.actual_base}];
                }
            }
            if(out.why==v316::reason::unresolved_name) {
                const auto token=out.unresolved_token;
                string_id v3_id{},v2_id{};
                std::string unresolved_word;
                if(token<root.identifiers.size()) {
                    v3_id=root.identifiers[token];
                    unresolved_word=std::string{input.strings.get(v3_id)};
                    v2_id=semantic.strings.find(unresolved_word);
                }
                const auto& owner_identity=actual_owner->identity;
                const auto detail="type_spelling="+base_spelling(root,input.strings,decl.begin,d.type_end)+
                    "; unresolved_type="+unresolved_word+
                    "; v3_string_id="+std::to_string(v3_id.value())+
                    "; v2_string_id="+std::to_string(v2_id.value())+
                    "; owner_identity="+std::to_string(owner_identity.value())+
                    "; lookup_scope="+std::to_string(out.lookup_scope)+
                    "; lookup_steps="+std::to_string(out.lookup_steps)+
                    "; owner_scope_chain="+identity_scope_chain(semantic.identities,owner_identity)+
                    "; type_begin="+std::to_string(decl.begin)+
                    "; type_end="+std::to_string(d.type_end);
                const auto src_index=token<root.origins.size()?token:origin_index;
                const auto type_origin=src_index<root.origins.size()?root.origins[src_index]:origin;
                totals.unresolved_name_rows.push_back({"unresolved_name",owner,symbol,"named_type",detail,type_origin});
            }
            if(out.result!=v316::verdict::match) {
                auto& samples=out.result==v316::verdict::mismatch?
                    totals.mismatch_samples:totals.unchecked_samples;
                const auto limit=out.result==v316::verdict::mismatch?5000u:1000u;
                if(samples.size()<limit)samples.push_back({std::string{v316::reason_name(out.why)},owner,symbol,
                    out.result==v316::verdict::not_checked?"not_checked":"mismatch",
                    "expected_base="+std::to_string(out.expected_base)+
                    "; actual_base="+std::to_string(out.actual_base)+
                    "; expected_layers="+std::to_string(out.expected_layer_count)+
                    "; actual_layers="+std::to_string(out.actual_layer_count)+
                    "; difference="+std::to_string(out.first_difference),origin});
            }
        }
    }
    // Prioritize genuine mismatches over NOT_CHECKED cases even if an early
    // 1,000 unresolved types filled their sample quota.
    totals.examples.reserve(totals.mismatch_samples.size()+totals.unchecked_samples.size());
    for(auto& item:totals.mismatch_samples)totals.examples.push_back(std::move(item));
    for(auto& item:totals.unchecked_samples)totals.examples.push_back(std::move(item));
    if(totals.checked!=totals.matched+totals.mismatched ||
       totals.fields_seen!=totals.checked+totals.not_checked+totals.graph_missing ||
       totals.unnamed_count!=totals.no_name.size() ||
       totals.operator_rows.size()!=[&]{std::uint64_t n=0;for(const auto& pair:totals.operator_categories)n+=pair.second;return n;}() ||
       (totals.not_checked_reasons.contains("unresolved_name")?
        totals.not_checked_reasons.at("unresolved_name"):0)!=totals.unresolved_name_rows.size())
        throw std::runtime_error{"V3-16 invalid canonical type accounting"};
    return totals;
}

// V3-17 constructor-target audit. V3 decodes operations from effective tokens;
// V2 Graph is consulted only AFTER decoding to compare normalized results.
// This does NOT prove equivalence of member default initializers or all G.
struct constructor_totals final {
    std::uint64_t shaped=0, parsed=0, declaration_only=0, failed=0;
    std::uint64_t raw_operations=0, normalized_targets=0, duplicate_writes=0;
    std::uint64_t checked=0, matched=0, mismatched=0, unchecked=0;
    std::uint64_t nested_targets=0, direct_targets=0;
    std::uint64_t v2_nested_defaults=0, v2_unseen_nested_defaults=0, v2_nonzero_member_constructions=0;
    std::map<std::string,std::uint64_t> causes;
    std::vector<semantic_detail_row> detail;
    // V3 expected direct construction, NOT copied from Header V2.
    std::vector<construction_value> expected_direct;
    std::vector<std::uint8_t> emitted_direct;
    std::vector<token_origin> direct_origins;
};
struct constructor_target final {
    identity_ref owner{};
    graph_range range{};
    std::unordered_map<std::string,std::size_t> members;
};
[[nodiscard]] constructor_totals audit_constructors(const world& input,
    const semantic_world& semantic) {
    constructor_totals totals;
    const auto& G=semantic.G;
    const auto types=G.type_entries();
    const auto ids=G.type_identity_entries();
    const auto members=G.member_entries();
    const auto initializers=G.member_construction_entries();
    totals.expected_direct.resize(members.size());
    totals.emitted_direct.resize(members.size());
    totals.direct_origins.resize(members.size());
    if (types.size()!=ids.size()||members.size()!=initializers.size())
        throw std::runtime_error{"V3-17 Graph member construction range mismatch"};
    std::unordered_map<std::string,std::vector<constructor_target>> by_name;
    for(std::size_t i=0;i<types.size();++i){
        if(types[i].kind!=graph_type_kind::record || !types[i].defined())continue;
        identity_record id;
        if(!semantic.identities.record(ids[i],id)||!id.name)
            throw std::runtime_error{"V3-17 bad Graph type identity"};
        constructor_target owner{ids[i],types[i].members,{}};
        if(owner.range.begin>members.size() || owner.range.count>members.size()-owner.range.begin)
            throw std::runtime_error{"V3-17 bad Graph type member range"};
        for(std::size_t j=owner.range.begin;j<static_cast<std::size_t>(owner.range.begin)+owner.range.count;++j){
            owner.members.emplace(std::string{semantic.strings.get(members[j].name)},j);
            totals.v2_nonzero_member_constructions+= initializers[j].kind!=construction_kind::zero;
        }
        by_name[std::string{semantic.strings.get(id.name)}].push_back(std::move(owner));
    }
    std::unordered_map<std::string,construction_value> nested_defaults;
    for(const auto& entry:G.constructor_defaults.entries()){
        ++totals.v2_nested_defaults;
        const std::string path{semantic.strings.get(entry.path)};
        const auto key=std::to_string(entry.owner.value())+":"+path;
        if(!nested_defaults.emplace(key,entry.value).second)
            throw std::runtime_error{"V3-17 duplicate authoritative constructor default"};
    }
    struct pending {
        v317::operation op;
        std::string owner_name;
        std::string target_path;
        token_origin origin{};
        const constructor_target* target=nullptr;
    };
    std::unordered_map<std::string,pending> latest;
    auto note=[&](std::string reason,const pending& op,std::string detail){
        ++totals.causes[reason];
        // Complete record of every unchecked/mismatched target, without sampling.
        totals.detail.push_back({std::move(reason),op.owner_name,op.target_path,
            "constructor_target",std::move(detail),op.origin});
    };
    syntax_result_03 model;
    reserve_shape(model,token_hint_plan(input));
    for(const auto& root:input.streams){
        if(!header_syntax_v3_06::scan(root.effective,model))
            throw std::runtime_error{"V3-17 syntax scan failed"};
        if(root.decoded_numbers.size()!=root.effective.size())
            throw std::runtime_error{"V3-17 numeric token bridge invalid"};
        std::vector<std::string> owners(model.records.size());
        for(std::size_t r=0;r<model.records.size();++r)
            if(!model.records[r].forward)owners[r]=record_name(root,model.records[r],input.strings);
        for(const auto& decl:model.declarations){
            // V3-06 includes root-level typedef declarations in this vector.
            // Those carry owner == 0xffffffffu by design. Only record-local
            // callable declarations can represent managed constructors.
            if(decl.kind!=syntax_decl_03::method_candidate &&
               decl.kind!=syntax_decl_03::ambiguous_callable)continue;
            if(decl.owner>=owners.size()){
                const auto origin=decl.begin<root.origins.size()?root.origins[decl.begin]:token_origin{};
                throw std::runtime_error{
                    "V3-17 invalid CALLABLE owner at file_id="+
                    std::to_string(origin.file)+
                    " source_offset="+std::to_string(origin.source_offset)+
                    " owner="+std::to_string(decl.owner)+
                    " records="+std::to_string(owners.size())};
            }
            const auto& name=owners[decl.owner];
            if(!constructor_shaped(root,decl,name,input.strings))continue;
            ++totals.shaped;
            const auto identifier=[&](std::size_t index)->std::uint32_t{
                return index<root.identifiers.size()?root.identifiers[index].value():0u;
            };
            const auto number=[&](std::size_t index)->v317::number{
                if(index>=root.decoded_numbers.size())return {};
                const auto n=root.decoded_numbers[index];
                return {n.bits,n.kind==prepared_number_kind_v2::unsigned_integer?
                    v317::number_kind::unsigned_integer:
                    n.kind==prepared_number_kind_v2::real?v317::number_kind::real:
                    v317::number_kind::none};
            };
            const auto decoded=v317::parse(root.effective,decl.begin,decl.end,identifier,number);
            if(!decoded.complete){
                ++totals.failed;
                ++totals.causes["syntax_"+decoded.reason];
                const auto origin=decl.begin<root.origins.size()?root.origins[decl.begin]:token_origin{};
                totals.detail.push_back({"constructor_not_checked",name,"","syntax_"+decoded.reason,
                    "begin="+std::to_string(decl.begin)+"; end="+std::to_string(decl.end),origin});
                continue;
            }
            ++totals.parsed;
            if(decoded.declaration_only)++totals.declaration_only;
            const auto it=by_name.find(name);
            const constructor_target* owner=it!=by_name.end()&&it->second.size()==1?&it->second.front():nullptr;
            for(const auto& op:decoded.operations){
                ++totals.raw_operations;
                std::string path{input.strings.spelling(op.target)};
                for(const auto& st:op.path){
                    if(st.array)path+="["+std::to_string(st.index)+"]";
                    else path+="."+std::string{input.strings.spelling(st.name)};
                }
                const auto origin=op.token<root.origins.size()?root.origins[op.token]:token_origin{};
                pending candidate{op,name,path,origin,owner};
                const std::string key=(owner?std::to_string(owner->owner.value()):"unmapped:"+name)+":"+path;
                if(!latest.emplace(key,candidate).second){
                    ++totals.duplicate_writes;
                    latest[key]=std::move(candidate); // final assignment wins
                }
            }
        }
    }
    totals.normalized_targets=latest.size();
    for(const auto& [key,p]:latest){
        const auto& op=p.op;
        if(op.path.empty())++totals.direct_targets;
        else ++totals.nested_targets;
        if(!p.target){
            ++totals.unchecked;note("ambiguous_owner",p,"No unique Graph record owner");continue;
        }
        construction_value computed{};
        if(op.kind==v317::operand_kind::member_reference){
            if(!op.path.empty()){
                ++totals.unchecked;note("nested_reference_operand",p,"Nested operation expected scalar");continue;
            }
            const auto target_name=std::string{input.strings.spelling(op.target)};
            const auto source_name=std::string{input.strings.spelling(op.reference)};
            const auto t=p.target->members.find(target_name), src=p.target->members.find(source_name);
            if(t==p.target->members.end()||src==p.target->members.end()){
                ++totals.unchecked;note("unmapped_reference",p,source_name);continue;
            }
            derived_type_record ref{};
            if(!G.derived(members[t->second].type,ref)||
               (ref.kind!=derived_type_kind::lvalue_reference&&ref.kind!=derived_type_kind::rvalue_reference)){
                ++totals.unchecked;note("non_reference_target",p,source_name);continue;
            }
            auto source_type=members[src->second].type;
            derived_type_record src_ref{};
            if(G.derived(source_type,src_ref)&&
               (src_ref.kind==derived_type_kind::lvalue_reference||src_ref.kind==derived_type_kind::rvalue_reference))
                source_type=src_ref.child;
            if(ref.child!=source_type){
                ++totals.unchecked;note("reference_type_mismatch",p,source_name);continue;
            }
            computed=construction_value::member_binding(static_cast<std::uint32_t>(src->second-p.target->range.begin+1));
        }else if(op.kind==v317::operand_kind::signed_integer)
            computed=construction_value::constant(construction_kind::signed_integer,op.bits);
        else if(op.kind==v317::operand_kind::unsigned_integer)
            computed=construction_value::constant(construction_kind::unsigned_integer,op.bits);
        else if(op.kind==v317::operand_kind::real)
            computed=construction_value::constant(construction_kind::real,op.bits);
        if(!op.path.empty()){
            const auto actual=nested_defaults.find(key);
            if(actual==nested_defaults.end()){
                ++totals.unchecked;note("nested_default_absent",p,"No matching Graph nested default");continue;
            }
            ++totals.checked;
            if(actual->second==computed)++totals.matched;
            else {
                ++totals.mismatched;
                note("nested_value_mismatch",p,"expected_kind="+std::to_string(static_cast<int>(computed.kind))+
                    ";actual_kind="+std::to_string(static_cast<int>(actual->second.kind)));
            }
        } else {
            const auto name=std::string{input.strings.spelling(op.target)};
            const auto index=p.target->members.find(name);
            if(index==p.target->members.end()){
                ++totals.unchecked;note("field_absent",p,"No Graph member");continue;
            }
            totals.expected_direct[index->second]=computed;
            totals.emitted_direct[index->second]=1;
            totals.direct_origins[index->second]=p.origin;
            ++totals.checked;
            if(initializers[index->second]==computed)++totals.matched;
            else {
                ++totals.mismatched;
                note("member_value_mismatch",p,"expected_kind="+std::to_string(static_cast<int>(computed.kind))+
                    ";actual_kind="+std::to_string(static_cast<int>(initializers[index->second].kind))+
                    ";expected_bits="+std::to_string(computed.bits())+
                    ";actual_bits="+std::to_string(initializers[index->second].bits()));
            }
        }
    }
    for(const auto& [key,value]:nested_defaults){
        (void)value;
        if(latest.find(key)==latest.end()){
            ++totals.v2_unseen_nested_defaults;
            ++totals.causes["v2_nested_not_emitted"];
            totals.detail.push_back({"v2_nested_not_emitted","",key,
                "graph_only_nested_default","V3 decoded no matching constructor operation",token_origin{}});
        }
    }
    if(totals.checked!=totals.matched+totals.mismatched ||
       totals.normalized_targets!=totals.checked+totals.unchecked ||
       totals.raw_operations<totals.normalized_targets ||
       totals.shaped!=totals.parsed+totals.failed)
        throw std::runtime_error{"V3-17 constructor accounting invalid"};
    return totals;
}


// V3-18: independent field-initializer decode + bidirectional coverage of every
// V2 member construction slot. Constructor expectations came from V3-17 tokens.
struct construction_totals final {
    std::uint64_t graph_members=0, fields=0, field_explicit=0, field_implicit=0;
    std::uint64_t field_failed=0, duplicate_fields=0, missing_fields=0;
    std::uint64_t constructor_overrides=0;
    std::uint64_t compared=0, matched=0, mismatched=0, not_checked=0;
    std::uint64_t v2_nonzero=0, v3_nonzero=0;
    std::uint64_t nested_compared=0, nested_mismatched=0, nested_not_checked=0;
    std::map<std::string,std::uint64_t> causes;
    std::vector<semantic_detail_row> details;
};
[[nodiscard]] construction_totals audit_full_construction(const world& input,
    const semantic_world& semantic,const constructor_totals& ctor) {
    construction_totals totals;
    const auto& G=semantic.G;
    const auto types=G.type_entries();
    const auto type_ids=G.type_identity_entries();
    const auto members=G.member_entries();
    const auto actual=G.member_construction_entries();
    if(actual.size()!=members.size() || types.size()!=type_ids.size() ||
       ctor.expected_direct.size()!=members.size() || ctor.emitted_direct.size()!=members.size())
        throw std::runtime_error{"V3-18 invalid Graph/constructor construction layout"};
    totals.graph_members=members.size();
    totals.v2_nonzero=ctor.v2_nonzero_member_constructions;
    struct field_owner {
        identity_ref who{};
        std::uint32_t begin=0;
        std::unordered_map<std::string,std::size_t> indices;
    };
    std::unordered_map<std::string,std::vector<field_owner>> graph_owners;
    for(std::size_t i=0;i<types.size();++i){
        if(types[i].kind!=graph_type_kind::record || !types[i].defined())continue;
        identity_record id;
        if(!semantic.identities.record(type_ids[i],id) || !id.name)
            throw std::runtime_error{"V3-18 invalid Graph record identity"};
        const auto rg=types[i].members;
        if(rg.begin>members.size() || rg.count>members.size()-rg.begin)
            throw std::runtime_error{"V3-18 invalid Graph member range"};
        field_owner owner{type_ids[i],rg.begin,{}};
        for(std::size_t j=rg.begin;j<static_cast<std::size_t>(rg.begin)+rg.count;++j)
            if(!owner.indices.emplace(std::string{semantic.strings.get(members[j].name)},j).second)
                throw std::runtime_error{"V3-18 duplicate Graph member name"};
        graph_owners[std::string{semantic.strings.get(id.name)}].push_back(std::move(owner));
    }
    // Dense V3 expectation is benchmark-local only. It is never persisted.
    std::vector<construction_value> expected(members.size());
    std::vector<std::uint8_t> visited(members.size(),0), unknown(members.size(),0);
    std::vector<token_origin> origins(members.size());
    std::vector<std::string> owners(members.size());
    auto note=[&](std::string reason,std::size_t index,const token_origin& origin,
                  std::string detail){
        ++totals.causes[reason];
        totals.details.push_back({reason,index<owners.size()?owners[index]:"",
            index<members.size()?std::string{semantic.strings.get(members[index].name)}:"",
            "construction",std::move(detail),origin});
    };
    syntax_result_03 model;
    reserve_shape(model,token_hint_plan(input));
    for(const auto& root:input.streams){
        if(!header_syntax_v3_06::scan(root.effective,model))
            throw std::runtime_error{"V3-18 syntax scan failed"};
        std::vector<const field_owner*> found(model.records.size(),nullptr);
        std::vector<std::string> name(model.records.size());
        for(std::size_t r=0;r<model.records.size();++r){
            if(model.records[r].forward)continue;
            name[r]=record_name(root,model.records[r],input.strings);
            auto it=graph_owners.find(name[r]);
            if(it!=graph_owners.end() && it->second.size()==1)found[r]=&it->second.front();
        }
        for(const auto& d:model.declarators){
            if(d.declaration>=model.declarations.size())throw std::runtime_error{"V3-18 invalid declaration index"};
            const auto& decl=model.declarations[d.declaration];
            if(decl.owner>=found.size())throw std::runtime_error{"V3-18 invalid field owner index"};
            if(decl.kind!=syntax_decl_03::field_candidate &&
               !v312::parenthesized_array_field(root.effective,decl,d))continue;
            ++totals.fields;
            const auto pos=d.name<root.origins.size()?d.name:decl.begin;
            const auto origin=pos<root.origins.size()?root.origins[pos]:token_origin{};
            const auto* owner=found[decl.owner];
            const std::string symbol=d.name<root.identifiers.size()?
                std::string{input.strings.get(root.identifiers[d.name])}:"";
            if(!owner || symbol.empty() || !owner->indices.contains(symbol)){
                ++totals.missing_fields;
                ++totals.field_failed;
                ++totals.causes["field_unmapped"];
                totals.details.push_back({"field_unmapped",name[decl.owner],symbol,"construction",
                    "V3 field not mapped to a unique Graph member",origin});
                continue;
            }
            const std::size_t idx=owner->indices.at(symbol);
            owners[idx]=name[decl.owner];
            origins[idx]=origin;
            if(visited[idx]++){
                ++totals.duplicate_fields;
                ++totals.field_failed;
                note("duplicate_v3_field",idx,origin,"Field declared twice");
                unknown[idx]=1;continue;
            }
            const auto identifier=[&](std::size_t at)->std::uint32_t{
                return at<root.identifiers.size()?root.identifiers[at].value():0;
            };
            const auto number=[&](std::size_t at)->v317::number{
                if(at>=root.decoded_numbers.size())return {};
                const auto n=root.decoded_numbers[at];
                return {n.bits,n.kind==prepared_number_kind_v2::unsigned_integer?
                    v317::number_kind::unsigned_integer:
                    n.kind==prepared_number_kind_v2::real?v317::number_kind::real:v317::number_kind::none};
            };
            const auto decoded=v318::parse_field(root.effective,d.initializer_begin,decl.end,identifier,number);
            if(!decoded.complete){
                ++totals.field_failed;unknown[idx]=1;
                note("field_initializer_"+decoded.reason,idx,origin,
                    "initializer_begin="+std::to_string(d.initializer_begin)+
                    ";declaration_end="+std::to_string(decl.end));
                continue;
            }
            if(!decoded.explicit_initializer){++totals.field_implicit;continue;}
            ++totals.field_explicit;
            const auto& op=decoded.value;
            construction_value value{};
            if(op.kind==v317::operand_kind::member_reference){
                const std::string source{input.strings.spelling(op.reference)};
                auto it=owner->indices.find(source);
                if(it==owner->indices.end()){
                    unknown[idx]=1;note("field_reference_unmapped",idx,origin,"source="+source);continue;
                }
                derived_type_record target_ref{},source_ref{};
                if(!G.derived(members[idx].type,target_ref) ||
                   (target_ref.kind!=derived_type_kind::lvalue_reference &&
                    target_ref.kind!=derived_type_kind::rvalue_reference)){
                    unknown[idx]=1;note("field_reference_target_not_reference",idx,origin,"source="+source);continue;
                }
                auto source_type=members[it->second].type;
                if(G.derived(source_type,source_ref) &&
                   (source_ref.kind==derived_type_kind::lvalue_reference ||
                    source_ref.kind==derived_type_kind::rvalue_reference))source_type=source_ref.child;
                if(source_type!=target_ref.child){
                    unknown[idx]=1;note("field_reference_type_mismatch",idx,origin,"source="+source);continue;
                }
                value=construction_value::member_binding(static_cast<std::uint32_t>(it->second-owner->begin+1));
            } else if(op.kind==v317::operand_kind::signed_integer)
                value=construction_value::constant(construction_kind::signed_integer,op.bits);
            else if(op.kind==v317::operand_kind::unsigned_integer)
                value=construction_value::constant(construction_kind::unsigned_integer,op.bits);
            else if(op.kind==v317::operand_kind::real)
                value=construction_value::constant(construction_kind::real,op.bits);
            expected[idx]=value;
        }
    }
    for(std::size_t idx=0;idx<members.size();++idx){
        if(!visited[idx]){
            ++totals.missing_fields;note("v2_field_not_emitted",idx,{},"No matching V3 field declarator");
            unknown[idx]=1;
        }
        if(ctor.emitted_direct[idx]){
            ++totals.constructor_overrides;
            expected[idx]=ctor.expected_direct[idx];
            origins[idx]=ctor.direct_origins[idx];
        }
        if(unknown[idx]){++totals.not_checked;continue;}
        ++totals.compared;
        if(expected[idx].kind!=construction_kind::zero)++totals.v3_nonzero;
        if(expected[idx]==actual[idx])++totals.matched;
        else{
            ++totals.mismatched;
            note("member_construction_mismatch",idx,origins[idx],
                "expected_kind="+std::to_string(static_cast<unsigned>(expected[idx].kind))+
                ";actual_kind="+std::to_string(static_cast<unsigned>(actual[idx].kind))+
                ";expected_bits="+std::to_string(expected[idx].bits())+
                ";actual_bits="+std::to_string(actual[idx].bits())+
                ";expected_operand="+std::to_string(expected[idx].operand)+
                ";actual_operand="+std::to_string(actual[idx].operand));
        }
    }
    totals.nested_compared=ctor.v2_nested_defaults;
    for(const auto& row:ctor.detail){
        if(row.reason=="v2_nested_not_emitted" || row.reason=="nested_value_mismatch") {
            ++totals.nested_mismatched;
            totals.details.push_back(row);
        }else if(row.reason=="nested_default_absent" || row.reason=="nested_reference_operand"){
            ++totals.nested_not_checked;
            totals.details.push_back(row);
        }
    }
    if(totals.fields!=totals.field_explicit+totals.field_implicit+totals.field_failed ||
       totals.compared!=totals.matched+totals.mismatched ||
       totals.graph_members!=totals.compared+totals.not_checked)
        throw std::runtime_error{"V3-18 construction accounting invalid"};
    return totals;
}

void write_semantic_rows(const fs::path& path,const std::vector<semantic_detail_row>& rows) {
    if(fs::exists(path))throw std::runtime_error{"Report must be new: "+path.string()};
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::out|std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error{"Cannot open V3-13 audit CSV: "+path.string()};
    out<<"reason,file_id,source_offset,source_length,record,member,category,detail\n";
    for(const auto& row:rows)out<<escape_csv(row.reason)<<','<<row.origin.file<<','<<
        row.origin.source_offset<<','<<row.origin.source_length<<','<<
        escape_csv(row.owner)<<','<<escape_csv(row.symbol)<<','<<
        escape_csv(row.category)<<','<<escape_csv(row.detail)<<'\n';
    out.flush();
    if(!out)throw std::runtime_error{"V3-13 CSV write failed: "+path.string()};
}

void save_report(const std::filesystem::path& path,const classifier_result& result) {
    // Caller is required to preflight path nonexistence. Direct-create again
    // with std::ofstream only after this check; no Project artifacts touched.
    if(std::filesystem::exists(path))
        throw std::runtime_error{"Mismatch report already exists"};
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path,std::ios::binary|std::ios::out|std::ios::trunc);
    if(!f)throw std::runtime_error{"Cannot create classification mismatch report"};
    f<<"reason,file_id,source_offset,source_length,record,member,detail\n";
    for(const auto& d:result.examples) {
        f<<escape_csv(d.reason)<<','<<d.file<<','<<d.offset<<','<<d.length<<','
         <<escape_csv(d.owner)<<','<<escape_csv(d.symbol)<<','<<escape_csv(d.detail)<<'\n';
    }
    f.flush();
    if(!f)throw std::runtime_error{"Cannot write classification mismatch report"};
}

void save_missing_report(const std::filesystem::path& path,
                         const classifier_result& result) {
    if(std::filesystem::exists(path))
        throw std::runtime_error{"Missing-member report already exists"};
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path,std::ios::binary|std::ios::out|std::ios::trunc);
    if(!f)throw std::runtime_error{"Cannot create missing-member report"};
    f<<"stage,reason,file_id,source_offset,source_length,record,member,detail\n";
    for(const auto& row:result.all_missing) {
        f<<escape_csv(row.stage)<<','<<escape_csv(row.reason)<<','
         <<row.origin.file<<','<<row.origin.source_offset<<','
         <<row.origin.source_length<<','<<escape_csv(row.owner)<<','
         <<escape_csv(row.symbol)<<','<<escape_csv(row.detail)<<'\n';
    }
    f.flush();
    if(!f)throw std::runtime_error{"Cannot write missing-member report"};
}

void save_base_pairs(const fs::path& path,const canonical_totals& totals) {
    if(fs::exists(path))throw std::runtime_error{"Base-pairs report must be new"};
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary|std::ios::out|std::ios::trunc);
    if(!out)throw std::runtime_error{"Cannot open base-pairs report"};
    out<<"expected_base,actual_base,count\n";
    std::uint64_t count=0;
    for(const auto& [pair,n]:totals.full_base_pairs) {
        out<<pair.first<<','<<pair.second<<','<<n<<'\n';
        count+=n;
    }
    if(count!=(totals.mismatch_reasons.contains("base_mismatch")?
        totals.mismatch_reasons.at("base_mismatch"):0))
        throw std::runtime_error{"Base-pair accounting mismatch"};
    out.flush();if(!out)throw std::runtime_error{"Base-pair CSV write failed"};
}
#include "v3_21_graph_producer.inc"

} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=16) {
            std::cerr<<"Usage: ServerEngineV4HeaderSyntaxV316 <project.json> <new-unresolved.csv> <new-callable.csv> <new-type-examples.csv> <new-canonical-examples.csv> <new-no-name.csv> <new-unresolved-type.csv> <new-base-pairs.csv> <new-operators.csv> <new-constructor-details.csv> <new-constructor-summary.csv>\n";
            return 2;
        }
        for(int i=2;i<16;++i) {
            if(fs::exists(argv[i]))throw std::runtime_error{"Result must be new: "+std::string{argv[i]}};
            for(int j=2;j<i;++j)if(fs::absolute(argv[i])==fs::absolute(argv[j]))
                throw std::runtime_error{"V3-16 output paths must differ"};
        }
        world input;
        double compose=0,prepare=0,decode=0,capture=0;
        std::uint64_t origin_checksum=0;
        build_inputs(input,fs::path{argv[1]},compose,prepare,decode,capture,origin_checksum);
        const auto corpus=observe_corpus(input);
        if(!corpus.effective_tokens)throw std::runtime_error{"Effective corpus is empty"};
        const auto parity=effective_parity(input);
        const auto bridge=inspect_bridge(input);
        if(parity.rejected||parity.tokens!=corpus.effective_tokens||
           bridge.invalid_indices||bridge.invalid_names||bridge.declarators!=parity.declarators)
            throw std::runtime_error{"V3-16 descriptor parity / canonical bridge failure"};
        // Time the standalone V3 Graph producer BEFORE invoking the V2 oracle.
        v319::producer_state produced{input.strings};
        const auto v319_started=clock_type::now();
        const bool independent_complete=v319::emit(produced,input);
        const auto v319_ms=elapsed_ms(v319_started);
        semantic_world semantic;
        run_header_v2_semantic(input,semantic);
        const auto v319_compare=v319::compare(produced,input.strings,semantic);
        // The Header-only gate can pass, but it MUST NOT be advertised as
        // full PUBLISH Graph parity: Source/Assign/Object/Link stages are absent.
        const bool header_graph_pass=independent_complete && produced.unsupported==0 &&
            produced.G.type_count()==semantic.G.type_count() &&
            produced.G.member_count()==semantic.G.member_count() &&
            produced.G.derived_type_count()==semantic.G.derived_type_count() &&
            produced.G.constructor_defaults.entries().size()==
                semantic.G.constructor_defaults.entries().size() &&
            v319_compare.type_missing==0 && v319_compare.extra_types==0 &&
            v319_compare.member_mismatch==0 && v319_compare.construction_mismatch==0 &&
            v319_compare.base_mismatch==0 && v319_compare.polymorphic_mismatch==0 &&
            v319_compare.nested_mismatch==0 &&
            v319_compare.identity_mismatch==0 &&
            v319_compare.type_where_mismatch==0 &&
            v319_compare.member_where_mismatch==0 &&
            v319_compare.derived_where_mismatch==0 &&
            v319_compare.derived_extra_v3==0 && v319_compare.derived_extra_v2==0 &&
            v319_compare.member_matches==semantic.G.member_count();
        constexpr bool complete_graph_gate=false;
        {
            const fs::path path=argv[14];
            if(!path.parent_path().empty())fs::create_directories(path.parent_path());
            std::ofstream f(path,std::ios::binary|std::ios::out|std::ios::trunc);
            if(!f)throw std::runtime_error{"V3-19 graph summary open failed"};
            f<<"mode,v3_producer_ms,v2_reference_ms,v3_records,v3_aliases,v3_fields,v3_constructors,v3_direct_writes,v3_nested_writes,method_return_declarations,method_return_derived_created,method_no_return,v3_types,v2_types,v3_members,v2_members,producer_unchecked,type_matches,type_missing,extra_types,member_matches,member_mismatch,construction_matches,construction_mismatch,base_matches,base_mismatch,polymorphic_mismatch,nested_matches,nested_mismatch,v3_derived,v2_derived,identity_mismatch,type_where_mismatch,member_where_mismatch,derived_where_mismatch,derived_extra_v3,derived_extra_v2,header_graph_gate,full_graph_gate\n";
            f<<"v321,"<<v319_ms<<','<<semantic.metrics.v2_parse_ms<<','
             <<produced.records<<','<<produced.aliases<<','<<produced.fields<<','
             <<produced.constructors<<','<<produced.direct<<','<<produced.nested<<','
             <<produced.method_returns<<','<<produced.method_derived_created<<','<<produced.method_no_return<<','
             <<produced.G.type_count()<<','<<semantic.G.type_count()<<','
             <<produced.G.member_count()<<','<<semantic.G.member_count()<<','
             <<produced.unsupported<<','<<v319_compare.type_matches<<','
             <<v319_compare.type_missing<<','<<v319_compare.extra_types<<','
             <<v319_compare.member_matches<<','
             <<v319_compare.member_mismatch<<','<<v319_compare.construction_matches<<','
             <<v319_compare.construction_mismatch<<','<<v319_compare.base_matches<<','
             <<v319_compare.base_mismatch<<','
             <<v319_compare.polymorphic_mismatch<<','<<v319_compare.nested_matches<<','
             <<v319_compare.nested_mismatch<<','<<produced.G.derived_type_count()<<','
             <<semantic.G.derived_type_count()<<','
             <<v319_compare.identity_mismatch<<','
             <<v319_compare.type_where_mismatch<<','
             <<v319_compare.member_where_mismatch<<','
             <<v319_compare.derived_where_mismatch<<','
             <<v319_compare.derived_extra_v3<<','<<v319_compare.derived_extra_v2<<','
             <<(header_graph_pass?"HEADER_GRAPH_PARITY_PASS":"PARTIAL")<<','
             <<(complete_graph_gate?"FULL_GRAPH_PARITY_PASS":"PARTIAL")<<'\n';
        }
        {
            const fs::path path=argv[15];
            if(!path.parent_path().empty())fs::create_directories(path.parent_path());
            std::ofstream f(path,std::ios::binary|std::ios::out|std::ios::trunc);
            if(!f)throw std::runtime_error{"V3-19 graph details open failed"};
            f<<"reason,file_id,source_offset,source_length,owner,symbol,detail\n";
            for(const auto& item:produced.failures)
                f<<escape_csv(item.reason)<<','<<item.file<<','<<item.offset<<','
                 <<item.length<<','<<escape_csv(item.owner)<<','
                 <<escape_csv(item.symbol)<<','<<escape_csv(item.detail)<<'\n';
            for(const auto& item:v319_compare.locations)
                f<<escape_csv(item.reason)<<','<<item.file<<','<<item.offset<<','
                 <<item.length<<','<<escape_csv(item.owner)<<','
                 <<escape_csv(item.symbol)<<','<<escape_csv(item.detail)<<'\n';
            for(const auto& item:v319_compare.differences)
                f<<escape_csv("graph_difference")<<",0,0,0,"","","
                 <<escape_csv(item)<<'\n';
        }
        std::cout<<"V3-21 independent_graph_producer="<<(independent_complete?"COMPLETE":"PARTIAL")
                 <<" full_graph_parity=PARTIAL v3_graph_ms="<<v319_ms
                 <<" v3_types="<<produced.G.type_count()<<" v3_members="<<produced.G.member_count()
                 <<" v2_types="<<semantic.G.type_count()<<" v2_members="<<semantic.G.member_count()
                 <<" producer_unchecked="<<produced.unsupported
                 <<" header_graph_parity="<<(header_graph_pass?"HEADER_GRAPH_PARITY_PASS":"PARTIAL")
                 <<'\n';
        const auto base=audit_graph_fields(input,semantic);
        const auto& b=base.totals;
        const bool fields_pass=b.unknown_records==0&&b.ambiguous_records==0&&
            b.record_kind_mismatches==0&&b.unmatched_fields==0&&
            b.unmatched_graph_members==0&&b.duplicate_v3_members==0&&
            b.unnamed_fields==0&&b.invalid_names==0&&b.invalid_owners==0&&
            b.graph_record_unmapped==0;
        const auto start=clock_type::now();
        const auto audit=audit_semantic_details(input,semantic);
        const auto elapsed=elapsed_ms(start);
        const auto& n=audit.totals;
        if(n.checked_graph_fields!=b.matched_fields ||
            n.missing_graph_fields!=b.unmatched_fields ||
            n.unresolved!=b.unresolved || n.callable_graph_collisions!=b.callable_graph_collisions ||
            n.callable!=b.callable)
            throw std::runtime_error{"V3-13/V3-12 audit counts differ"};
        const auto cstart=clock_type::now();
        const auto canonical=audit_canonical_types(input,semantic);
        const auto canonical_ms=elapsed_ms(cstart);
        if(canonical.fields_seen!=b.v3_fields || canonical.graph_missing!=0 ||
           canonical.no_name.size()!=canonical.unnamed_count ||
           canonical.constructor_without_type!=(audit.unresolved_reasons.contains("constructor_without_type")?
               audit.unresolved_reasons.at("constructor_without_type"):0) ||
           canonical.unnamed_count+canonical.operator_rows.size()!=(audit.unresolved_reasons.contains("no_name")?
               audit.unresolved_reasons.at("no_name"):0))
            throw std::runtime_error{"V3-16 field coverage / no-name accounting failure"};
        write_semantic_rows(argv[5],canonical.examples);
        write_semantic_rows(argv[6],canonical.no_name);
        write_semantic_rows(argv[7],canonical.unresolved_name_rows);
        save_base_pairs(argv[8],canonical);
        write_semantic_rows(argv[9],canonical.operator_rows);
        const auto ctor_start=clock_type::now();
        const auto constructors=audit_constructors(input,semantic);
        const auto ctor_ms=elapsed_ms(ctor_start);
        const auto construction_start=clock_type::now();
        const auto construction=audit_full_construction(input,semantic,constructors);
        const auto construction_ms=elapsed_ms(construction_start);
        write_semantic_rows(argv[12],construction.details);
        {
            const fs::path path=argv[13];
            if(fs::exists(path))throw std::runtime_error{"V3-18 construction summary must be new"};
            if(!path.parent_path().empty())fs::create_directories(path.parent_path());
            std::ofstream f(path,std::ios::binary|std::ios::out|std::ios::trunc);
            if(!f)throw std::runtime_error{"V3-18 construction summary open failed"};
            f<<"mode,graph_members,v3_fields,explicit_initializers,implicit_initializers,failed_initializers,duplicate_fields,missing_fields,constructor_overrides,compared,matched,mismatched,not_checked,v2_nonzero,v3_nonzero,nested_v2,nested_mismatched,nested_not_checked,construction_ms,gate\n";
            const bool pass=fields_pass && canonical.fields_seen==construction.graph_members &&
                canonical.mismatched==0 && canonical.not_checked==0 && canonical.graph_missing==0 &&
                construction.matched==construction.graph_members &&
                construction.mismatched==0 && construction.not_checked==0 &&
                construction.missing_fields==0 && construction.field_failed==0 &&
                construction.duplicate_fields==0 && constructors.mismatched==0 &&
                constructors.unchecked==0 && constructors.failed==0 &&
                construction.nested_mismatched==0 && construction.nested_not_checked==0;
            f<<"v318,"<<construction.graph_members<<','<<construction.fields<<','<<construction.field_explicit<<','
             <<construction.field_implicit<<','<<construction.field_failed<<','<<construction.duplicate_fields<<','
             <<construction.missing_fields<<','<<construction.constructor_overrides<<','<<construction.compared<<','
             <<construction.matched<<','<<construction.mismatched<<','<<construction.not_checked<<','
             <<construction.v2_nonzero<<','<<construction.v3_nonzero<<','<<construction.nested_compared<<','
             <<construction.nested_mismatched<<','<<construction.nested_not_checked<<','<<construction_ms<<','
             <<(pass?"FULL_CONSTRUCTION_PARITY_PASS":"PARTIAL")<<'\n';
            if(!f)throw std::runtime_error{"V3-18 construction summary write failed"};
        }
        write_semantic_rows(argv[10],constructors.detail);
        {
            const fs::path p=argv[11];
            if(fs::exists(p))throw std::runtime_error{"Constructor summary must be new"};
            if(!p.parent_path().empty())fs::create_directories(p.parent_path());
            std::ofstream f(p,std::ios::binary|std::ios::out|std::ios::trunc);
            if(!f)throw std::runtime_error{"V3-17 cannot open constructor summary"};
            f<<"mode,constructor_shaped,constructor_parsed,constructor_failed,declaration_only,raw_operations,normalized_targets,duplicate_writes,checked,matched,mismatched,not_checked,direct_targets,nested_targets,v2_nested_defaults,v2_unseen_nested_defaults,v2_nonzero_member_constructions,constructor_ms,constructor_graph_gate\n";
            f<<"v317,"<<constructors.shaped<<','<<constructors.parsed<<','<<constructors.failed<<','<<constructors.declaration_only<<','
             <<constructors.raw_operations<<','<<constructors.normalized_targets<<','<<constructors.duplicate_writes<<','
             <<constructors.checked<<','<<constructors.matched<<','<<constructors.mismatched<<','<<constructors.unchecked<<','
             <<constructors.direct_targets<<','<<constructors.nested_targets<<','<<constructors.v2_nested_defaults<<','<<constructors.v2_unseen_nested_defaults<<','
             <<constructors.v2_nonzero_member_constructions<<','<<ctor_ms<<",NOT_IMPLEMENTED\n";
            if(!f)throw std::runtime_error{"V3-17 constructor summary write failed"};
        }
        write_semantic_rows(argv[2],audit.unresolved_rows);
        write_semantic_rows(argv[3],audit.collision_rows);
        write_semantic_rows(argv[4],audit.type_examples);
        std::cout<<"effective_parity=PASS shadow_bridge=PASS header_v2_graph=PASS field_identity="
                 <<(fields_pass?"PASS":"GAPS")
                 <<" semantic_type_parity="<<(fields_pass && canonical.graph_missing==0 && canonical.not_checked==0 && canonical.mismatched==0?"FULL_FIELD_TYPE_PASS":"PARTIAL")
                 <<" operator_classification="<<(canonical.unnamed_count==0?"PASS":"PARTIAL")
                 <<" constructor_parity=PARTIAL"
                 <<" constructor_target_parity="<<(constructors.mismatched==0 && constructors.unchecked==0 && constructors.failed==0 && constructors.v2_unseen_nested_defaults==0?"PASS":"PARTIAL")
                 <<" construction_parity="<<(fields_pass && canonical.fields_seen==construction.graph_members &&
                    canonical.mismatched==0 && canonical.not_checked==0 && canonical.graph_missing==0 &&
                    construction.matched==construction.graph_members &&
                    construction.mismatched==0 && construction.not_checked==0 &&
                    construction.missing_fields==0 && construction.field_failed==0 &&
                    construction.duplicate_fields==0 && constructors.mismatched==0 &&
                    constructors.unchecked==0 && constructors.failed==0 &&
                    construction.nested_mismatched==0 && construction.nested_not_checked==0?
                    "FULL_CONSTRUCTION_PARITY_PASS":"PARTIAL")<<"\n";
        std::cout<<"mode,effective_tokens,graph_members,matched_fields,unresolved,unresolved_fields,unresolved_callable,unresolved_other,callable,constructor_shape,method_body_shape,other_callable_shape,callable_graph_collisions,shape_checks,shape_matches,shape_mismatches,builtin_checks,builtin_matches,builtin_mismatches,builtin_not_checked,invalid_type_chains,type_examples,semantic_audit_ms,semantic_graph_gate\n";
        std::cout<<std::fixed<<std::setprecision(6)
                 <<"v313,"<<parity.tokens<<','<<b.graph_members<<','<<n.checked_graph_fields<<','
                 <<n.unresolved<<','<<n.unresolved_fields<<','<<n.unresolved_callable<<','<<n.unresolved_other<<','
                 <<n.callable<<','<<n.constructor_shape<<','<<n.method_body_shape<<','<<n.other_callable_shape<<','
                 <<n.callable_graph_collisions<<','<<n.derived_shape_checks<<','<<n.derived_shape_matches<<','
                 <<n.derived_shape_mismatches<<','<<n.builtin_checks<<','<<n.builtin_matches<<','
                 <<n.builtin_mismatches<<','<<n.builtin_not_checked<<','<<n.invalid_graph_type_chain<<','
                 <<audit.type_examples.size()<<','<<elapsed<<",NOT_IMPLEMENTED\n";
        std::cout<<"mode,full_type_fields,full_type_checked,full_type_matches,full_type_mismatches,full_type_not_checked,missing_fields,no_name,operators,canonical_samples,canonical_ms,semantic_graph_gate\n";
        std::cout<<std::fixed<<std::setprecision(6)<<"v316,"<<canonical.fields_seen<<','<<canonical.checked<<','<<canonical.matched<<','<<canonical.mismatched<<','<<canonical.not_checked<<','<<canonical.graph_missing<<','<<canonical.unnamed_count<<','<<canonical.operator_rows.size()<<','<<canonical.examples.size()<<','<<canonical_ms<<",NOT_IMPLEMENTED\n";
        std::cout<<"constructor_shaped="<<constructors.shaped
                 <<" parsed="<<constructors.parsed<<" incomplete="<<constructors.failed
                 <<" operations="<<constructors.raw_operations<<" normalized="<<constructors.normalized_targets
                 <<" graph_only_nested="<<constructors.v2_unseen_nested_defaults
                 <<" checked="<<constructors.checked<<" matches="<<constructors.matched
                 <<" mismatches="<<constructors.mismatched<<" not_checked="<<constructors.unchecked<<'\n';
        for(const auto& [reason,count]:constructors.causes)
            std::cout<<"constructor_reason="<<reason<<" count="<<count<<'\n';
        std::cout<<"construction_fields="<<construction.fields<<" explicit="<<construction.field_explicit
                 <<" implicit="<<construction.field_implicit<<" failed="<<construction.field_failed
                 <<" compared="<<construction.compared<<" matched="<<construction.matched
                 <<" mismatched="<<construction.mismatched<<" not_checked="<<construction.not_checked
                 <<" graph_only_direct="<<construction.missing_fields
                 <<" nested_mismatched="<<construction.nested_mismatched<<'\n';
        for(const auto& [reason,count]:construction.causes)
            std::cout<<"construction_reason="<<reason<<" count="<<count<<'\n';
        std::cout<<"constructor_without_type="<<canonical.constructor_without_type<<'\n';
        std::cout<<"no_name_actual="<<canonical.unnamed_count<<'\n';
        std::cout<<"operator_declarations="<<canonical.operator_rows.size()<<'\n';
        for(const auto& [kind,count]:canonical.operator_categories)
            std::cout<<"operator_form="<<kind<<" count="<<count<<'\n';
        for(const auto& [kind,count]:canonical.no_name_categories)
            std::cout<<"no_name_category="<<kind<<" count="<<count<<'\n';
        std::cout<<"base_pair_distinct="<<canonical.full_base_pairs.size()<<'\n';
        std::cout<<"unresolved_name_report_rows="<<canonical.unresolved_name_rows.size()<<'\n';
        for(const auto& [reason,count]:canonical.not_checked_reasons)
            std::cout<<"type_not_checked_reason="<<reason<<" count="<<count<<'\n';
        for(const auto& [reason,count]:canonical.mismatch_reasons)
            std::cout<<"type_mismatch_reason="<<reason<<" count="<<count<<'\n';
        for(const auto& [reason,count]:audit.unresolved_reasons)
            std::cout<<"unresolved_reason="<<reason<<" count="<<count<<'\n';
        for(const auto& [reason,count]:audit.collision_reasons)
            std::cout<<"callable_collision_shape="<<reason<<" count="<<count<<'\n';
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"HEADER-SYNTAX-V3-18 FAIL: "<<e.what()<<'\n';
        return 1;
    }
}
