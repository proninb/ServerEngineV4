/*
 * Parser V2 prepared lexical replay boundary.
 *
 * Physical text has already been lexed and identifier spellings have already
 * been merged to canonical string_id values. This view borrows only compact
 * lexical words plus the file-local symbol stream and its resolved IDs.
 */
#pragma once

#include "lexical_symbols_v2.hpp"
#include "prepared_token.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {


class prepared_literal_stream_v2 final {
public:
    [[nodiscard]] server_status reset(
        file_id file) noexcept {

        if (!file) {
            return server_status::
                project_configuration_invalid;
        }

        source_file =
            file;

        numbers.clear();

        return server_status::success;
    }

    [[nodiscard]] server_status record(
        prepared_number_v2 value) noexcept {

        if (!source_file) {
            return server_status::
                project_configuration_invalid;
        }

        try {
            numbers.push_back(
                value);

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    [[nodiscard]] file_id file() const noexcept {
        return source_file;
    }

    [[nodiscard]] std::size_t number_count() const noexcept {
        return numbers.size();
    }

    [[nodiscard]] prepared_number_v2 number(
        std::size_t occurrence) const noexcept {

        return occurrence < numbers.size()
            ? numbers[occurrence]
            : prepared_number_v2{};
    }

private:
    file_id source_file{};
    std::vector<prepared_number_v2> numbers;
};

struct prepared_lexical_input_v2 final {
    file_id file{};
    std::span<const std::uint32_t> words;
    const lexical_symbol_stream_v2* symbols = nullptr;
    const lexical_symbol_resolution_v2* resolution = nullptr;
    const prepared_literal_stream_v2* literals = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return file &&
            symbols != nullptr &&
            resolution != nullptr &&
            literals != nullptr &&
            symbols->file() == file &&
            resolution->file() == file &&
            literals->file() == file &&
            resolution->complete();
    }
};

}
