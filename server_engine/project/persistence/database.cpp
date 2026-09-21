#include "database.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'C'}, std::byte{'W'}, std::byte{'D'}, std::byte{'B'},
    std::byte{'0'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'},
};

constexpr std::uint32_t format_version = 1;
constexpr std::uint32_t section_count = 4;
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
    strings = 1,
    lexical_records = 2,
    lexical_words = 3,
    lexical_directives = 4,
};

struct section_record final {
    section_kind kind = section_kind::strings;
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

void append_u32(
    std::vector<std::byte>& output,
    std::uint32_t value) {

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_u64(
    std::vector<std::byte>& output,
    std::uint64_t value) {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_bytes(
    std::vector<std::byte>& output,
    const std::byte* data,
    std::size_t size) {

    output.insert(
        output.end(),
        data,
        data + size);
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

    for (std::size_t index = 0;
         index < 4;
         ++index) {

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

    for (std::size_t index = 0;
         index < 8;
         ++index) {

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

[[nodiscard]] file_content_hash checksum(
    std::span<const std::byte> bytes) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                bytes.data()),
            bytes.size()});
}

[[nodiscard]] constexpr bool lexical_kind(
    file_kind kind) noexcept {

    return kind == file_kind::header ||
        kind == file_kind::source;
}

[[nodiscard]] bool fits_u32(
    std::size_t value) noexcept {

    return value <=
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());
}

void append_section_record(
    std::vector<std::byte>& output,
    section_kind kind,
    std::uint64_t offset,
    std::uint64_t size) {

    append_u32(
        output,
        static_cast<std::uint32_t>(
            kind));

    append_u32(
        output,
        0);

    append_u64(
        output,
        offset);

    append_u64(
        output,
        size);
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
        !read_u64(
            input,
            offset,
            output.offset) ||
        !read_u64(
            input,
            offset,
            output.size)) {

        return false;
    }

    if (kind <
            static_cast<std::uint32_t>(
                section_kind::strings) ||
        kind >
            static_cast<std::uint32_t>(
                section_kind::lexical_directives)) {

        return false;
    }

    output.kind =
        static_cast<section_kind>(
            kind);

    return true;
}

}

