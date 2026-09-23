#include "database.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'C'}, std::byte{'W'}, std::byte{'D'}, std::byte{'B'},
    std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'3'},
};

constexpr std::uint32_t format_version = 3;
constexpr std::uint32_t section_count = 3;
constexpr std::size_t header_size = 16;
constexpr std::size_t section_record_size = 24;
constexpr std::size_t section_table_size =
    section_count * section_record_size;
constexpr std::size_t payload_offset =
    header_size + section_table_size;
constexpr std::size_t checksum_size = 32;

constexpr std::uint32_t lexical_available_flag =
    0x00000001u;
constexpr std::uint32_t lexical_known_flags =
    lexical_available_flag;

enum class section_kind : std::uint32_t {
    lexical_records = 1,
    lexical_words = 2,
    lexical_directives = 3,
};

struct section_record final {
    section_kind kind =
        section_kind::lexical_records;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct persisted_lexical_record final {
    std::uint32_t flags = 0;
    std::uint32_t word_offset = 0;
    std::uint32_t word_count = 0;
    std::uint32_t directive_offset = 0;
    std::uint32_t directive_count = 0;
    std::uint32_t token_count = 0;
};

static_assert(sizeof(persisted_lexical_record) == 24);

[[nodiscard]] bool write_u32(
    std::span<std::byte> output,
    std::size_t& offset,
    std::uint32_t value) noexcept {

    if (offset > output.size() ||
        output.size() - offset < 4) {
        return false;
    }

    for (std::size_t index = 0; index < 4; ++index) {
        output[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8)) & 0xffU);
    }

    offset += 4;
    return true;
}

[[nodiscard]] bool write_u64(
    std::span<std::byte> output,
    std::size_t& offset,
    std::uint64_t value) noexcept {

    if (offset > output.size() ||
        output.size() - offset < 8) {
        return false;
    }

    for (std::size_t index = 0; index < 8; ++index) {
        output[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8)) & 0xffU);
    }

    offset += 8;
    return true;
}

[[nodiscard]] bool write_bytes(
    std::span<std::byte> output,
    std::size_t& offset,
    const std::byte* data,
    std::size_t size) noexcept {

    if (offset > output.size() ||
        size > output.size() - offset ||
        (size != 0 && data == nullptr)) {
        return false;
    }

    if (size != 0) {
        std::copy_n(
            data,
            size,
            output.begin() +
                static_cast<std::ptrdiff_t>(offset));
    }

    offset += size;
    return true;
}

[[nodiscard]] bool read_u32(
    std::span<const std::byte> input,
    std::size_t& offset,
    std::uint32_t& output) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 4) {
        return false;
    }

    output = 0;

    for (std::size_t index = 0; index < 4; ++index) {
        output |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 4;
    return true;
}

[[nodiscard]] bool read_u64(
    std::span<const std::byte> input,
    std::size_t& offset,
    std::uint64_t& output) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 8) {
        return false;
    }

    output = 0;

    for (std::size_t index = 0; index < 8; ++index) {
        output |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 8;
    return true;
}

[[nodiscard]] bool add_size(
    std::size_t& value,
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            value) {
        return false;
    }

    value += additional;
    return true;
}

[[nodiscard]] bool multiply_size(
    std::size_t left,
    std::size_t right,
    std::size_t& output) noexcept {

    if (left != 0 &&
        right >
            (std::numeric_limits<std::size_t>::max)() /
                left) {
        return false;
    }

    output = left * right;
    return true;
}

[[nodiscard]] bool fits_u32(
    std::size_t value) noexcept {

    return value <=
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());
}

[[nodiscard]] constexpr bool lexical_kind(
    file_kind kind) noexcept {

    return kind == file_kind::header ||
        kind == file_kind::source;
}

[[nodiscard]] file_content_hash checksum(
    std::span<const std::byte> bytes) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                bytes.data()),
            bytes.size()});
}

[[nodiscard]] bool write_section_record(
    std::span<std::byte> output,
    std::size_t& offset,
    section_kind kind,
    std::uint64_t section_offset,
    std::uint64_t section_size) noexcept {

    return write_u32(
            output,
            offset,
            static_cast<std::uint32_t>(kind)) &&
        write_u32(output, offset, 0) &&
        write_u64(output, offset, section_offset) &&
        write_u64(output, offset, section_size);
}

