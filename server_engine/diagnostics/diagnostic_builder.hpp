/*
 * Fluent construction helper for diagnostic_record.
 *
 * The builder is a convenience layer only; diagnostic_record remains the
 * storage/protocol model.
 */
#pragma once

#include "diagnostic_descriptor.hpp"

#include <filesystem>
#include <string_view>
#include <utility>

namespace cw::server {

// Builds one diagnostic occurrence from a stable descriptor.
class diagnostic_builder final {
public:
    // Initializes id/severity from descriptor and associates one operation.
    diagnostic_builder(
        const diagnostic_descriptor& descriptor,
        operation_id operation)
        : descriptor(descriptor) {

        record.id =
            descriptor.id;
        record.severity =
            descriptor.default_severity;
        record.operation =
            operation;
    }

    // Overrides severity for this occurrence.
    diagnostic_builder& severity(
        diagnostic_severity value) noexcept {

        record.severity = value;
        return *this;
    }

    // Sets complete source/document location.
    diagnostic_builder& location(
        diagnostic_location value) {

        record.location =
            std::move(value);
        return *this;
    }

    // Sets the display file path when no source-cache identity is required.
    diagnostic_builder& file(
        std::filesystem::path value) {

        record.location.file =
            std::move(value);
        return *this;
    }

    // Sets one-based presentation coordinates.
    diagnostic_builder& line_column(
        std::uint32_t line,
        std::uint32_t column) noexcept {

        record.location.line = line;
        record.location.column = column;
        return *this;
    }

    // Sets zero-based byte range.
    diagnostic_builder& span(
        std::uint32_t offset,
        std::uint32_t length) noexcept {

        record.location.offset = offset;
        record.location.length = length;
        return *this;
    }

    // Overrides the descriptor's default primary message.
    diagnostic_builder& message(
        std::string_view value) {

        record.message.assign(
            value.data(),
            value.size());
        return *this;
    }

    // Describes specifically what is wrong for this occurrence.
    diagnostic_builder& detail(
        std::string_view value) {

        record.detail.assign(
            value.data(),
            value.size());
        return *this;
    }

    // Moves the completed diagnostic record from an rvalue builder.
    [[nodiscard]] diagnostic_record build() && {
        return std::move(record);
    }

    // Copies the completed record when fluent chaining has produced an lvalue
    // reference to the temporary builder.
    [[nodiscard]] diagnostic_record build() const& {
        return record;
    }

private:
    // Descriptor lifetime is static; retained for architectural clarity.
    const diagnostic_descriptor& descriptor;

    // In-progress diagnostic occurrence.
    diagnostic_record record;
};

// Starts a builder with the descriptor's default severity.
[[nodiscard]] inline diagnostic_builder diagnostic(
    const diagnostic_descriptor& descriptor,
    operation_id operation) {

    return diagnostic_builder(
        descriptor,
        operation);
}

}
