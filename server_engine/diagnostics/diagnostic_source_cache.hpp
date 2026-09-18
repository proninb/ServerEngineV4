/*
 * Operation-local source text cache used only for diagnostic presentation.
 *
 * Source text is stored once per file so individual diagnostic records remain
 * compact and do not duplicate complete source lines/files.
 */
#pragma once

#include "diagnostic.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cw::server {

// Immutable source snapshot retained for diagnostic rendering.
struct diagnostic_source_file {
    // Operation-local source-cache identity.
    diagnostic_file_id id = invalid_diagnostic_file_id;

    // Source/document path shown in diagnostics.
    std::filesystem::path path;

    // Exact original source text.
    std::string text;

    // Zero-based byte offset of each one-based source line.
    std::vector<std::uint32_t> line_offsets;
};

// Owns source snapshots referenced by one diagnostic_collection.
class diagnostic_source_cache final {
public:
    // Stores one immutable source snapshot and returns its operation-local id.
    [[nodiscard]] diagnostic_file_id add(
        std::filesystem::path path,
        std::string text);

    // Returns source metadata by operation-local id.
    [[nodiscard]] const diagnostic_source_file* find(
        diagnostic_file_id id) const noexcept;

    // Returns one source line without CR/LF terminators.
    [[nodiscard]] std::string_view line_text(
        diagnostic_file_id id,
        std::uint32_t line) const noexcept;

    // Converts a byte offset/range into a one-based line/column location.
    [[nodiscard]] diagnostic_location locate(
        diagnostic_file_id id,
        std::size_t offset,
        std::size_t length = 0) const noexcept;

private:
    // Builds one-based line lookup from exact source bytes.
    static void build_line_offsets(
        diagnostic_source_file& file);

    // Operation-local immutable source files.
    std::vector<diagnostic_source_file> files;
};

}
