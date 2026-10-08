#include "preprocessor_v2.hpp"

#include "../file/file_context.hpp"
#include "../string/string_table.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] constexpr bool directive_start_v2(
    token_kind kind) noexcept {

    switch (kind) {
    case token_kind::pp_include:
    case token_kind::pp_define:
    case token_kind::pp_undef:
    case token_kind::pp_if:
    case token_kind::pp_ifdef:
    case token_kind::pp_ifndef:
    case token_kind::pp_elif:
    case token_kind::pp_else:
    case token_kind::pp_endif:
    case token_kind::pp_line:
    case token_kind::pp_error:
    case token_kind::pp_pragma:
    case token_kind::pp_unknown:
        return true;

    default:
        return false;
    }
}

}

semantic_preprocessor_v2::semantic_preprocessor_v2(
    file_context& files_value,
    prepared_include_view_v2 prepared_value,
    const preprocessor_configuration& configuration_value,
    string_table& strings_value,
    preprocessor_v2_failure* failure_value) noexcept
    : files(files_value),
      prepared(prepared_value),
      configuration(configuration_value),
      strings(strings_value),
      preprocessing(strings_value),
      failure(failure_value) {
}

bool semantic_preprocessor_v2::contextual_identifier(
    std::string_view spelling) const noexcept {

    return current_valid &&
        current_value.kind == token_kind::identifier &&
        current_value.identifier &&
        strings.get(current_value.identifier) == spelling;
}

std::uint32_t semantic_preprocessor_v2::file_hash(
    std::uint32_t value) noexcept {

    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;

    return value == 0
        ? 1
        : value;
}

server_status semantic_preprocessor_v2::fail(
    preprocessor_v2_failure_kind kind,
    file_id file,
    source_range source,
    std::string_view detail,
    server_status status) noexcept {

    current_value = {};
    current_valid = false;

    if (failure != nullptr) {
        *failure = {
            kind,
            file,
            source,
            detail,
        };
    }

    return status;
}

bool semantic_preprocessor_v2::active() const noexcept {

    return conditional_depth == 0 ||
        conditionals[
            conditional_depth - 1]
            .branch_active;
}

server_status
semantic_preprocessor_v2::initialize_configuration() noexcept {

    for (const auto& configured :
         configuration.predefines) {

        string_id name;

        auto status =
            strings.intern(
                configured.name,
                name);

        if (!succeeded(status)) {
            return status;
        }

        if (configured.replacement.empty()) {
            status =
                preprocessing.define(
                    name);
        }
        else {
            string_id replacement;

            status =
                strings.intern(
                    configured.replacement,
                    replacement);

            if (succeeded(status)) {
                status =
                    preprocessing.define(
                        name,
                        replacement);
            }
        }

        if (!succeeded(status)) {
            return status;
        }
    }

    return server_status::success;
}

lexical_symbol_resolution_v2*
semantic_preprocessor_v2::find_resolution(
    file_id file) noexcept {

    if (!file ||
        resolution_index.empty()) {

        return nullptr;
    }

    const auto mask =
        resolution_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            file_hash(
                file.value())) &
        mask;

    for (std::size_t probe = 0;
         probe < resolution_index.size();
         ++probe) {

        const auto& slot =
            resolution_index[position];

        if (!slot.file) {
            return nullptr;
        }

        if (slot.file == file) {
            if (slot.record == 0 ||
                slot.record >
                    resolutions.size()) {

                return nullptr;
            }

            const auto& value =
                resolutions[
                    slot.record - 1];

            return value != nullptr &&
                value->file == file
                ? &value->resolution
                : nullptr;
        }

        position =
            (position + 1) &
                mask;
    }

    return nullptr;
}

void semantic_preprocessor_v2::insert_resolution_index(
    std::vector<resolution_slot>& index,
    file_id file,
    std::uint32_t record) const noexcept {

    const auto mask =
        index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            file_hash(
                file.value())) &
        mask;

    while (index[position].file) {
        position =
            (position + 1) &
                mask;
    }

    index[position] = {
        file,
        record,
    };
}

