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

}