database_image_result build_database_image(
    const file_context& files,
    const string_table& strings,
    const lexical_generation& lexical,
    project_artifact_image& output) noexcept {

    output = {};

    if (files.size() == 0 ||
        lexical.size() != files.size() ||
        !fits_u32(files.size()) ||
        !fits_u32(strings.size())) {

        return database_image_result::
            invalid_state;
    }

    try {
        std::vector<std::byte> string_section;

        const auto string_count =
            static_cast<std::uint32_t>(
                strings.size());

        std::uint32_t string_bytes = 0;

        string_section.reserve(
            8 +
            static_cast<std::size_t>(
                string_count) *
                8);

        append_u32(
            string_section,
            string_count);

        append_u32(
            string_section,
            0);

        std::vector<std::string_view>
            spellings;

        spellings.reserve(
            string_count);

        for (std::uint32_t slot = 1;
             slot <= string_count;
             ++slot) {

            const auto spelling =
                strings.spelling(slot);

            if (spelling.empty() ||
                spelling.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()) -
                        string_bytes) {

                return database_image_result::
                    invalid_state;
            }

            append_u32(
                string_section,
                string_bytes);

            append_u32(
                string_section,
                static_cast<std::uint32_t>(
                    spelling.size()));

            string_bytes +=
                static_cast<std::uint32_t>(
                    spelling.size());

            spellings.push_back(
                spelling);
        }

        for (const auto spelling :
             spellings) {

            append_bytes(
                string_section,
                reinterpret_cast<const std::byte*>(
                    spelling.data()),
                spelling.size());
        }

        string_section[4] =
            static_cast<std::byte>(
                string_bytes & 0xffU);

        string_section[5] =
            static_cast<std::byte>(
                (string_bytes >> 8) & 0xffU);

        string_section[6] =
            static_cast<std::byte>(
                (string_bytes >> 16) & 0xffU);

        string_section[7] =
            static_cast<std::byte>(
                (string_bytes >> 24) & 0xffU);

        std::vector<std::byte>
            lexical_record_section;

        const auto file_count =
            static_cast<std::uint32_t>(
                files.size());

        std::uint32_t word_count = 0;
        std::uint32_t directive_count = 0;

        lexical_record_section.reserve(
            16 +
            static_cast<std::size_t>(
                file_count) *
                sizeof(persisted_lexical_record));

        append_u32(
            lexical_record_section,
            file_count);

        append_u32(
            lexical_record_section,
            0);

        append_u32(
            lexical_record_section,
            0);

        append_u32(
            lexical_record_section,
            0);

        std::vector<std::uint32_t>
            words;

        std::vector<lexical_directive_anchor>
            directives;

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            const file_id file{value};

            const auto available =
                lexical.contains(file);

            if (available !=
                lexical_kind(
                    files.kind(file))) {

                return database_image_result::
                    invalid_state;
            }

            if (!available) {
                for (std::size_t index = 0;
                     index < 6;
                     ++index) {

                    append_u32(
                        lexical_record_section,
                        0);
                }

                continue;
            }

            const auto file_words =
                lexical.words(file);

            const auto file_directives =
                lexical.directives(file);

            if (!fits_u32(
                    file_words.size()) ||
                !fits_u32(
                    file_directives.size()) ||
                file_words.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()) -
                        word_count ||
                file_directives.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()) -
                        directive_count ||
                lexical.token_count(file) >
                    file_words.size()) {

                return database_image_result::
                    invalid_state;
            }

            for (const auto& anchor :
                 file_directives) {

                if (anchor.word_offset >=
                    file_words.size()) {

                    return database_image_result::
                        invalid_state;
                }
            }

            append_u32(
                lexical_record_section,
                lexical_available_flag);

            append_u32(
                lexical_record_section,
                word_count);

            append_u32(
                lexical_record_section,
                static_cast<std::uint32_t>(
                    file_words.size()));

            append_u32(
                lexical_record_section,
                directive_count);

            append_u32(
                lexical_record_section,
                static_cast<std::uint32_t>(
                    file_directives.size()));

            append_u32(
                lexical_record_section,
                lexical.token_count(file));

            words.insert(
                words.end(),
                file_words.begin(),
                file_words.end());

            directives.insert(
                directives.end(),
                file_directives.begin(),
                file_directives.end());

            word_count +=
                static_cast<std::uint32_t>(
                    file_words.size());

            directive_count +=
                static_cast<std::uint32_t>(
                    file_directives.size());
        }

        auto write_u32_at =
            [](std::vector<std::byte>& target,
               std::size_t offset,
               std::uint32_t value) noexcept {

            target[offset + 0] =
                static_cast<std::byte>(
                    value & 0xffU);

            target[offset + 1] =
                static_cast<std::byte>(
                    (value >> 8) & 0xffU);

            target[offset + 2] =
                static_cast<std::byte>(
                    (value >> 16) & 0xffU);

            target[offset + 3] =
                static_cast<std::byte>(
                    (value >> 24) & 0xffU);
        };

        write_u32_at(
            lexical_record_section,
            4,
            word_count);

        write_u32_at(
            lexical_record_section,
            8,
            directive_count);

        std::vector<std::byte>
            word_section;

        word_section.reserve(
            static_cast<std::size_t>(
                word_count) *
                sizeof(std::uint32_t));

        for (const auto word :
             words) {

            append_u32(
                word_section,
                word);
        }

        std::vector<std::byte>
            directive_section;

        directive_section.reserve(
            static_cast<std::size_t>(
                directive_count) *
                sizeof(lexical_directive_anchor));

        for (const auto& anchor :
             directives) {

            append_u32(
                directive_section,
                anchor.word_offset);

            append_u32(
                directive_section,
                anchor.source_base);
        }

        const std::array<
            std::span<const std::byte>,
            section_count>
            sections{
                std::span<const std::byte>{
                    string_section},
                std::span<const std::byte>{
                    lexical_record_section},
                std::span<const std::byte>{
                    word_section},
                std::span<const std::byte>{
                    directive_section},
            };

        std::size_t total_size =
            payload_offset;

        for (const auto section :
             sections) {

            if (!add_size(
                    total_size,
                    section.size())) {

                return database_image_result::
                    failed;
            }
        }

        if (!add_size(
                total_size,
                checksum_size)) {

            return database_image_result::
                failed;
        }

        output.bytes.reserve(
            total_size);

        append_bytes(
            output.bytes,
            magic.data(),
            magic.size());

        append_u32(
            output.bytes,
            format_version);

        append_u32(
            output.bytes,
            section_count);

        std::uint64_t section_offset =
            payload_offset;

        for (std::size_t index = 0;
             index < sections.size();
             ++index) {

            append_section_record(
                output.bytes,
                static_cast<section_kind>(
                    index + 1),
                section_offset,
                sections[index].size());

            section_offset +=
                sections[index].size();
        }

        for (const auto section :
             sections) {

            append_bytes(
                output.bytes,
                section.data(),
                section.size());
        }

        const auto digest =
            checksum(
                std::span<const std::byte>{
                    output.bytes});

        append_bytes(
            output.bytes,
            digest.bytes.data(),
            digest.bytes.size());

        if (output.bytes.size() !=
            total_size) {

            output = {};
            return database_image_result::
                failed;
        }

        finalize_project_artifact_image(
            output);

        return database_image_result::
            success;
    }
    catch (...) {
        output = {};

        return database_image_result::
            failed;
    }
}