server_status
semantic_preprocessor_v2::ensure_resolution_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            resolutions.size()) {

        return server_status::io_error;
    }

    const auto required =
        resolutions.size() +
        additional;

    if (!resolution_index.empty() &&
        required <=
            resolution_index.size() / 2) {

        return server_status::success;
    }

    if (required >
        (std::numeric_limits<std::size_t>::max)() / 2) {

        return server_status::io_error;
    }

    const auto minimum =
        required * 2;

    std::size_t capacity = 8;

    while (capacity < minimum) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return server_status::io_error;
        }

        capacity *= 2;
    }

    try {
        std::vector<resolution_slot>
            candidate(
                capacity);

        for (std::size_t index = 0;
             index < resolutions.size();
             ++index) {

            if (!resolutions[index] ||
                !resolutions[index]->file) {

                return server_status::
                    project_artifact_invalid;
            }

            insert_resolution_index(
                candidate,
                resolutions[index]->file,
                static_cast<std::uint32_t>(
                    index + 1));
        }

        resolution_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status semantic_preprocessor_v2::ensure_resolution(
    file_id file,
    lexical_symbol_resolution_v2*& output) noexcept {

    output =
        find_resolution(
            file);

    if (output != nullptr) {
        return server_status::success;
    }

    const auto* physical =
        prepared.file(
            file);

    if (physical == nullptr ||
        physical->symbols == nullptr ||
        !files.content_available(
            file)) {

        return server_status::
            project_artifact_invalid;
    }

    const auto source =
        files.content(
            file);

    if (source.size() !=
        physical->symbols->
            source_size()) {

        return server_status::
            project_artifact_invalid;
    }

    const auto indexed =
        ensure_resolution_index_capacity(
            1);

    if (!succeeded(indexed)) {
        return indexed;
    }

    std::unique_ptr<resolution_record>
        stable;

    try {
        resolutions.reserve(
            resolutions.size() + 1);

        stable =
            std::make_unique<
                resolution_record>();
    }
    catch (...) {
        return server_status::io_error;
    }

    const std::array<
        lexical_symbol_source_v2,
        1>
        source_value{
            lexical_symbol_source_v2{
                physical->symbols,
                source},
        };

    std::vector<
        lexical_symbol_resolution_v2>
        merged;

    const auto status =
        merge_lexical_symbols_v2(
            source_value,
            strings,
            merged);

    if (!succeeded(status)) {
        return status;
    }

    if (merged.size() != 1 ||
        !merged[0].complete() ||
        merged[0].file() != file) {

        return server_status::
            project_artifact_invalid;
    }

    stable->file =
        file;

    stable->resolution =
        std::move(
            merged[0]);

    auto* stable_pointer =
        stable.get();

    resolutions.push_back(
        std::move(
            stable));

    insert_resolution_index(
        resolution_index,
        file,
        static_cast<std::uint32_t>(
            resolutions.size()));

    output =
        &stable_pointer->
            resolution;

    return server_status::success;
}

bool semantic_preprocessor_v2::once_contains(
    file_id file) const noexcept {

    if (!file ||
        once_index.empty()) {

        return false;
    }

    const auto mask =
        once_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            file_hash(
                file.value())) &
        mask;

    for (std::size_t probe = 0;
         probe < once_index.size();
         ++probe) {

        const auto value =
            once_index[position];

        if (!value) {
            return false;
        }

        if (value == file) {
            return true;
        }

        position =
            (position + 1) &
                mask;
    }

    return false;
}

server_status semantic_preprocessor_v2::ensure_once_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            once_count) {

        return server_status::io_error;
    }

    const auto required =
        once_count +
        additional;

    if (!once_index.empty() &&
        required <=
            once_index.size() / 2) {

        return server_status::success;
    }

    if (required >
        (std::numeric_limits<std::size_t>::max)() / 2) {

        return server_status::io_error;
    }

    const auto minimum =
        required * 2;

    std::size_t capacity = 8;

    while (capacity < minimum) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return server_status::io_error;
        }

        capacity *= 2;
    }

    try {
        std::vector<file_id>
            candidate(
                capacity);

        const auto mask =
            candidate.size() - 1;

        for (const auto existing :
             once_index) {

            if (!existing) {
                continue;
            }

            auto position =
                static_cast<std::size_t>(
                    file_hash(
                        existing.value())) &
                mask;

            while (candidate[position]) {
                position =
                    (position + 1) &
                        mask;
            }

            candidate[position] =
                existing;
        }

        once_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status semantic_preprocessor_v2::mark_once(
    file_id file) noexcept {

    if (!file) {
        return server_status::
            project_configuration_invalid;
    }

    if (once_contains(file)) {
        return server_status::success;
    }

    const auto prepared_capacity =
        ensure_once_capacity(
            1);

    if (!succeeded(prepared_capacity)) {
        return prepared_capacity;
    }

    const auto mask =
        once_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            file_hash(
                file.value())) &
        mask;

    while (once_index[position]) {
        position =
            (position + 1) &
                mask;
    }

    once_index[position] =
        file;

    ++once_count;

    return server_status::success;
}

