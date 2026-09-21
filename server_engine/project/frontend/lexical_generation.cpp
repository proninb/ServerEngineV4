#include "lexical_generation.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t grown_capacity(
    std::size_t current,
    std::size_t required,
    std::size_t initial) noexcept {

    auto capacity =
        current == 0
            ? initial
            : current;

    while (capacity < required) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return required;
        }

        capacity *= 2;
    }

    return capacity;
}

}

server_status lexical_generation::ensure_records(
    std::size_t required) noexcept {

    if (required <= record_capacity) {
        if (record_count < required) {
            for (auto index = record_count;
                 index < required;
                 ++index) {

                records[index] = {};
                directive_records[index] = {};
            }

            record_count =
                required;
        }

        return server_status::success;
    }

    if (required >
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    const auto capacity =
        grown_capacity(
            record_capacity,
            required,
            16);

    std::unique_ptr<lexical_record[]> new_records{
        new (std::nothrow)
            lexical_record[capacity]};

    std::unique_ptr<lexical_directive_record[]> new_directives{
        new (std::nothrow)
            lexical_directive_record[capacity]};

    if (!new_records ||
        !new_directives) {

        return server_status::io_error;
    }

    if (record_count != 0) {
        std::copy_n(
            records.get(),
            record_count,
            new_records.get());

        std::copy_n(
            directive_records.get(),
            record_count,
            new_directives.get());
    }

    records =
        std::move(new_records);

    directive_records =
        std::move(new_directives);

    record_capacity =
        capacity;

    record_count =
        required;

    return server_status::success;
}

server_status lexical_generation::ensure_words(
    lexical_arena& arena,
    std::size_t required) noexcept {

    if (required <= arena.word_capacity) {
        return server_status::success;
    }

    const auto capacity =
        grown_capacity(
            arena.word_capacity,
            required,
            1024);

    std::unique_ptr<std::uint32_t[]> candidate{
        new (std::nothrow)
            std::uint32_t[capacity]};

    if (!candidate) {
        return server_status::io_error;
    }

    if (arena.word_count != 0) {
        std::copy_n(
            arena.words.get(),
            arena.word_count,
            candidate.get());
    }

    arena.words =
        std::move(candidate);

    arena.word_capacity =
        capacity;

    return server_status::success;
}

server_status lexical_generation::ensure_directives(
    lexical_arena& arena,
    std::size_t required) noexcept {

    if (required <=
        arena.directive_capacity) {

        return server_status::success;
    }

    const auto capacity =
        grown_capacity(
            arena.directive_capacity,
            required,
            64);

    std::unique_ptr<lexical_directive_anchor[]> candidate{
        new (std::nothrow)
            lexical_directive_anchor[capacity]};

    if (!candidate) {
        return server_status::io_error;
    }

    if (arena.directive_count != 0) {
        std::copy_n(
            arena.directives.get(),
            arena.directive_count,
            candidate.get());
    }

    arena.directives =
        std::move(candidate);

    arena.directive_capacity =
        capacity;

    return server_status::success;
}

server_status lexical_generation::reset(
    std::size_t file_count,
    std::size_t arena_count) noexcept {

    if (file_count >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)()) ||
        arena_count == 0 ||
        arena_count >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    std::unique_ptr<lexical_record[]> new_records;
    std::unique_ptr<lexical_directive_record[]> new_directives;

    if (file_count != 0) {
        new_records.reset(
            new (std::nothrow)
                lexical_record[file_count]);

        new_directives.reset(
            new (std::nothrow)
                lexical_directive_record[file_count]);

        if (!new_records ||
            !new_directives) {

            return server_status::io_error;
        }
    }

    std::unique_ptr<lexical_arena[]> new_arenas{
        new (std::nothrow)
            lexical_arena[arena_count]};

    if (!new_arenas) {
        return server_status::io_error;
    }

    records =
        std::move(new_records);

    directive_records =
        std::move(new_directives);

    record_count =
        file_count;

    record_capacity =
        file_count;

    arena_values =
        std::move(new_arenas);

    arena_count_value =
        arena_count;

    return server_status::success;
}

