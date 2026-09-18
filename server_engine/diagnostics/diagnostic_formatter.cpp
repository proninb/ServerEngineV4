/*
 * Clang-style diagnostics formatter implementation.
 */
#include "diagnostic_formatter.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace cw::server {
namespace {

[[nodiscard]] std::string expand_tabs(
    std::string_view text,
    std::uint32_t tab_width = 4) {

    std::string output;
    output.reserve(text.size());

    std::uint32_t column = 1;

    for (const char ch : text) {
        if (ch == '\t') {
            const auto spaces =
                tab_width -
                ((column - 1) % tab_width);

            output.append(
                spaces,
                ' ');

            column += spaces;
        } else {
            output.push_back(ch);
            ++column;
        }
    }

    return output;
}

[[nodiscard]] std::size_t expanded_prefix_width(
    std::string_view text,
    std::size_t byte_count,
    std::uint32_t tab_width = 4) noexcept {

    const auto limit =
        std::min(
            byte_count,
            text.size());

    std::size_t width = 0;

    for (std::size_t i = 0;
         i < limit;
         ++i) {

        if (text[i] == '\t') {
            const auto column =
                static_cast<std::uint32_t>(
                    width + 1);

            width +=
                tab_width -
                ((column - 1) % tab_width);
        } else {
            ++width;
        }
    }

    return width;
}

[[nodiscard]] std::size_t same_line_range_length(
    const diagnostic_source_file& file,
    const diagnostic_location& location) noexcept {

    if (location.length == 0 ||
        location.offset >= file.text.size()) {
        return 0;
    }

    const auto requested_end =
        std::min<std::size_t>(
            file.text.size(),
            static_cast<std::size_t>(location.offset) +
                static_cast<std::size_t>(location.length));

    std::size_t end =
        static_cast<std::size_t>(
            location.offset);

    while (end < requested_end &&
           file.text[end] != '\r' &&
           file.text[end] != '\n') {
        ++end;
    }

    return
        end -
        static_cast<std::size_t>(
            location.offset);
}

void write_location_prefix(
    std::ostream& output,
    const diagnostic_location& location) {

    if (!location.file.empty()) {
        output <<
            location.file.string();
    } else {
        output << "<unknown>";
    }

    if (location.line != 0) {
        output <<
            ':' <<
            location.line;

        if (location.column != 0) {
            output <<
                ':' <<
                location.column;
        }
    }
}

void write_source(
    std::ostream& output,
    const diagnostic_collection& diagnostics,
    const diagnostic_record& record) {

    if (record.location.file_id ==
            invalid_diagnostic_file_id ||
        record.location.line == 0) {
        return;
    }

    const auto* file =
        diagnostics.sources().find(
            record.location.file_id);

    if (file == nullptr) {
        return;
    }

    const auto raw_line =
        diagnostics.sources().line_text(
            record.location.file_id,
            record.location.line);

    const auto source_line =
        expand_tabs(raw_line);

    output <<
        source_line <<
        '\n';

    const auto raw_column =
        record.location.column == 0
            ? std::size_t{0}
            : static_cast<std::size_t>(
                  record.location.column - 1);

    const auto caret_column =
        expanded_prefix_width(
            raw_line,
            raw_column);

    output <<
        std::string(
            caret_column,
            ' ') <<
        '^';

    const auto raw_range =
        same_line_range_length(
            *file,
            record.location);

    if (raw_range > 1) {
        const auto expanded_end =
            expanded_prefix_width(
                raw_line,
                raw_column + raw_range);

        const auto expanded_length =
            expanded_end > caret_column
                ? expanded_end - caret_column
                : std::size_t{1};

        if (expanded_length > 1) {
            output <<
                std::string(
                    expanded_length - 1,
                    '~');
        }
    }

    output << '\n';
}

}

void format_diagnostics(
    std::ostream& output,
    const diagnostic_collection& diagnostics,
    diagnostic_registry_view registry) {

    for (const auto& record :
         diagnostics.records()) {

        const auto* descriptor =
            registry.find(record.id);

        write_location_prefix(
            output,
            record.location);

        output <<
            ": " <<
            diagnostic_severity_name(
                record.severity) <<
            ": ";

        if (!record.message.empty()) {
            output <<
                record.message;
        } else if (descriptor != nullptr) {
            output <<
                descriptor->message;
        } else {
            output <<
                "Unknown diagnostic";
        }

        output << '\n';

        write_source(
            output,
            diagnostics,
            record);

        if (!record.detail.empty()) {
            output <<
                "detail: " <<
                record.detail <<
                '\n';
        }
    }
}

}