server_status semantic_preprocessor_v2::enter_file(
    file_id file) noexcept {

    if (!file ||
        frame_depth ==
            frames.size()) {

        return fail(
            preprocessor_v2_failure_kind::
                include_depth_exceeded,
            file,
            {},
            "Header preprocessing include nesting exceeds supported depth");
    }

    const auto* physical =
        prepared.file(
            file);

    if (physical == nullptr ||
        !physical->valid()) {

        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            file,
            {},
            "Prepared Header input is unavailable");
    }

    lexical_symbol_resolution_v2*
        resolution = nullptr;

    const auto resolved =
        ensure_resolution(
            file,
            resolution);

    if (!succeeded(resolved) ||
        resolution == nullptr) {

        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            file,
            {},
            "Active Header symbols could not be canonicalized",
            resolved);
    }

    if (files.baseline_bound()) {
        const auto replacement =
            files.begin_dependency_replacement(
                file);

        if (!succeeded(replacement)) {
            return fail(
                preprocessor_v2_failure_kind::
                    invalid_input,
                file,
                {},
                "Active Header dependency replacement could not begin",
                replacement);
        }
    }

    auto& target =
        frames[
            frame_depth];

    target = {};
    target.file =
        file;

    target.conditional_floor =
        conditional_depth;

    const prepared_lexical_input_v2
        input{
            file,
            physical->words,
            physical->symbols,
            resolution,
            physical->literals,
        };

    const auto started_input =
        target.cursor.start(
            input);

    if (!succeeded(started_input)) {
        target = {};

        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            file,
            {},
            "Prepared Header lexical replay could not start",
            started_input);
    }

    ++frame_depth;

    return server_status::success;
}

server_status semantic_preprocessor_v2::leave_file() noexcept {

    if (frame_depth == 0) {
        return server_status::
            project_configuration_invalid;
    }

    const auto& leaving =
        frames[
            frame_depth - 1];

    if (conditional_depth !=
        leaving.conditional_floor) {

        const auto source =
            conditional_depth >
                leaving.conditional_floor
            ? conditionals[
                conditional_depth - 1]
                .source
            : source_range{};

        return fail(
            preprocessor_v2_failure_kind::
                conditional_mismatch,
            leaving.file,
            source,
            "Conditional preprocessing group is not closed");
    }

    frames[
        frame_depth - 1] = {};

    --frame_depth;

    return server_status::success;
}

server_status semantic_preprocessor_v2::consume_to_end(
    frame& input) noexcept {

    for (;;) {
        const auto* token =
            input.cursor.current();

        if (token == nullptr) {
            return fail(
                preprocessor_v2_failure_kind::
                    malformed_directive,
                input.file,
                {},
                "Preprocessing directive is not terminated");
        }

        const auto kind =
            token->kind;

        const auto advanced =
            input.cursor.advance();

        if (!succeeded(advanced)) {
            return advanced;
        }

        if (kind ==
            token_kind::pp_end) {

            return server_status::success;
        }
    }
}

server_status semantic_preprocessor_v2::parse_one_identifier(
    frame& input,
    string_id& identifier) noexcept {

    identifier = {};

    const auto* token =
        input.cursor.current();

    if (token == nullptr ||
        token->kind !=
            token_kind::identifier ||
        !token->identifier) {

        return server_status::
            project_configuration_invalid;
    }

    identifier =
        token->identifier;

    const auto advanced =
        input.cursor.advance();

    if (!succeeded(advanced)) {
        return advanced;
    }

    const auto* end =
        input.cursor.current();

    if (end == nullptr ||
        end->kind !=
            token_kind::pp_end) {

        identifier = {};

        return server_status::
            project_configuration_invalid;
    }

    return input.cursor.advance();
}