[[nodiscard]] bool read_section_record(
    std::span<const std::byte> input,
    std::size_t& offset,
    section_record& output) noexcept {

    std::uint32_t kind = 0;
    std::uint32_t reserved = 0;

    if (!read_u32(input, offset, kind) ||
        !read_u32(input, offset, reserved) ||
        reserved != 0 ||
        !read_u64(input, offset, output.offset) ||
        !read_u64(input, offset, output.size)) {
        return false;
    }

    if (kind <
            static_cast<std::uint32_t>(
                section_kind::lexical_records) ||
        kind >
            static_cast<std::uint32_t>(
                section_kind::lexical_directives)) {
        return false;
    }

    output.kind =
        static_cast<section_kind>(kind);

    return true;
}

[[nodiscard]] bool decode_lexical_record(
    std::span<const std::byte> image,
    std::size_t records_offset,
    std::uint32_t file_count,
    std::uint32_t index,
    persisted_lexical_record& output) noexcept {

    output = {};

    if (index >= file_count) {
        return false;
    }

    std::size_t offset =
        records_offset +
        16 +
        static_cast<std::size_t>(index) *
            sizeof(persisted_lexical_record);

    return read_u32(image, offset, output.flags) &&
        read_u32(image, offset, output.word_offset) &&
        read_u32(image, offset, output.word_count) &&
        read_u32(image, offset, output.directive_offset) &&
        read_u32(image, offset, output.directive_count) &&
        read_u32(image, offset, output.token_count);
}

}

void database_view::reset() noexcept {
    *this = {};
}

database_image_result database_view::bind(
    std::span<const std::byte> image) noexcept {

    reset();

    if (image.size() <
        payload_offset + checksum_size) {
        return database_image_result::invalid_image;
    }

    if (!std::equal(
            magic.begin(),
            magic.end(),
            image.begin())) {
        return database_image_result::invalid_image;
    }

    std::size_t offset =
        magic.size();

    std::uint32_t version = 0;
    std::uint32_t persisted_section_count = 0;

    if (!read_u32(
            image,
            offset,
            version) ||
        version != format_version ||
        !read_u32(
            image,
            offset,
            persisted_section_count) ||
        persisted_section_count !=
            section_count ||
        offset != header_size) {
        return database_image_result::invalid_image;
    }

    std::array<section_record, section_count>
        sections{};

    const auto payload_size =
        image.size() - checksum_size;

    std::uint64_t expected_offset =
        payload_offset;

    for (std::size_t index = 0;
         index < sections.size();
         ++index) {

        if (!read_section_record(
                image,
                offset,
                sections[index]) ||
            sections[index].kind !=
                static_cast<section_kind>(
                    index + 1) ||
            sections[index].offset !=
                expected_offset ||
            sections[index].offset >
                payload_size ||
            sections[index].size >
                static_cast<std::uint64_t>(
                    payload_size) -
                    sections[index].offset) {
            return database_image_result::invalid_image;
        }

        expected_offset +=
            sections[index].size;
    }

    if (offset != payload_offset ||
        expected_offset != payload_size) {
        return database_image_result::invalid_image;
    }

    const auto& records =
        sections[
            static_cast<std::size_t>(
                section_kind::lexical_records) - 1];

    const auto& words =
        sections[
            static_cast<std::size_t>(
                section_kind::lexical_words) - 1];

    const auto& directives =
        sections[
            static_cast<std::size_t>(
                section_kind::lexical_directives) - 1];

    if (records.size < 16) {
        return database_image_result::invalid_image;
    }

    std::size_t records_header_offset =
        static_cast<std::size_t>(
            records.offset);

    std::uint32_t file_count = 0;
    std::uint32_t word_count = 0;
    std::uint32_t directive_count = 0;
    std::uint32_t reserved = 0;

    if (!read_u32(
            image,
            records_header_offset,
            file_count) ||
        file_count == 0 ||
        !read_u32(
            image,
            records_header_offset,
            word_count) ||
        !read_u32(
            image,
            records_header_offset,
            directive_count) ||
        !read_u32(
            image,
            records_header_offset,
            reserved) ||
        reserved != 0) {
        return database_image_result::invalid_image;
    }

    std::size_t record_payload_size = 0;
    std::size_t expected_records_size = 16;
    std::size_t word_bytes = 0;
    std::size_t directive_bytes = 0;

    if (!multiply_size(
            file_count,
            sizeof(persisted_lexical_record),
            record_payload_size) ||
        !add_size(
            expected_records_size,
            record_payload_size) ||
        !multiply_size(
            word_count,
            sizeof(std::uint32_t),
            word_bytes) ||
        !multiply_size(
            directive_count,
            sizeof(lexical_directive_anchor),
            directive_bytes) ||
        records.size !=
            expected_records_size ||
        words.size !=
            word_bytes ||
        directives.size !=
            directive_bytes) {
        return database_image_result::invalid_image;
    }

    bytes = image;
    lexical_records_offset =
        static_cast<std::size_t>(
            records.offset);
    lexical_words_offset =
        static_cast<std::size_t>(
            words.offset);
    lexical_directives_offset =
        static_cast<std::size_t>(
            directives.offset);
    file_count_value = file_count;
    word_count_value = word_count;
    directive_count_value =
        directive_count;

    return database_image_result::success;
}

