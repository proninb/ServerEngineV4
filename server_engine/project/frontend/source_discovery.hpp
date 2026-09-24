/*
 * Initial physical source preparation.
 *
 * Project-declared Header/Source bytes are materialized and lexed in parallel.
 * This stage performs no preprocessing execution, include discovery, string
 * interning, semantic work, or dependency publication.
 *
 * Active include discovery belongs to the single Parser/Semantic preprocessing
 * execution so one semantic root is never preprocessed twice.
 */
#pragma once

#include "lexical_generation.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstdint>
#include <span>

namespace cw::server {

enum class source_preparation_failure_kind : std::uint8_t {
    none = 0,
    lexical,
};

struct source_preparation_failure final {
    source_preparation_failure_kind kind =
        source_preparation_failure_kind::none;
    file_id file{};
    lexical_error lexical;
};

[[nodiscard]] server_status prepare_source_lexical_state(
    file_context& files,
    lexical_generation& lexical,
    source_preparation_failure* failure = nullptr) noexcept;

struct source_replacement_metrics final {
    std::uint64_t masked_files = 0;
    std::uint64_t retokenized_files = 0;
    std::uint64_t missing_files = 0;
    std::uint64_t active_lanes = 0;
};

// BUILD-only sparse lexical replacement. semantic_changed must be strictly
// ascending file_id values from exact SourceSave classification. Header/Source
// baselines are masked before workers start; present changed files are tokenized
// from already-acquired File Context bytes, while missing files remain masked.
[[nodiscard]] server_status replace_source_lexical_state(
    file_context& files,
    lexical_generation& lexical,
    std::span<const file_id> semantic_changed,
    source_preparation_failure* failure = nullptr,
    source_replacement_metrics* metrics = nullptr) noexcept;

}