server_status semantic_preprocessor_v2::parse_define(
    frame& input,
    file_id file,
    source_range source) noexcept {

    std::array<string_id, 2>
        operands{};

    std::size_t count = 0;

    for (;;) {
        const auto* token =
            input.cursor.current();

        if (token == nullptr) {
            return fail(
                preprocessor_v2_failure_kind::
                    malformed_directive,
                file,
                source,
                "Define directive is not terminated");
        }

        if (token->kind ==
            token_kind::pp_end) {

            const auto advanced =
                input.cursor.advance();

            if (!succeeded(advanced)) {
                return advanced;
            }

            break;
        }

        if (token->kind !=
                token_kind::identifier ||
            !token->identifier ||
            count == operands.size()) {

            return fail(
                preprocessor_v2_failure_kind::
                    malformed_directive,
                file,
                source,
                "Only object-like identifier macro definitions are supported");
        }

        operands[count++] =
            token->identifier;

        const auto advanced =
            input.cursor.advance();

        if (!succeeded(advanced)) {
            return advanced;
        }
    }

    if (count == 0) {
        return fail(
            preprocessor_v2_failure_kind::
                malformed_directive,
            file,
            source,
            "Define directive requires an identifier");
    }

    if (!active()) {
        return server_status::success;
    }

    const auto status =
        count == 1
        ? preprocessing.define(
            operands[0])
        : preprocessing.define(
            operands[0],
            operands[1]);

    return succeeded(status)
        ? status
        : fail(
            preprocessor_v2_failure_kind::
                malformed_directive,
            file,
            source,
            "Macro definition conflicts with existing preprocessing state",
            status);
}

server_status semantic_preprocessor_v2::parse_undef(
    frame& input,
    file_id file,
    source_range source) noexcept {

    string_id identifier;

    const auto parsed =
        parse_one_identifier(
            input,
            identifier);

    if (!succeeded(parsed)) {
        return fail(
            preprocessor_v2_failure_kind::
                malformed_directive,
            file,
            source,
            "Undef directive requires exactly one identifier",
            parsed);
    }

    if (!active()) {
        return server_status::success;
    }

    const auto status =
        preprocessing.undefine(
            identifier);

    return succeeded(status)
        ? status
        : fail(
            preprocessor_v2_failure_kind::
                malformed_directive,
            file,
            source,
            "Macro undefinition is invalid",
            status);
}

server_status semantic_preprocessor_v2::begin_conditional(
    frame& input,
    file_id file,
    source_range source,
    bool inverted) noexcept {

    string_id identifier;

    const auto parsed =
        parse_one_identifier(
            input,
            identifier);

    if (!succeeded(parsed)) {
        return fail(
            preprocessor_v2_failure_kind::
                malformed_directive,
            file,
            source,
            "Conditional directive requires exactly one identifier",
            parsed);
    }

    if (conditional_depth ==
        conditionals.size()) {

        return fail(
            preprocessor_v2_failure_kind::
                conditional_mismatch,
            file,
            source,
            "Conditional preprocessing nesting exceeds supported depth");
    }

    const auto parent_active =
        active();

    bool condition = false;

    if (parent_active) {
        condition =
            preprocessing.defined(
                identifier);

        if (inverted) {
            condition =
                !condition;
        }
    }

    conditionals[
        conditional_depth++] = {
            file,
            source,
            parent_active,
            condition,
            parent_active &&
                condition,
            false,
        };

    return server_status::success;
}

server_status semantic_preprocessor_v2::execute_include(
    frame& input,
    file_id file,
    source_range source,
    std::size_t occurrence) noexcept {

    const auto* record =
        prepared.include(
            file,
            occurrence);

    const auto consumed =
        consume_to_end(
            input);

    if (!succeeded(consumed)) {
        return consumed;
    }

    if (!active()) {
        return server_status::success;
    }

    if (record == nullptr) {
        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            file,
            source,
            "Prepared include ordinal does not match lexical input");
    }

    switch (record->state) {
    case prepared_include_state_v2::ready:
        break;

    case prepared_include_state_v2::missing:
        return fail(
            preprocessor_v2_failure_kind::
                active_include_missing,
            file,
            record->locator,
            "Active included Header is missing");

    case prepared_include_state_v2::io_error:
        return fail(
            preprocessor_v2_failure_kind::
                active_include_invalid,
            file,
            record->locator,
            "Active included Header could not be read",
            server_status::io_error);

    case prepared_include_state_v2::lexical_error:
        return fail(
            preprocessor_v2_failure_kind::
                active_include_invalid,
            file,
            record->locator,
            "Active included Header has invalid lexical input");

    case prepared_include_state_v2::invalid:
        return fail(
            preprocessor_v2_failure_kind::
                active_include_invalid,
            file,
            record->locator,
            "Active include is not a supported direct include");
    }

    if (!record->target ||
        prepared.file(
            record->target) ==
            nullptr) {

        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            file,
            record->locator,
            "Prepared active include target is unavailable");
    }

    const auto staged =
        files.add_dependency(
            file,
            record->target);

    if (!succeeded(staged)) {
        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            file,
            record->locator,
            "Active include dependency could not be published",
            staged);
    }

    if (once_contains(
            record->target)) {

        return server_status::success;
    }

    return enter_file(
        record->target);
}

