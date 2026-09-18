/*
 * Core Server diagnostics data model.
 *
 * A descriptor defines one stable diagnostic kind.
 * A diagnostic_record is one occurrence produced by one Server operation.
 *
 * Source coordinates:
 * - offset is zero-based byte offset;
 * - line and column are one-based presentation coordinates;
 * - length is a byte range beginning at offset.
 */
#pragma once

#include "../operation.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace cw::server {

using diagnostic_code_value = std::uint32_t;
using diagnostic_file_id = std::uint32_t;

inline constexpr diagnostic_file_id invalid_diagnostic_file_id =
    0xFFFF'FFFFu;

// Stable numeric identity of one diagnostic definition.
class diagnostic_id final {
public:
    // Invalid diagnostic identifier.
    constexpr diagnostic_id() noexcept = default;

    // Constructs an identifier from its catalog value.
    explicit constexpr diagnostic_id(
        diagnostic_code_value value) noexcept
        : value_storage(value) {
    }

    // Returns the stable catalog value.
    [[nodiscard]] constexpr diagnostic_code_value value() const noexcept {
        return value_storage;
    }

    // Returns true when this identifier references a catalog entry.
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value_storage != 0;
    }

    friend constexpr bool operator==(
        diagnostic_id,
        diagnostic_id) noexcept = default;

private:
    // Zero is reserved for an invalid identifier.
    diagnostic_code_value value_storage = 0;
};

// Diagnostic importance and control significance.
enum class diagnostic_severity : std::uint8_t {
    // Additional explanatory information.
    note,

    // Operation may succeed but behavior deserves attention.
    warning,

    // Operation failed because of a recoverable error.
    error,

    // Operation or subsystem could not continue safely.
    fatal,
};

// Architectural subsystem that owns a diagnostic definition.
enum class diagnostic_domain : std::uint8_t {
    // Descriptor has no assigned architectural domain.
    unknown = 0,

    // Server process startup/lifecycle.
    server,

    // Process configuration.
    configuration,

    // Communication endpoint lifecycle or protocol.
    communication,

    // Project ownership/lifecycle.
    project,

    // Source acquisition/dependency management.
    source,

    // Parser/semantic frontend.
    parser,

    // Build/generation pipeline.
    build,

    // Runtime execution.
    runtime,

    // Persistence/load/save.
    persistence,
};

// Clang-style source/document location for one diagnostic occurrence.
struct diagnostic_location {
    // File path displayed to the user when available.
    std::filesystem::path file;

    // Zero-based byte offset into the original source text.
    std::uint32_t offset = 0;

    // Byte length of the highlighted range. Zero means caret-only location.
    std::uint32_t length = 0;

    // One-based line number. Zero means unavailable.
    std::uint32_t line = 0;

    // One-based byte column. Zero means unavailable.
    std::uint32_t column = 0;

    // Source-cache identity used to recover the source line for rendering.
    diagnostic_file_id file_id = invalid_diagnostic_file_id;

    // Returns true when a file identity/path is available.
    [[nodiscard]] bool has_file() const noexcept {
        return file_id != invalid_diagnostic_file_id || !file.empty();
    }

    // Returns true when line/column presentation can be rendered.
    [[nodiscard]] bool has_position() const noexcept {
        return has_file() && line != 0;
    }

    // Returns true when the diagnostic highlights a source range.
    [[nodiscard]] bool has_range() const noexcept {
        return length != 0;
    }
};

// One concrete diagnostic emitted during one Server operation.
struct diagnostic_record {
    // Stable descriptor catalog identity.
    diagnostic_id id;

    // Effective severity for this occurrence.
    diagnostic_severity severity = diagnostic_severity::error;

    // Operation that produced this occurrence.
    operation_id operation;

    // Source/document location and highlight range.
    diagnostic_location location;

    // Occurrence-specific explanation of what is wrong.
    std::string detail;

    // Optional occurrence-specific primary message.
    // Empty means use diagnostic_descriptor::message.
    std::string message;
};

// Returns the Clang-compatible severity spelling.
[[nodiscard]] constexpr const char* diagnostic_severity_name(
    diagnostic_severity severity) noexcept {

    switch (severity) {
    case diagnostic_severity::note:
        return "note";
    case diagnostic_severity::warning:
        return "warning";
    case diagnostic_severity::error:
        return "error";
    case diagnostic_severity::fatal:
        return "fatal error";
    }

    return "error";
}

}