bool database_view::contains(
    file_id file) const noexcept {

    return valid() &&
        file &&
        file.value() <=
            file_count_value;
}

bool database_view::file(
    file_id id,
    database_lexical_file_view& output) const noexcept {

    output = {};

    if (!contains(id)) {
        return false;
    }

    persisted_lexical_record record;

    if (!decode_lexical_record(
            bytes,
            lexical_records_offset,
            file_count_value,
            id.value() - 1,
            record) ||
        (record.flags &
            ~lexical_known_flags) != 0) {
        return false;
    }

    const auto available =
        (record.flags &
            lexical_available_flag) != 0;

    if (!available) {
        return record.word_offset == 0 &&
            record.word_count == 0 &&
            record.directive_offset == 0 &&
            record.directive_count == 0 &&
            record.token_count == 0;
    }

    if (record.word_offset >
            word_count_value ||
        record.word_count >
            word_count_value -
                record.word_offset ||
        record.directive_offset >
            directive_count_value ||
        record.directive_count >
            directive_count_value -
                record.directive_offset ||
        record.token_count >
            record.word_count) {
        return false;
    }

    output.available = true;
    output.token_count =
        record.token_count;

    const auto word_begin =
        lexical_words_offset +
        static_cast<std::size_t>(
            record.word_offset) *
            sizeof(std::uint32_t);

    output.words =
        lexical_word_view::from_encoded(
            bytes.subspan(
                word_begin,
                static_cast<std::size_t>(
                    record.word_count) *
                    sizeof(std::uint32_t)));

    const auto directive_begin =
        lexical_directives_offset +
        static_cast<std::size_t>(
            record.directive_offset) *
            sizeof(lexical_directive_anchor);

    output.directives =
        lexical_directive_view::from_encoded(
            bytes.subspan(
                directive_begin,
                static_cast<std::size_t>(
                    record.directive_count) *
                    sizeof(lexical_directive_anchor)));

    return true;
}

lexical_baseline_view database_view::lexical_baseline() const noexcept {
    lexical_baseline_view output;

    if (!valid()) {
        return output;
    }

    output.context = this;
    output.file_count_value = file_count_value;
    output.reader = read_lexical_baseline;
    return output;
}

bool database_view::read_lexical_baseline(
    const void* context,
    file_id file,
    lexical_file_view& output) noexcept {

    output = {};

    return context != nullptr &&
        static_cast<const database_view*>(context)->file(file, output);
}