server_status semantic_preprocessor_v2::consume_directive(
    frame& input) noexcept {

    const auto* first =
        input.cursor.current();

    if (first == nullptr ||
        !directive_start_v2(
            first->kind)) {

        return fail(
            preprocessor_v2_failure_kind::
                malformed_directive,
            input.file,
            {},
            "Expected preprocessing directive");
    }

    const auto kind =
        first->kind;

    const source_range source{
        first->source_offset,
        first->source_length,
    };

    const auto advanced =
        input.cursor.advance();

    if (!succeeded(advanced)) {
        return advanced;
    }

    switch (kind) {
    case token_kind::pp_define:
        return parse_define(
            input,
            input.file,
            source);

    case token_kind::pp_undef:
        return parse_undef(
            input,
            input.file,
            source);

    case token_kind::pp_ifdef:
        return begin_conditional(
            input,
            input.file,
            source,
            false);

    case token_kind::pp_ifndef:
        return begin_conditional(
            input,
            input.file,
            source,
            true);

    case token_kind::pp_else: {
        const auto* end =
            input.cursor.current();

        if (end == nullptr ||
            end->kind !=
                token_kind::pp_end) {

            return fail(
                preprocessor_v2_failure_kind::
                    malformed_directive,
                input.file,
                source,
                "Else directive accepts no operands");
        }

        const auto consumed =
            input.cursor.advance();

        if (!succeeded(consumed)) {
            return consumed;
        }

        if (conditional_depth <=
                input.conditional_floor ||
            conditionals[
                conditional_depth - 1]
                .file !=
                    input.file) {

            return fail(
                preprocessor_v2_failure_kind::
                    conditional_mismatch,
                input.file,
                source,
                "Else directive has no matching conditional");
        }

        auto& current =
            conditionals[
                conditional_depth - 1];

        if (current.else_seen) {
            return fail(
                preprocessor_v2_failure_kind::
                    conditional_mismatch,
                input.file,
                source,
                "Conditional preprocessing group has multiple else branches");
        }

        current.else_seen = true;
        current.branch_active =
            current.parent_active &&
            !current.condition;

        return server_status::success;
    }

    case token_kind::pp_endif: {
        const auto* end =
            input.cursor.current();

        if (end == nullptr ||
            end->kind !=
                token_kind::pp_end) {

            return fail(
                preprocessor_v2_failure_kind::
                    malformed_directive,
                input.file,
                source,
                "Endif directive accepts no operands");
        }

        const auto consumed =
            input.cursor.advance();

        if (!succeeded(consumed)) {
            return consumed;
        }

        if (conditional_depth <=
                input.conditional_floor ||
            conditionals[
                conditional_depth - 1]
                .file !=
                    input.file) {

            return fail(
                preprocessor_v2_failure_kind::
                    conditional_mismatch,
                input.file,
                source,
                "Endif directive has no matching conditional");
        }

        --conditional_depth;

        return server_status::success;
    }

    case token_kind::pp_include: {
        const auto occurrence =
            input.include_occurrence++;

        return execute_include(
            input,
            input.file,
            source,
            occurrence);
    }

    case token_kind::pp_pragma: {
        string_id name;
        std::size_t count = 0;
        bool valid_name = true;

        for (;;) {
            const auto* token =
                input.cursor.current();

            if (token == nullptr) {
                return fail(
                    preprocessor_v2_failure_kind::
                        malformed_directive,
                    input.file,
                    source,
                    "Pragma directive is not terminated");
            }

            if (token->kind ==
                token_kind::pp_end) {

                const auto consumed =
                    input.cursor.advance();

                if (!succeeded(consumed)) {
                    return consumed;
                }

                break;
            }

            ++count;

            if (count == 1 &&
                token->kind ==
                    token_kind::identifier &&
                token->identifier) {

                name =
                    token->identifier;
            }
            else {
                valid_name = false;
            }

            const auto consumed =
                input.cursor.advance();

            if (!succeeded(consumed)) {
                return consumed;
            }
        }

        if (!active()) {
            return server_status::success;
        }

        if (count == 1 &&
            valid_name &&
            name &&
            strings.get(name) ==
                "once") {

            return mark_once(
                input.file);
        }

        return fail(
            preprocessor_v2_failure_kind::
                unsupported_directive,
            input.file,
            source,
            "Only #pragma once is supported");
    }

    case token_kind::pp_if:
    case token_kind::pp_elif:
        return fail(
            preprocessor_v2_failure_kind::
                unsupported_directive,
            input.file,
            source,
            "#if/#elif are not supported by the restricted preprocessor");

    case token_kind::pp_line:
    case token_kind::pp_error:
    case token_kind::pp_unknown: {
        const auto consumed =
            consume_to_end(
                input);

        if (!succeeded(consumed)) {
            return consumed;
        }

        return !active()
            ? server_status::success
            : fail(
                preprocessor_v2_failure_kind::
                    unsupported_directive,
                input.file,
                source,
                "Preprocessing directive is not supported");
    }

    default:
        break;
    }

    return fail(
        preprocessor_v2_failure_kind::
            malformed_directive,
        input.file,
        source,
        "Preprocessing directive is invalid");
}

