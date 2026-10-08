/*
 * Parser V2 file-local lexical symbolization.
 *
 * One lexer invocation owns one lexical_symbol_stream_v2. Identifier spellings
 * are deduplicated only inside that physical file; no shared/global state is
 * touched while files are lexed in parallel.
 */
#pragma once

#include "../file/file_id.hpp"
#include "../../server_status.hpp"
#include "../../string_id.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {

class string_table;

class local_symbol_id_v2 final {
public:
    constexpr local_symbol_id_v2() noexcept = default;

    explicit constexpr local_symbol_id_v2(
        std::uint32_t value) noexcept
        : slot(value) {
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return slot;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        local_symbol_id_v2,
        local_symbol_id_v2) noexcept = default;

private:
    std::uint32_t slot = 0;
};

static_assert(sizeof(local_symbol_id_v2) == 4);

struct lexical_symbol_record_v2 final {
    std::uint32_t hash = 0;
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
};

static_assert(sizeof(lexical_symbol_record_v2) == 12);

class lexical_symbol_stream_v2 final {
public:
    [[nodiscard]] server_status reset(
        file_id file,
        std::size_t source_size) noexcept {

        if (!file ||
            source_size >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

            return server_status::
                project_configuration_invalid;
        }

        source_file = file;
        source_size_value =
            static_cast<std::uint32_t>(
                source_size);

        symbol_records.clear();
        identifier_symbols.clear();

        for (auto& slot : symbol_index) {
            slot = {};
        }

        return server_status::success;
    }

    [[nodiscard]] server_status record_identifier(
        std::string_view source,
        std::uint32_t hash,
        std::uint32_t source_offset,
        std::uint32_t source_length) noexcept {

        if (!source_file ||
            source.size() != source_size_value ||
            source_length == 0 ||
            source_offset > source_size_value ||
            source_length >
                source_size_value - source_offset) {

            return server_status::
                project_configuration_invalid;
        }

        if (identifier_symbols.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

            return server_status::io_error;
        }

        auto symbol =
            find_symbol(
                source,
                hash,
                source_offset,
                source_length);

        const auto creating =
            !symbol;

        if (creating) {
            if (symbol_records.size() >=
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

                return server_status::io_error;
            }

            const auto prepared =
                ensure_index_capacity(1);

            if (!succeeded(prepared)) {
                return prepared;
            }

            try {
                symbol_records.reserve(
                    symbol_records.size() + 1);

                identifier_symbols.reserve(
                    identifier_symbols.size() + 1);
            }
            catch (...) {
                return server_status::io_error;
            }

            symbol_records.push_back({
                hash,
                source_offset,
                source_length,
            });

            symbol =
                local_symbol_id_v2{
                    static_cast<std::uint32_t>(
                        symbol_records.size())};

            insert_index(
                symbol_index,
                symbol,
                hash);
        }
        else {
            try {
                identifier_symbols.reserve(
                    identifier_symbols.size() + 1);
            }
            catch (...) {
                return server_status::io_error;
            }
        }

        identifier_symbols.push_back(
            symbol.value());

        return server_status::success;
    }

    [[nodiscard]] file_id file() const noexcept {
        return source_file;
    }

    [[nodiscard]] std::size_t source_size() const noexcept {
        return source_size_value;
    }

    [[nodiscard]] std::size_t symbol_count() const noexcept {
        return symbol_records.size();
    }

    [[nodiscard]] std::size_t identifier_count() const noexcept {
        return identifier_symbols.size();
    }

    [[nodiscard]] std::span<const lexical_symbol_record_v2>
    symbols() const noexcept {
        return symbol_records;
    }

    [[nodiscard]] const lexical_symbol_record_v2* symbol(
        local_symbol_id_v2 id) const noexcept {

        if (!id ||
            id.value() > symbol_records.size()) {

            return nullptr;
        }

        return &symbol_records[
            static_cast<std::size_t>(
                id.value() - 1)];
    }

    [[nodiscard]] local_symbol_id_v2 identifier_symbol(
        std::size_t occurrence) const noexcept {

        return occurrence < identifier_symbols.size()
            ? local_symbol_id_v2{
                identifier_symbols[occurrence]}
            : local_symbol_id_v2{};
    }

    [[nodiscard]] std::string_view spelling(
        local_symbol_id_v2 id,
        std::string_view source) const noexcept {

        const auto* record =
            symbol(id);

        if (record == nullptr ||
            source.size() != source_size_value ||
            record->source_offset > source.size() ||
            record->source_length >
                source.size() -
                    record->source_offset) {

            return {};
        }

        return source.substr(
            record->source_offset,
            record->source_length);
    }

private:
    struct symbol_slot final {
        std::uint32_t hash = 0;
        std::uint32_t symbol = 0;
    };

    static_assert(sizeof(symbol_slot) == 8);