database_image_result prepare_database_layout(
    const file_context& files,
    const lexical_generation& lexical,
    database_layout& output) noexcept {

    output.reset();

    if (files.size() == 0 ||
        lexical.size() != files.size() ||
        !fits_u32(files.size())) {
        return database_image_result::invalid_state;
    }

    const auto file_count =
        static_cast<std::uint32_t>(
            files.size());

    std::uint32_t word_count = 0;
    std::uint32_t directive_count = 0;

    for (std::uint32_t value = 1;
         value <= file_count;
         ++value) {

        const file_id file{value};
        const auto available =
            lexical.contains(file);

        if (available !=
            lexical_kind(
                files.kind(file))) {
            return database_image_result::invalid_state;
        }

        if (!available) {
            continue;
        }

        const auto words =
            lexical.words(file);
        const auto directives =
            lexical.directives(file);

        if (!fits_u32(words.size()) ||
            !fits_u32(directives.size()) ||
            words.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    word_count ||
            directives.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    directive_count ||
            lexical.token_count(file) >
                words.size()) {
            return database_image_result::invalid_state;
        }

        for (const auto& anchor :
             directives) {
            if (anchor.word_offset >=
                words.size()) {
                return database_image_result::invalid_state;
            }
        }

        word_count +=
            static_cast<std::uint32_t>(
                words.size());
        directive_count +=
            static_cast<std::uint32_t>(
                directives.size());
    }

    std::size_t lexical_records_payload = 0;
    std::size_t lexical_words_size = 0;
    std::size_t lexical_directives_size = 0;

    if (!multiply_size(
            file_count,
            sizeof(persisted_lexical_record),
            lexical_records_payload) ||
        !multiply_size(
            word_count,
            sizeof(std::uint32_t),
            lexical_words_size) ||
        !multiply_size(
            directive_count,
            sizeof(lexical_directive_anchor),
            lexical_directives_size)) {
        return database_image_result::failed;
    }

    std::size_t lexical_records_size = 16;

    if (!add_size(
            lexical_records_size,
            lexical_records_payload)) {
        return database_image_result::failed;
    }

    std::size_t cursor =
        payload_offset;

    output.lexical_records_offset =
        cursor;
    output.lexical_records_size =
        lexical_records_size;

    if (!add_size(
            cursor,
            lexical_records_size)) {
        output.reset();
        return database_image_result::failed;
    }

    output.lexical_words_offset =
        cursor;
    output.lexical_words_size =
        lexical_words_size;

    if (!add_size(
            cursor,
            lexical_words_size)) {
        output.reset();
        return database_image_result::failed;
    }

    output.lexical_directives_offset =
        cursor;
    output.lexical_directives_size =
        lexical_directives_size;

    if (!add_size(
            cursor,
            lexical_directives_size)) {
        output.reset();
        return database_image_result::failed;
    }

    output.checksum_offset =
        cursor;

    if (!add_size(
            cursor,
            checksum_size)) {
        output.reset();
        return database_image_result::failed;
    }

    output.size_value = cursor;
    output.file_count = file_count;
    output.word_count = word_count;
    output.directive_count =
        directive_count;

    return database_image_result::success;
}