server_status lexical_generation::extend(
    std::size_t file_count) noexcept {

    if (file_count < record_count) {
        return server_status::project_configuration_invalid;
    }

    return ensure_records(
        file_count);
}

server_status lexical_generation::publish(
    file_id file,
    std::uint32_t arena_index,
    const lexical_stream& stream) noexcept {

    if (!file ||
        stream.file() != file ||
        static_cast<std::size_t>(
            arena_index) >=
            arena_count_value ||
        stream.word_count() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)()) ||
        stream.directive_count() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::project_configuration_invalid;
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() - 1);

    if (index >= record_count) {
        return server_status::project_artifact_invalid;
    }

    if (records[index].available()) {
        return server_status::project_configuration_invalid;
    }

    auto& arena =
        arena_values[arena_index];

    const auto values =
        stream.words();

    const auto anchors =
        stream.directives();

    const auto max_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (arena.word_count > max_u32 ||
        values.size() >
            max_u32 - arena.word_count ||
        arena.directive_count > max_u32 ||
        anchors.size() >
            max_u32 - arena.directive_count) {

        return server_status::io_error;
    }

    const auto required_words =
        arena.word_count +
        values.size();

    const auto required_directives =
        arena.directive_count +
        anchors.size();

    const auto words_ready =
        ensure_words(
            arena,
            required_words);

    if (!succeeded(words_ready)) {
        return words_ready;
    }

    const auto directives_ready =
        ensure_directives(
            arena,
            required_directives);

    if (!succeeded(directives_ready)) {
        return directives_ready;
    }

    const auto word_offset =
        static_cast<std::uint32_t>(
            arena.word_count);

    const auto directive_offset =
        static_cast<std::uint32_t>(
            arena.directive_count);

    if (!values.empty()) {
        std::copy(
            values.begin(),
            values.end(),
            arena.words.get() +
                arena.word_count);
    }

    if (!anchors.empty()) {
        std::copy(
            anchors.begin(),
            anchors.end(),
            arena.directives.get() +
                arena.directive_count);
    }

    arena.word_count =
        required_words;

    arena.directive_count =
        required_directives;

    records[index] = {
        arena_index,
        word_offset,
        static_cast<std::uint32_t>(
            values.size()),
        stream.token_count(),
    };

    directive_records[index] = {
        directive_offset,
        static_cast<std::uint32_t>(
            anchors.size()),
    };

    return server_status::success;
}

bool lexical_generation::contains(
    file_id file) const noexcept {

    if (!file) {
        return false;
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() - 1);

    return index < record_count &&
        records[index].available();
}

std::span<const std::uint32_t>
lexical_generation::words(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    const auto& record =
        records[
            file.value() - 1];

    if (record.word_count == 0) {
        return {};
    }

    if (record.arena >=
        arena_count_value) {

        return {};
    }

    const auto& arena =
        arena_values[
            record.arena];

    if (record.word_offset >
            arena.word_count ||
        record.word_count >
            arena.word_count -
                record.word_offset) {

        return {};
    }

    return {
        arena.words.get() +
            record.word_offset,
        record.word_count,
    };
}

std::span<const lexical_directive_anchor>
lexical_generation::directives(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() - 1);

    const auto& record =
        records[index];

    const auto& directive =
        directive_records[index];

    if (directive.count == 0) {
        return {};
    }

    if (record.arena >=
        arena_count_value) {

        return {};
    }

    const auto& arena =
        arena_values[
            record.arena];

    if (directive.offset >
            arena.directive_count ||
        directive.count >
            arena.directive_count -
                directive.offset) {

        return {};
    }

    return {
        arena.directives.get() +
            directive.offset,
        directive.count,
    };
}

std::uint32_t lexical_generation::token_count(
    file_id file) const noexcept {

    if (!contains(file)) {
        return 0;
    }

    return records[
        file.value() - 1]
        .token_count;
}

}