database_image_result validate_database_image(
    std::span<const std::byte> image) noexcept {

    if (image.size() <
        payload_offset +
        checksum_size) {

        return database_image_result::
            invalid_image;
    }

    if (!std::equal(
            magic.begin(),
            magic.end(),
            image.begin())) {

        return database_image_result::
            invalid_image;
    }

    const auto payload_size =
        image.size() -
        checksum_size;

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

        return database_image_result::
            invalid_image;
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

        return database_image_result::
            invalid_image;
    }

    std::array<section_record, section_count>
        sections{};

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
            sections[index].size >
                static_cast<std::uint64_t>(
                    payload_size) -
                    expected_offset) {

            return database_image_result::
                invalid_image;
        }

        expected_offset +=
            sections[index].size;
    }

    if (offset != payload_offset ||
        expected_offset !=
            payload_size) {

        return database_image_result::
            invalid_image;
    }

    const auto section_span =
        [&](section_kind kind)
        -> std::span<const std::byte> {

        const auto& section =
            sections[
                static_cast<std::size_t>(
                    kind) - 1];

        return image.subspan(
            static_cast<std::size_t>(
                section.offset),
            static_cast<std::size_t>(
                section.size));
    };

    const auto strings =
        section_span(
            section_kind::strings);

    std::size_t strings_offset = 0;
    std::uint32_t string_count = 0;
    std::uint32_t string_bytes = 0;

    if (!read_u32(
            strings,
            strings_offset,
            string_count) ||
        !read_u32(
            strings,
            strings_offset,
            string_bytes)) {

        return database_image_result::
            invalid_image;
    }

    std::size_t string_records_size = 0;

    if (!multiply_size(
            string_count,
            8,
            string_records_size)) {

        return database_image_result::
            invalid_image;
    }

    std::size_t expected_string_size =
        8;

    if (!add_size(
            expected_string_size,
            string_records_size) ||
        !add_size(
            expected_string_size,
            string_bytes) ||
        expected_string_size !=
            strings.size()) {

        return database_image_result::
            invalid_image;
    }

    std::uint32_t expected_string_offset = 0;

    for (std::uint32_t index = 0;
         index < string_count;
         ++index) {

        std::uint32_t value_offset = 0;
        std::uint32_t value_size = 0;

        if (!read_u32(
                strings,
                strings_offset,
                value_offset) ||
            !read_u32(
                strings,
                strings_offset,
                value_size) ||
            value_size == 0 ||
            value_offset !=
                expected_string_offset ||
            value_size >
                string_bytes -
                    expected_string_offset) {

            return database_image_result::
                invalid_image;
        }

        expected_string_offset +=
            value_size;
    }

    if (expected_string_offset !=
            string_bytes ||
        strings_offset !=
            8 +
            string_records_size) {

        return database_image_result::
            invalid_image;
    }

    const auto lexical_records =
        section_span(
            section_kind::lexical_records);

    std::size_t lexical_offset = 0;
    std::uint32_t file_count = 0;
    std::uint32_t word_count = 0;
    std::uint32_t directive_count = 0;
    std::uint32_t reserved = 0;

    if (!read_u32(
            lexical_records,
            lexical_offset,
            file_count) ||
        file_count == 0 ||
        !read_u32(
            lexical_records,
            lexical_offset,
            word_count) ||
        !read_u32(
            lexical_records,
            lexical_offset,
            directive_count) ||
        !read_u32(
            lexical_records,
            lexical_offset,
            reserved) ||
        reserved != 0) {

        return database_image_result::
            invalid_image;
    }

    std::size_t lexical_records_size = 0;

    if (!multiply_size(
            file_count,
            sizeof(persisted_lexical_record),
            lexical_records_size)) {

        return database_image_result::
            invalid_image;
    }

    if (lexical_records.size() !=
        16 +
        lexical_records_size) {

        return database_image_result::
            invalid_image;
    }

    std::vector<persisted_lexical_record>
        decoded;

    try {
        decoded.resize(
            file_count);
    }
    catch (...) {
        return database_image_result::
            failed;
    }

    std::uint32_t expected_word_offset = 0;
    std::uint32_t expected_directive_offset = 0;

    for (std::uint32_t index = 0;
         index < file_count;
         ++index) {

        auto& record =
            decoded[index];

        if (!read_u32(
                lexical_records,
                lexical_offset,
                record.flags) ||
            (record.flags &
                ~lexical_known_flags) != 0 ||
            !read_u32(
                lexical_records,
                lexical_offset,
                record.word_offset) ||
            !read_u32(
                lexical_records,
                lexical_offset,
                record.word_count) ||
            !read_u32(
                lexical_records,
                lexical_offset,
                record.directive_offset) ||
            !read_u32(
                lexical_records,
                lexical_offset,
                record.directive_count) ||
            !read_u32(
                lexical_records,
                lexical_offset,
                record.token_count)) {

            return database_image_result::
                invalid_image;
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

                return database_image_result::
                    invalid_image;
            }

            continue;
        }

        if (record.word_offset !=
                expected_word_offset ||
            record.directive_offset !=
                expected_directive_offset ||
            record.word_count >
                word_count -
                    expected_word_offset ||
            record.directive_count >
                directive_count -
                    expected_directive_offset ||
            record.token_count >
                record.word_count) {

            return database_image_result::
                invalid_image;
        }

        expected_word_offset +=
            record.word_count;

        expected_directive_offset +=
            record.directive_count;
    }

    if (lexical_offset !=
            lexical_records.size() ||
        expected_word_offset !=
            word_count ||
        expected_directive_offset !=
            directive_count) {

        return database_image_result::
            invalid_image;
    }

    const auto words =
        section_span(
            section_kind::lexical_words);

    const auto directives =
        section_span(
            section_kind::lexical_directives);

    std::size_t expected_word_bytes = 0;
    std::size_t expected_directive_bytes = 0;

    if (!multiply_size(
            word_count,
            sizeof(std::uint32_t),
            expected_word_bytes) ||
        !multiply_size(
            directive_count,
            sizeof(lexical_directive_anchor),
            expected_directive_bytes) ||
        words.size() !=
            expected_word_bytes ||
        directives.size() !=
            expected_directive_bytes) {

        return database_image_result::
            invalid_image;
    }

    for (const auto& record :
         decoded) {

        if ((record.flags &
                lexical_available_flag) == 0) {

            continue;
        }

        for (std::uint32_t index = 0;
             index < record.directive_count;
             ++index) {

            const auto anchor_index =
                record.directive_offset +
                index;

            std::size_t anchor_offset =
                static_cast<std::size_t>(
                    anchor_index) *
                    sizeof(lexical_directive_anchor);

            std::uint32_t word_offset = 0;
            std::uint32_t source_base = 0;

            if (!read_u32(
                    directives,
                    anchor_offset,
                    word_offset) ||
                !read_u32(
                    directives,
                    anchor_offset,
                    source_base) ||
                word_offset >=
                    record.word_count) {

                return database_image_result::
                    invalid_image;
            }

            (void)source_base;
        }
    }

    return database_image_result::
        success;
}

}