database_image_result encode_database_image(
    const file_context& files,
    const lexical_generation& lexical,
    const database_layout& layout,
    std::span<std::byte> output) noexcept {

    if (layout.size_value == 0 ||
        output.size() !=
            layout.size_value ||
        files.size() !=
            layout.file_count ||
        lexical.size() !=
            layout.file_count ||
        layout.lexical_records_offset !=
            payload_offset ||
        layout.checksum_offset >
            output.size() ||
        output.size() -
            layout.checksum_offset !=
                checksum_size) {
        return database_image_result::invalid_state;
    }

    std::size_t header_cursor = 0;

    if (!write_bytes(
            output,
            header_cursor,
            magic.data(),
            magic.size()) ||
        !write_u32(
            output,
            header_cursor,
            format_version) ||
        !write_u32(
            output,
            header_cursor,
            section_count) ||
        header_cursor !=
            header_size) {
        return database_image_result::failed;
    }

    std::size_t table_cursor =
        header_size;

    if (!write_section_record(
            output,
            table_cursor,
            section_kind::lexical_records,
            layout.lexical_records_offset,
            layout.lexical_records_size) ||
        !write_section_record(
            output,
            table_cursor,
            section_kind::lexical_words,
            layout.lexical_words_offset,
            layout.lexical_words_size) ||
        !write_section_record(
            output,
            table_cursor,
            section_kind::lexical_directives,
            layout.lexical_directives_offset,
            layout.lexical_directives_size) ||
        table_cursor !=
            payload_offset) {
        return database_image_result::failed;
    }

    std::size_t lexical_record_cursor =
        layout.lexical_records_offset;

    if (!write_u32(
            output,
            lexical_record_cursor,
            layout.file_count) ||
        !write_u32(
            output,
            lexical_record_cursor,
            layout.word_count) ||
        !write_u32(
            output,
            lexical_record_cursor,
            layout.directive_count) ||
        !write_u32(
            output,
            lexical_record_cursor,
            0)) {
        return database_image_result::failed;
    }

    std::size_t word_cursor =
        layout.lexical_words_offset;
    std::size_t directive_cursor =
        layout.lexical_directives_offset;

    std::uint32_t word_offset = 0;
    std::uint32_t directive_offset = 0;

    for (std::uint32_t value = 1;
         value <= layout.file_count;
         ++value) {

        const file_id file{value};
        const auto available =
            lexical.contains(file);

        if (available !=
            lexical_kind(
                files.kind(file))) {
            return database_image_result::invalid_state;
        }

        if (!available) {
            for (std::size_t index = 0;
                 index < 6;
                 ++index) {
                if (!write_u32(
                        output,
                        lexical_record_cursor,
                        0)) {
                    return database_image_result::failed;
                }
            }

            continue;
        }

        const auto words =
            lexical.words(file);
        const auto directives =
            lexical.directives(file);

        if (!fits_u32(words.size()) ||
            !fits_u32(directives.size()) ||
            words.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    word_offset ||
            directives.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    directive_offset ||
            lexical.token_count(file) >
                words.size() ||
            !write_u32(
                output,
                lexical_record_cursor,
                lexical_available_flag) ||
            !write_u32(
                output,
                lexical_record_cursor,
                word_offset) ||
            !write_u32(
                output,
                lexical_record_cursor,
                static_cast<std::uint32_t>(
                    words.size())) ||
            !write_u32(
                output,
                lexical_record_cursor,
                directive_offset) ||
            !write_u32(
                output,
                lexical_record_cursor,
                static_cast<std::uint32_t>(
                    directives.size())) ||
            !write_u32(
                output,
                lexical_record_cursor,
                lexical.token_count(file))) {
            return database_image_result::invalid_state;
        }

        for (const auto word :
             words) {
            if (!write_u32(
                    output,
                    word_cursor,
                    word)) {
                return database_image_result::failed;
            }
        }

        for (const auto& anchor :
             directives) {
            if (anchor.word_offset >=
                    words.size() ||
                !write_u32(
                    output,
                    directive_cursor,
                    anchor.word_offset) ||
                !write_u32(
                    output,
                    directive_cursor,
                    anchor.source_base)) {
                return database_image_result::invalid_state;
            }
        }

        word_offset +=
            static_cast<std::uint32_t>(
                words.size());
        directive_offset +=
            static_cast<std::uint32_t>(
                directives.size());
    }

    if (word_offset !=
            layout.word_count ||
        directive_offset !=
            layout.directive_count ||
        lexical_record_cursor !=
            layout.lexical_records_offset +
                layout.lexical_records_size ||
        word_cursor !=
            layout.lexical_words_offset +
                layout.lexical_words_size ||
        directive_cursor !=
            layout.lexical_directives_offset +
                layout.lexical_directives_size) {
        return database_image_result::invalid_state;
    }

    const auto digest =
        checksum(
            output.first(
                layout.checksum_offset));

    std::size_t checksum_cursor =
        layout.checksum_offset;

    if (!write_bytes(
            output,
            checksum_cursor,
            digest.bytes.data(),
            digest.bytes.size()) ||
        checksum_cursor !=
            output.size()) {
        return database_image_result::failed;
    }

    return database_image_result::success;
}