    [[nodiscard]] local_symbol_id_v2 find_symbol(
        std::string_view source,
        std::uint32_t hash,
        std::uint32_t source_offset,
        std::uint32_t source_length) const noexcept {

        if (symbol_index.empty()) {
            return {};
        }

        const auto mask =
            symbol_index.size() - 1;

        auto position =
            static_cast<std::size_t>(
                hash) &
            mask;

        const auto spelling =
            source.substr(
                source_offset,
                source_length);

        for (std::size_t probe = 0;
             probe < symbol_index.size();
             ++probe) {

            const auto& slot =
                symbol_index[position];

            if (slot.symbol == 0) {
                return {};
            }

            if (slot.hash == hash &&
                slot.symbol <=
                    symbol_records.size()) {

                const auto& record =
                    symbol_records[
                        slot.symbol - 1];

                if (record.source_length ==
                        source_length &&
                    record.source_offset <=
                        source.size() &&
                    record.source_length <=
                        source.size() -
                            record.source_offset &&
                    source.substr(
                        record.source_offset,
                        record.source_length) ==
                        spelling) {

                    return local_symbol_id_v2{
                        slot.symbol};
                }
            }

            position =
                (position + 1) &
                mask;
        }

        return {};
    }

    void insert_index(
        std::vector<symbol_slot>& target,
        local_symbol_id_v2 symbol,
        std::uint32_t hash) const noexcept {

        const auto mask =
            target.size() - 1;

        auto position =
            static_cast<std::size_t>(
                hash) &
            mask;

        while (target[position].symbol != 0) {
            position =
                (position + 1) &
                mask;
        }

        target[position] = {
            hash,
            symbol.value(),
        };
    }

    [[nodiscard]] server_status ensure_index_capacity(
        std::size_t additional) noexcept {

        if (additional >
            (std::numeric_limits<std::size_t>::max)() -
                symbol_records.size()) {

            return server_status::io_error;
        }

        const auto required =
            symbol_records.size() +
            additional;

        if (!symbol_index.empty() &&
            required <=
                symbol_index.size() / 2) {

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
            std::vector<symbol_slot>
                candidate(capacity);

            for (std::size_t index = 0;
                 index < symbol_records.size();
                 ++index) {

                insert_index(
                    candidate,
                    local_symbol_id_v2{
                        static_cast<std::uint32_t>(
                            index + 1)},
                    symbol_records[index].hash);
            }

            symbol_index =
                std::move(candidate);

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    file_id source_file{};
    std::uint32_t source_size_value = 0;

    std::vector<lexical_symbol_record_v2>
        symbol_records;

    std::vector<std::uint32_t>
        identifier_symbols;

    std::vector<symbol_slot>
        symbol_index;
};

class lexical_symbol_resolution_v2 final {
public:
    [[nodiscard]] server_status reset(
        file_id file,
        std::size_t symbol_count) noexcept {

        if (!file ||
            symbol_count >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

            return server_status::
                project_configuration_invalid;
        }

        try {
            values.assign(
                symbol_count,
                string_id{});
        }
        catch (...) {
            return server_status::io_error;
        }

        source_file = file;
        return server_status::success;
    }

    [[nodiscard]] server_status set(
        local_symbol_id_v2 local,
        string_id global) noexcept {

        if (!source_file ||
            !local ||
            !global ||
            local.value() > values.size()) {

            return server_status::
                project_configuration_invalid;
        }

        values[
            local.value() - 1] =
                global;

        return server_status::success;
    }

    [[nodiscard]] string_id resolve(
        local_symbol_id_v2 local) const noexcept {

        return source_file &&
            local &&
            local.value() <= values.size()
            ? values[local.value() - 1]
            : string_id{};
    }

    [[nodiscard]] string_id resolve_identifier(
        const lexical_symbol_stream_v2& stream,
        std::size_t occurrence) const noexcept {

        return source_file ==
                stream.file()
            ? resolve(
                stream.identifier_symbol(
                    occurrence))
            : string_id{};
    }

    [[nodiscard]] file_id file() const noexcept {
        return source_file;
    }

    [[nodiscard]] bool complete() const noexcept {
        if (!source_file) {
            return false;
        }

        for (const auto value : values) {
            if (!value) {
                return false;
            }
        }

        return true;
    }

private:
    file_id source_file{};
    std::vector<string_id> values;
};

struct lexical_symbol_source_v2 final {
    const lexical_symbol_stream_v2* symbols = nullptr;
    std::string_view source;
};

// Single-owner deterministic merge. Parallel lexer lanes are complete before
// this function is called. Existing string IDs remain unchanged; new global
// spellings are interned in deterministic (hash,length,spelling) order.
[[nodiscard]] server_status merge_lexical_symbols_v2(
    std::span<const lexical_symbol_source_v2> sources,
    string_table& strings,
    std::vector<lexical_symbol_resolution_v2>& output) noexcept;

}
