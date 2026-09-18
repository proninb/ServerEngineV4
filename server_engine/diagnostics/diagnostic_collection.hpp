/*
 * Mutable diagnostics produced by one Server operation.
 *
 * diagnostic_collection owns both diagnostic records and immutable source
 * snapshots needed to render Clang-style source lines/carets.
 */
#pragma once

#include "diagnostic.hpp"
#include "diagnostic_source_cache.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cw::server {

// Owns diagnostic occurrences produced during one Server operation.
class diagnostic_collection final {
public:
    // Removes records and diagnostic source snapshots for reuse.
    void clear() noexcept;

    // Preallocates storage for an expected record count.
    void reserve(std::size_t count);

    // Appends one fully constructed diagnostic occurrence.
    void emit(diagnostic_record record);

    // Stores original source text once for subsequent line/caret rendering.
    [[nodiscard]] diagnostic_file_id add_source(
        std::filesystem::path path,
        std::string text);

    // Resolves offset/range to file + line + column.
    [[nodiscard]] diagnostic_location locate(
        diagnostic_file_id file,
        std::size_t offset,
        std::size_t length = 0) const noexcept;

    // Returns true when no diagnostics have been emitted.
    [[nodiscard]] bool empty() const noexcept;

    // Returns true when at least one error/fatal diagnostic exists.
    [[nodiscard]] bool has_errors() const noexcept;

    // Returns a read-only contiguous view over emitted records.
    [[nodiscard]] std::span<const diagnostic_record> records() const noexcept;

    // Provides source snapshots to presentation formatters.
    [[nodiscard]] const diagnostic_source_cache& sources() const noexcept;

    // Produces stable presentation order independent of worker scheduling.
    void sort_deterministic();

private:
    // Operation-local owned record storage.
    std::vector<diagnostic_record> records_storage;

    // Original source/document snapshots referenced by locations.
    diagnostic_source_cache source_storage;
};

}