database_image_result validate_database_image(
    std::span<const std::byte> image) noexcept {

    database_view view;

    if (view.bind(image) !=
        database_image_result::success) {
        return database_image_result::invalid_image;
    }

    const auto payload_size =
        image.size() - checksum_size;

    const auto expected =
        checksum(
            image.first(
                payload_size));

    if (!std::equal(
            expected.bytes.begin(),
            expected.bytes.end(),
            image.begin() +
                static_cast<std::ptrdiff_t>(
                    payload_size))) {
        return database_image_result::invalid_image;
    }

    std::uint32_t expected_word_offset = 0;
    std::uint32_t expected_directive_offset = 0;

    for (std::uint32_t index = 0;
         index < view.file_count_value;
         ++index) {

        persisted_lexical_record record;

        if (!decode_lexical_record(
                image,
                view.lexical_records_offset,
                view.file_count_value,
                index,
                record) ||
            (record.flags &
                ~lexical_known_flags) != 0) {
            return database_image_result::invalid_image;
        }

        const auto available =
            (record.flags &
                lexical_available_flag) != 0;

        if (!available) {
            if (record.word_offset != 0 ||
                record.word_count != 0 ||
                record.directive_offset != 0 ||
                record.directive_count != 0 ||
                record.token_count != 0) {
                return database_image_result::invalid_image;
            }

            continue;
        }

        if (record.word_offset !=
                expected_word_offset ||
            record.directive_offset !=
                expected_directive_offset ||
            record.word_count >
                view.word_count_value -
                    expected_word_offset ||
            record.directive_count >
                view.directive_count_value -
                    expected_directive_offset ||
            record.token_count >
                record.word_count) {
            return database_image_result::invalid_image;
        }

        database_lexical_file_view state;

        if (!view.file(
                file_id{index + 1},
                state) ||
            !state.available) {
            return database_image_result::invalid_image;
        }

        for (std::size_t directive = 0;
             directive <
                state.directives.size();
             ++directive) {
            if (state.directives[
                    directive].word_offset >=
                state.words.size()) {
                return database_image_result::invalid_image;
            }
        }

        expected_word_offset +=
            record.word_count;
        expected_directive_offset +=
            record.directive_count;
    }

    if (expected_word_offset !=
            view.word_count_value ||
        expected_directive_offset !=
            view.directive_count_value) {
        return database_image_result::invalid_image;
    }

    return database_image_result::success;
}

database_image_result verify_database_image(
    std::span<const std::byte> image,
    const file_context& files,
    const lexical_generation& lexical) noexcept {

    const auto validated =
        validate_database_image(image);

    if (validated !=
        database_image_result::success) {
        return validated;
    }

    database_view view;

    if (view.bind(image) !=
            database_image_result::success ||
        files.size() !=
            view.file_count() ||
        lexical.size() !=
            view.file_count()) {
        return database_image_result::invalid_image;
    }

    for (std::uint32_t value = 1;
         value <= view.file_count();
         ++value) {

        const file_id file{value};
        database_lexical_file_view persisted;

        if (!view.file(
                file,
                persisted)) {
            return database_image_result::invalid_image;
        }

        const auto available =
            lexical.contains(file);

        if (available !=
                lexical_kind(
                    files.kind(file)) ||
            persisted.available !=
                available) {
            return database_image_result::invalid_image;
        }

        if (!available) {
            continue;
        }

        const auto words =
            lexical.words(file);
        const auto directives =
            lexical.directives(file);

        if (persisted.words.size() !=
                words.size() ||
            persisted.directives.size() !=
                directives.size() ||
            persisted.token_count !=
                lexical.token_count(file)) {
            return database_image_result::invalid_image;
        }

        for (std::size_t index = 0;
             index < words.size();
             ++index) {
            if (persisted.words[index] !=
                words[index]) {
                return database_image_result::invalid_image;
            }
        }

        for (std::size_t index = 0;
             index < directives.size();
             ++index) {
            const auto current =
                persisted.directives[index];

            if (current.word_offset !=
                    directives[index].word_offset ||
                current.source_base !=
                    directives[index].source_base) {
                return database_image_result::invalid_image;
            }
        }
    }

    return database_image_result::success;
}

}