server_status semantic_preprocessor_v2::seek_next() noexcept {

    current_value = {};
    current_valid = false;

    for (;;) {
        if (frame_depth == 0) {
            return server_status::success;
        }

        auto& input =
            frames[
                frame_depth - 1];

        if (input.cursor.finished()) {
            const auto left =
                leave_file();

            if (!succeeded(left)) {
                return left;
            }

            continue;
        }

        const auto* raw =
            input.cursor.current();

        if (raw == nullptr) {
            return fail(
                preprocessor_v2_failure_kind::
                    invalid_input,
                input.file,
                {},
                "Prepared Header cursor has no current token");
        }

        if (directive_start_v2(
                raw->kind)) {

            const auto executed =
                consume_directive(
                    input);

            if (!succeeded(executed)) {
                return executed;
            }

            continue;
        }

        if (!active()) {
            const auto skipped =
                input.cursor.advance();

            if (!succeeded(skipped)) {
                return skipped;
            }

            continue;
        }

        current_value =
            *raw;

        if (raw->kind ==
            token_kind::identifier) {

            preprocessor_expansion
                expansion;

            const auto expanded =
                preprocessing.expand(
                    raw->identifier,
                    expansion);

            if (!succeeded(expanded)) {
                return fail(
                    preprocessor_v2_failure_kind::
                        invalid_input,
                    raw->file,
                    {
                        raw->source_offset,
                        raw->source_length,
                    },
                    "Identifier macro expansion failed",
                    expanded);
            }

            if (expansion.kind ==
                preprocessor_expansion_kind::
                    empty) {

                const auto skipped =
                    input.cursor.advance();

                if (!succeeded(skipped)) {
                    return skipped;
                }

                current_value = {};

                continue;
            }

            current_value.identifier =
                expansion.identifier;
        }

        current_valid = true;

        return server_status::success;
    }
}

server_status semantic_preprocessor_v2::start(
    file_id root) noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    current_value = {};
    current_valid = false;
    started = false;

    frame_depth = 0;
    conditional_depth = 0;

    resolutions.clear();
    resolution_index.clear();

    once_index.clear();
    once_count = 0;

    preprocessing.reset();

    if (!root ||
        prepared.file(
            root) == nullptr) {

        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            root,
            {},
            "Prepared Header root is unavailable");
    }

    const auto initialized =
        initialize_configuration();

    if (!succeeded(initialized)) {
        return fail(
            preprocessor_v2_failure_kind::
                invalid_input,
            root,
            {},
            "Preprocessor configuration could not be initialized",
            initialized);
    }

    const auto entered =
        enter_file(
            root);

    if (!succeeded(entered)) {
        return entered;
    }

    started = true;

    return seek_next();
}

server_status semantic_preprocessor_v2::advance() noexcept {

    if (!started ||
        !current_valid ||
        frame_depth == 0) {

        return server_status::
            project_configuration_invalid;
    }

    auto& input =
        frames[
            frame_depth - 1];

    const auto advanced =
        input.cursor.advance();

    if (!succeeded(advanced)) {
        return advanced;
    }

    return seek_next();
}

}
