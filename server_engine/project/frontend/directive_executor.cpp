#include "directive_executor.hpp"

#include "../preprocessor/preprocessor.hpp"
#include "../string/string_table.hpp"

namespace cw::server {
namespace {

[[nodiscard]] bool range_view(
    std::string_view source,
    source_range range,
    std::string_view& output) noexcept {

    output = {};

    const auto offset =
        static_cast<std::size_t>(range.offset);

    const auto length =
        static_cast<std::size_t>(range.length);

    if (length == 0 ||
        offset > source.size() ||
        length > source.size() - offset) {

        return false;
    }

    output =
        source.substr(
            offset,
            length);

    return true;
}

[[nodiscard]] server_status intern_range(
    std::string_view source,
    source_range range,
    string_table& strings,
    string_id& output) noexcept {

    output = {};

    std::string_view spelling;

    if (!range_view(
            source,
            range,
            spelling)) {

        return server_status::project_configuration_invalid;
    }

    return strings.intern(
        spelling,
        output);
}

}

server_status initialize_preprocessor(
    const preprocessor_configuration& configuration,
    string_table& strings,
    preprocessor& state) noexcept {

    for (const auto& configured : configuration.predefines) {
        string_id name;

        const auto name_status =
            strings.intern(
                configured.name,
                name);

        if (!succeeded(name_status)) {
            return name_status;
        }

        if (configured.replacement.empty()) {
            const auto define_status =
                state.define(name);

            if (!succeeded(define_status)) {
                return define_status;
            }

            continue;
        }

        string_id replacement;

        const auto replacement_status =
            strings.intern(
                configured.replacement,
                replacement);

        if (!succeeded(replacement_status)) {
            return replacement_status;
        }

        const auto define_status =
            state.define(
                name,
                replacement);

        if (!succeeded(define_status)) {
            return define_status;
        }
    }

    return server_status::success;
}

bool directive_executor::active() const noexcept {

    return conditional_depth == 0 ||
        conditionals[
            conditional_depth - 1]
            .branch_active;
}

bool directive_executor::current_file_entry(
    file_id file,
    std::size_t& conditional_floor) const noexcept {

    conditional_floor = 0;

    if (!file ||
        file_depth == 0 ||
        file_entries[
            file_depth - 1].file != file) {

        return false;
    }

    conditional_floor =
        file_entries[
            file_depth - 1].
            conditional_floor;

    return conditional_floor <=
        conditional_depth;
}

server_status directive_executor::enter_file(
    file_id file) noexcept {

    if (!file ||
        file_depth ==
            file_entries.size()) {

        return server_status::
            project_configuration_invalid;
    }

    file_entries[
        file_depth++] = {
            file,
            conditional_depth,
        };

    return server_status::success;
}

server_status directive_executor::fail(
    directive_execution_error_kind kind,
    file_id file,
    source_range source,
    directive_execution_error* error) const noexcept {

    if (error != nullptr) {
        *error = {
            kind,
            file,
            source,
        };
    }

    return server_status::project_configuration_invalid;
}

server_status directive_executor::begin_conditional(
    const preprocessing_directive& directive,
    std::string_view source,
    bool inverted,
    directive_execution_error* error) noexcept {

    if (conditional_depth == conditionals.size()) {
        return fail(
            directive_execution_error_kind::
                conditional_depth_exceeded,
            directive.range.file,
            directive.range.source,
            error);
    }

    const auto parent_active =
        active();

    bool condition = false;

    if (parent_active) {
        std::string_view spelling;

        if (!range_view(
                source,
                directive.identifier.name,
                spelling)) {

            return fail(
                directive_execution_error_kind::
                    malformed_operand,
                directive.range.file,
                directive.identifier.name,
                error);
        }

        const auto id =
            strings.find(
                spelling);

        condition =
            id &&
            state.defined(id);

        if (inverted) {
            condition = !condition;
        }
    }

    conditionals[
        conditional_depth++] = {
            directive.range.file,
            directive.range.source,
            parent_active,
            condition,
            parent_active && condition,
            false,
        };

    return server_status::success;
}

server_status directive_executor::execute(
    const preprocessing_directive& directive,
    std::string_view source,
    directive_execution_result& output,
    directive_execution_error* error) noexcept {

    output = {};

    if (error != nullptr) {
        *error = {};
    }

    std::size_t conditional_floor = 0;

    if (!current_file_entry(
            directive.range.file,
            conditional_floor)) {

        return fail(
            directive_execution_error_kind::
                file_entry_mismatch,
            directive.range.file,
            directive.range.source,
            error);
    }

    switch (directive.kind) {
    case directive_kind::ifdef:
        return begin_conditional(
            directive,
            source,
            false,
            error);

    case directive_kind::ifndef:
        return begin_conditional(
            directive,
            source,
            true,
            error);

    case directive_kind::else_: {
        if (conditional_depth <=
                conditional_floor ||
            conditionals[
                conditional_depth - 1]
                .file != directive.range.file) {

            return fail(
                directive_execution_error_kind::
                    unmatched_else,
                directive.range.file,
                directive.range.source,
                error);
        }

        auto& current =
            conditionals[
                conditional_depth - 1];

        if (current.else_seen) {
            return fail(
                directive_execution_error_kind::
                    duplicate_else,
                directive.range.file,
                directive.range.source,
                error);
        }

        current.else_seen = true;
        current.branch_active =
            current.parent_active &&
            !current.condition;

        return server_status::success;
    }

    case directive_kind::endif:
        if (conditional_depth <=
                conditional_floor ||
            conditionals[
                conditional_depth - 1]
                .file != directive.range.file) {

            return fail(
                directive_execution_error_kind::
                    unmatched_endif,
                directive.range.file,
                directive.range.source,
                error);
        }

        --conditional_depth;
        return server_status::success;

    case directive_kind::if_:
    case directive_kind::elif:
        return fail(
            directive_execution_error_kind::
                unsupported_directive,
            directive.range.file,
            directive.range.source,
            error);

    default:
        break;
    }

    if (!active()) {
        return server_status::success;
    }

    switch (directive.kind) {
    case directive_kind::define: {
        string_id name;

        const auto name_status =
            intern_range(
                source,
                directive.identifier.name,
                strings,
                name);

        if (!succeeded(name_status)) {
            return fail(
                directive_execution_error_kind::
                    malformed_operand,
                directive.range.file,
                directive.identifier.name,
                error);
        }

        if (directive.identifier.replacement.length == 0) {
            const auto define_status =
                state.define(name);

            if (define_status ==
                server_status::project_configuration_invalid) {

                return fail(
                    directive_execution_error_kind::
                        invalid_macro_definition,
                    directive.range.file,
                    directive.range.source,
                    error);
            }

            return define_status;
        }

        string_id replacement;

        const auto replacement_status =
            intern_range(
                source,
                directive.identifier.replacement,
                strings,
                replacement);

        if (!succeeded(replacement_status)) {
            return fail(
                directive_execution_error_kind::
                    malformed_operand,
                directive.range.file,
                directive.identifier.replacement,
                error);
        }

        const auto define_status =
            state.define(
                name,
                replacement);

        if (define_status ==
            server_status::project_configuration_invalid) {

            return fail(
                directive_execution_error_kind::
                    invalid_macro_definition,
                directive.range.file,
                directive.range.source,
                error);
        }

        return define_status;
    }

    case directive_kind::undef: {
        std::string_view spelling;

        if (!range_view(
                source,
                directive.identifier.name,
                spelling)) {

            return fail(
                directive_execution_error_kind::
                    malformed_operand,
                directive.range.file,
                directive.identifier.name,
                error);
        }

        const auto id =
            strings.find(
                spelling);

        return id
            ? state.undefine(id)
            : server_status::success;
    }

    case directive_kind::include:
        if (directive.include.form ==
                include_form::none ||
            directive.include.locator.length == 0) {

            return fail(
                directive_execution_error_kind::
                    invalid_include,
                directive.range.file,
                directive.range.source,
                error);
        }

        output.kind =
            directive_execution_kind::include;

        output.include = {
            directive.range.file,
            directive.include.form,
            directive.include.locator,
        };

        return server_status::success;

    case directive_kind::line:
    case directive_kind::error:
    case directive_kind::pragma:
    case directive_kind::unknown:
        return fail(
            directive_execution_error_kind::
                unsupported_directive,
            directive.range.file,
            directive.range.source,
            error);

    case directive_kind::invalid:
        return fail(
            directive_execution_error_kind::
                malformed_operand,
            directive.range.file,
            directive.range.source,
            error);

    case directive_kind::ifdef:
    case directive_kind::ifndef:
    case directive_kind::else_:
    case directive_kind::endif:
    case directive_kind::if_:
    case directive_kind::elif:
        break;
    }

    return server_status::project_configuration_invalid;
}

server_status directive_executor::finish_file(
    file_id file,
    directive_execution_error* error) noexcept {

    if (error != nullptr) {
        *error = {};
    }

    std::size_t conditional_floor = 0;

    if (!current_file_entry(
            file,
            conditional_floor)) {

        return fail(
            directive_execution_error_kind::
                file_entry_mismatch,
            file,
            {},
            error);
    }

    if (conditional_depth >
        conditional_floor) {

        const auto& opening =
            conditionals[
                conditional_depth - 1];

        return fail(
            directive_execution_error_kind::
                unterminated_conditional,
            opening.file,
            opening.source,
            error);
    }

    if (conditional_depth !=
        conditional_floor) {

        return fail(
            directive_execution_error_kind::
                file_entry_mismatch,
            file,
            {},
            error);
    }

    --file_depth;
    return server_status::success;
}

}
