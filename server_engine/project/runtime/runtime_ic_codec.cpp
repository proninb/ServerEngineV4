#include "runtime_ic_codec.hpp"

#include "../persistence/crc64_ecma.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <limits>
#include <type_traits>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> image_magic{
    std::byte{'C'},
    std::byte{'W'},
    std::byte{'I'},
    std::byte{'C'},
    std::byte{'V'},
    std::byte{'4'},
    std::byte{0},
    std::byte{0},
};

constexpr std::uint32_t endian_marker =
    0x01020304u;

constexpr std::size_t header_payload_crc_offset = 112;
constexpr std::size_t header_reserved_offset = 120;

[[nodiscard]] constexpr std::uint64_t align8(
    std::uint64_t value) noexcept {

    return (value + 7u) & ~std::uint64_t{7u};
}

[[nodiscard]] bool add_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left >
        (std::numeric_limits<std::uint64_t>::max)() -
            right) {

        return false;
    }

    output = left + right;
    return true;
}

[[nodiscard]] bool multiply_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left != 0 &&
        right >
            (std::numeric_limits<std::uint64_t>::max)() /
                left) {

        return false;
    }

    output = left * right;
    return true;
}

void write_u16(
    std::byte* target,
    std::uint16_t value) noexcept {

    target[0] =
        static_cast<std::byte>(
            value & 0xffu);

    target[1] =
        static_cast<std::byte>(
            (value >> 8) & 0xffu);
}

void write_u32(
    std::byte* target,
    std::uint32_t value) noexcept {

    for (std::uint32_t index = 0;
         index < 4;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

void write_u64(
    std::byte* target,
    std::uint64_t value) noexcept {

    for (std::uint32_t index = 0;
         index < 8;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

[[nodiscard]] std::uint16_t read_u16(
    const std::byte* source) noexcept {

    return
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(
                source[0])) |
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(
                source[1]) << 8);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::byte* source) noexcept {

    return
        static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(
                source[0])) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(
                 source[1])) << 8) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(
                 source[2])) << 16) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(
                 source[3])) << 24);
}

[[nodiscard]] std::uint64_t read_u64(
    const std::byte* source) noexcept {

    std::uint64_t value = 0;

    for (std::uint32_t index = 0;
         index < 8;
         ++index) {

        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    source[index]))
            << (index * 8);
    }

    return value;
}

[[nodiscard]] std::uint32_t hash_text(
    std::string_view value) noexcept {

    std::uint32_t hash = 2166136261u;

    for (const auto character : value) {
        hash ^=
            static_cast<std::uint8_t>(
                character);

        hash *= 16777619u;
    }

    return hash == 0
        ? 1u
        : hash;
}

[[nodiscard]] bool valid_scalar(
    const runtime_ic_scalar_value& value) noexcept {

    const auto raw_type =
        static_cast<std::uint32_t>(
            value.type);

    return
        value.type != intrinsic_type::none &&
        value.type != intrinsic_type::void_type &&
        value.type != intrinsic_type::nullptr_type &&
        raw_type <=
            static_cast<std::uint32_t>(
                intrinsic_type::nullptr_type) &&
        value.size != 0 &&
        value.size <= value.bytes.size();
}

[[nodiscard]] bool valid_component(
    std::string_view value) noexcept {

    return !value.empty();
}

[[nodiscard]] std::size_t next_power_of_two(
    std::size_t value) noexcept {

    if (value <= 8) {
        return 8;
    }

    --value;

    for (std::size_t shift = 1;
         shift < sizeof(value) * 8;
         shift <<= 1) {

        value |= value >> shift;
    }

    if (value ==
        (std::numeric_limits<std::size_t>::max)()) {

        return 0;
    }

    return value + 1;
}

[[nodiscard]] bool text_path_component(
    std::string_view value) noexcept {

    if (value.empty()) {
        return false;
    }

    for (const auto character : value) {
        if (character == ':' ||
            character == '.' ||
            character == '=' ||
            character == '\r' ||
            character == '\n' ||
            character == '\t' ||
            character == ' ') {

            return false;
        }
    }

    return true;
}

[[nodiscard]] std::string_view trim(
    std::string_view value) noexcept {

    while (!value.empty() &&
           (value.front() == ' ' ||
            value.front() == '\t' ||
            value.front() == '\r')) {

        value.remove_prefix(1);
    }

    while (!value.empty() &&
           (value.back() == ' ' ||
            value.back() == '\t' ||
            value.back() == '\r')) {

        value.remove_suffix(1);
    }

    return value;
}

template <typename T>
[[nodiscard]] runtime_ic_codec_result
append_number(
    T value,
    std::string& output) noexcept {

    std::array<char, 96> buffer{};

    auto result =
        std::to_chars(
            buffer.data(),
            buffer.data() + buffer.size(),
            value);

    if (result.ec != std::errc{}) {
        return runtime_ic_codec_result::
            unsupported_value;
    }

    try {
        output.append(
            buffer.data(),
            result.ptr);
    }
    catch (...) {
        return runtime_ic_codec_result::failed;
    }

    return runtime_ic_codec_result::success;
}

[[nodiscard]] std::uint64_t load_unsigned(
    const runtime_ic_scalar_value& value) noexcept {

    std::uint64_t output = 0;

    std::memcpy(
        &output,
        value.bytes.data(),
        std::min<std::size_t>(
            value.size,
            sizeof(output)));

    return output;
}

[[nodiscard]] std::int64_t load_signed(
    const runtime_ic_scalar_value& value) noexcept {

    const auto raw =
        load_unsigned(
            value);

    const auto bits =
        static_cast<unsigned>(
            value.size) * 8u;

    if (bits >= 64u) {
        return static_cast<std::int64_t>(
            raw);
    }

    const auto sign =
        std::uint64_t{1} <<
        (bits - 1u);

    const auto extended =
        (raw & sign) != 0
        ? raw |
            (~std::uint64_t{0} << bits)
        : raw;

    return static_cast<std::int64_t>(
        extended);
}

[[nodiscard]] runtime_ic_codec_result
append_scalar(
    const runtime_ic_scalar_value& value,
    std::string& output) noexcept {

    if (!valid_scalar(value)) {
        return runtime_ic_codec_result::
            invalid_input;
    }

    switch (value.type) {
    case intrinsic_type::bool_type: {
        if (value.size != 1) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        const auto raw =
            std::to_integer<std::uint8_t>(
                value.bytes[0]);

        if (raw > 1) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        try {
            output.append(
                raw == 0
                    ? "false"
                    : "true");
        }
        catch (...) {
            return runtime_ic_codec_result::failed;
        }

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::signed_char:
    case intrinsic_type::signed_short:
    case intrinsic_type::signed_int:
    case intrinsic_type::signed_long:
    case intrinsic_type::signed_long_long:
        if (value.size > sizeof(std::int64_t)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        return append_number(
            load_signed(value),
            output);

    case intrinsic_type::char_type:
    case intrinsic_type::unsigned_char:
    case intrinsic_type::wchar_type:
    case intrinsic_type::char8_type:
    case intrinsic_type::char16_type:
    case intrinsic_type::char32_type:
    case intrinsic_type::unsigned_short:
    case intrinsic_type::unsigned_int:
    case intrinsic_type::unsigned_long:
    case intrinsic_type::unsigned_long_long:
        if (value.size > sizeof(std::uint64_t)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        return append_number(
            load_unsigned(value),
            output);

    case intrinsic_type::float_type: {
        if (value.size != sizeof(float)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        float number = 0;

        std::memcpy(
            &number,
            value.bytes.data(),
            sizeof(number));

        std::array<char, 96> buffer{};

        const auto converted =
            std::to_chars(
                buffer.data(),
                buffer.data() + buffer.size(),
                number,
                std::chars_format::general,
                std::numeric_limits<float>::
                    max_digits10);

        if (converted.ec != std::errc{}) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        try {
            output.append(
                buffer.data(),
                converted.ptr);
        }
        catch (...) {
            return runtime_ic_codec_result::failed;
        }

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::double_type:
    case intrinsic_type::long_double_type: {
        if (value.size != sizeof(double)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        double number = 0;

        std::memcpy(
            &number,
            value.bytes.data(),
            sizeof(number));

        std::array<char, 128> buffer{};

        const auto converted =
            std::to_chars(
                buffer.data(),
                buffer.data() + buffer.size(),
                number,
                std::chars_format::general,
                std::numeric_limits<double>::
                    max_digits10);

        if (converted.ec != std::errc{}) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        try {
            output.append(
                buffer.data(),
                converted.ptr);
        }
        catch (...) {
            return runtime_ic_codec_result::failed;
        }

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::none:
    case intrinsic_type::void_type:
    case intrinsic_type::nullptr_type:
        return runtime_ic_codec_result::
            unsupported_value;
    }

    return runtime_ic_codec_result::
        unsupported_value;
}

[[nodiscard]] bool parse_unsigned(
    std::string_view text,
    std::uint64_t maximum,
    std::uint64_t& output) noexcept {

    output = 0;

    if (text.empty()) {
        return false;
    }

    const auto converted =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            output);

    return
        converted.ec == std::errc{} &&
        converted.ptr ==
            text.data() + text.size() &&
        output <= maximum;
}

[[nodiscard]] bool parse_signed(
    std::string_view text,
    std::int64_t minimum,
    std::int64_t maximum,
    std::int64_t& output) noexcept {

    output = 0;

    if (text.empty()) {
        return false;
    }

    const auto converted =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            output);

    return
        converted.ec == std::errc{} &&
        converted.ptr ==
            text.data() + text.size() &&
        output >= minimum &&
        output <= maximum;
}

[[nodiscard]] std::uint64_t unsigned_maximum(
    std::uint8_t size) noexcept {

    if (size >= sizeof(std::uint64_t)) {
        return
            (std::numeric_limits<std::uint64_t>::max)();
    }

    return
        (std::uint64_t{1} <<
            (static_cast<unsigned>(size) * 8u)) -
        1u;
}

[[nodiscard]] std::int64_t signed_minimum(
    std::uint8_t size) noexcept {

    if (size >= sizeof(std::int64_t)) {
        return
            (std::numeric_limits<std::int64_t>::min)();
    }

    return -
        (std::int64_t{1} <<
            (static_cast<unsigned>(size) * 8u - 1u));
}

[[nodiscard]] std::int64_t signed_maximum(
    std::uint8_t size) noexcept {

    if (size >= sizeof(std::int64_t)) {
        return
            (std::numeric_limits<std::int64_t>::max)();
    }

    return
        (std::int64_t{1} <<
            (static_cast<unsigned>(size) * 8u - 1u)) -
        1;
}

template <typename T>
void store_value(
    T value,
    runtime_ic_scalar_value& output) noexcept {

    std::memcpy(
        output.bytes.data(),
        &value,
        sizeof(value));
}

}

std::uint32_t runtime_ic_binary_plan::find_string_id(
    std::string_view value) const noexcept {

    if (string_index.empty()) {
        return 0;
    }

    const auto hash =
        hash_text(value);

    const auto mask =
        string_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    for (std::size_t probe = 0;
         probe < string_index.size();
         ++probe) {

        const auto& slot =
            string_index[position];

        if (slot.id == 0) {
            return 0;
        }

        if (slot.hash == hash &&
            slot.id <= strings.size() &&
            strings[
                slot.id - 1] == value) {

            return slot.id;
        }

        position =
            (position + 1) &
            mask;
    }

    return 0;
}

runtime_ic_codec_result
runtime_ic_binary_plan::append_string(
    std::string_view value) noexcept {

    if (!valid_component(value)) {
        return runtime_ic_codec_result::
            invalid_input;
    }

    if (find_string_id(value) != 0) {
        return runtime_ic_codec_result::success;
    }

    if (strings.size() >=
        (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_codec_result::overflow;
    }

    std::uint64_t string_bytes = 0;

    if (!add_u64(
            string_bytes_value,
            value.size(),
            string_bytes) ||
        string_bytes >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_codec_result::overflow;
    }

    const auto id =
        static_cast<std::uint32_t>(
            strings.size() + 1);

    try {
        strings.push_back(
            value);
    }
    catch (...) {
        return runtime_ic_codec_result::failed;
    }

    const auto hash =
        hash_text(value);

    const auto mask =
        string_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    for (std::size_t probe = 0;
         probe < string_index.size();
         ++probe) {

        auto& slot =
            string_index[position];

        if (slot.id == 0) {
            slot.hash = hash;
            slot.id = id;
            string_bytes_value =
                string_bytes;
            return runtime_ic_codec_result::
                success;
        }

        position =
            (position + 1) &
            mask;
    }

    return runtime_ic_codec_result::failed;
}

runtime_ic_codec_result prepare_runtime_ic_binary(
    std::span<const runtime_ic_record_source> records,
    runtime_ic_binary_plan& output) noexcept {

    output = {};

    if (records.empty() ||
        records.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return records.empty()
            ? runtime_ic_codec_result::invalid_input
            : runtime_ic_codec_result::overflow;
    }

    std::uint64_t component_count = 0;
    std::uint64_t value_bytes = 0;

    for (const auto& record : records) {
        if (record.path.object.empty() ||
            !valid_scalar(
                record.value)) {

            return runtime_ic_codec_result::
                invalid_input;
        }

        std::uint64_t next = 0;

        if (!add_u64(
                component_count,
                record.path.object.size(),
                next) ||
            !add_u64(
                next,
                record.path.members.size(),
                component_count) ||
            component_count >
                (std::numeric_limits<std::uint32_t>::max)() ||
            !add_u64(
                value_bytes,
                record.value.size,
                value_bytes) ||
            value_bytes >
                (std::numeric_limits<std::uint32_t>::max)()) {

            return runtime_ic_codec_result::overflow;
        }
    }

    const auto desired_slots =
        component_count >
            (std::numeric_limits<std::size_t>::max)() / 2
        ? std::size_t{0}
        : next_power_of_two(
            static_cast<std::size_t>(
                component_count) * 2);

    if (desired_slots == 0) {
        return runtime_ic_codec_result::overflow;
    }

    try {
        output.string_index.assign(
            desired_slots,
            {});
        output.strings.reserve(
            static_cast<std::size_t>(
                component_count));
    }
    catch (...) {
        output = {};
        return runtime_ic_codec_result::failed;
    }

    for (const auto& record : records) {
        for (const auto component :
             record.path.object) {

            const auto added =
                output.append_string(
                    component);

            if (added !=
                runtime_ic_codec_result::success) {

                output = {};
                return added;
            }
        }

        for (const auto component :
             record.path.members) {

            const auto added =
                output.append_string(
                    component);

            if (added !=
                runtime_ic_codec_result::success) {

                output = {};
                return added;
            }
        }
    }

    std::uint64_t string_records_bytes = 0;
    std::uint64_t components_bytes = 0;
    std::uint64_t records_bytes = 0;

    if (!multiply_u64(
            output.strings.size(),
            runtime_ic_binary_string_record_size,
            string_records_bytes) ||
        !multiply_u64(
            component_count,
            sizeof(std::uint32_t),
            components_bytes) ||
        !multiply_u64(
            records.size(),
            runtime_ic_binary_record_size,
            records_bytes)) {

        output = {};
        return runtime_ic_codec_result::overflow;
    }

    output.string_records_offset =
        runtime_ic_binary_header_size;

    output.components_offset =
        align8(
            output.string_records_offset +
            string_records_bytes);

    output.records_offset =
        align8(
            output.components_offset +
            components_bytes);

    output.string_bytes_offset =
        align8(
            output.records_offset +
            records_bytes);

    output.values_offset =
        align8(
            output.string_bytes_offset +
            output.string_bytes_value);

    std::uint64_t total = 0;

    if (!add_u64(
            output.values_offset,
            value_bytes,
            total) ||
        total >
            (std::numeric_limits<std::size_t>::max)()) {

        output = {};
        return runtime_ic_codec_result::overflow;
    }

    output.value_bytes_value =
        value_bytes;

    output.component_count_value =
        static_cast<std::uint32_t>(
            component_count);

    output.record_count_value =
        static_cast<std::uint32_t>(
            records.size());

    output.size_value =
        static_cast<std::size_t>(
            total);

    return runtime_ic_codec_result::success;
}

runtime_ic_codec_result encode_runtime_ic_binary(
    std::span<const runtime_ic_record_source> records,
    const runtime_ic_binary_plan& plan,
    std::span<std::byte> output) noexcept {

    if (records.size() !=
            plan.record_count_value ||
        plan.size_value == 0 ||
        output.size() !=
            plan.size_value) {

        return runtime_ic_codec_result::
            invalid_input;
    }

    std::fill(
        output.begin(),
        output.end(),
        std::byte{0});

    std::memcpy(
        output.data(),
        image_magic.data(),
        image_magic.size());

    write_u32(
        output.data() + 8,
        runtime_ic_binary_format_version);

    write_u32(
        output.data() + 12,
        endian_marker);

    write_u32(
        output.data() + 16,
        runtime_ic_binary_header_size);

    write_u32(
        output.data() + 20,
        runtime_ic_binary_string_record_size);

    write_u32(
        output.data() + 24,
        sizeof(std::uint32_t));

    write_u32(
        output.data() + 28,
        runtime_ic_binary_record_size);

    write_u64(
        output.data() + 32,
        output.size());

    write_u32(
        output.data() + 40,
        plan.string_count());

    write_u32(
        output.data() + 44,
        plan.component_count_value);

    write_u32(
        output.data() + 48,
        plan.record_count_value);

    write_u32(
        output.data() + 52,
        0);

    write_u64(
        output.data() + 56,
        plan.string_bytes_value);

    write_u64(
        output.data() + 64,
        plan.value_bytes_value);

    write_u64(
        output.data() + 72,
        plan.string_records_offset);

    write_u64(
        output.data() + 80,
        plan.components_offset);

    write_u64(
        output.data() + 88,
        plan.records_offset);

    write_u64(
        output.data() + 96,
        plan.string_bytes_offset);

    write_u64(
        output.data() + 104,
        plan.values_offset);

    write_u64(
        output.data() +
            header_payload_crc_offset,
        0);

    write_u64(
        output.data() +
            header_reserved_offset,
        0);

    std::uint32_t string_cursor = 0;

    for (std::size_t index = 0;
         index < plan.strings.size();
         ++index) {

        const auto text =
            plan.strings[index];

        if (text.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return runtime_ic_codec_result::overflow;
        }

        auto* record =
            output.data() +
            static_cast<std::size_t>(
                plan.string_records_offset) +
            index *
                runtime_ic_binary_string_record_size;

        write_u32(
            record,
            string_cursor);

        write_u32(
            record + 4,
            static_cast<std::uint32_t>(
                text.size()));

        std::memcpy(
            output.data() +
                static_cast<std::size_t>(
                    plan.string_bytes_offset) +
                string_cursor,
            text.data(),
            text.size());

        string_cursor +=
            static_cast<std::uint32_t>(
                text.size());
    }

    std::uint32_t component_cursor = 0;
    std::uint32_t value_cursor = 0;

    for (std::size_t index = 0;
         index < records.size();
         ++index) {

        const auto& source =
            records[index];

        if (source.path.object.empty() ||
            source.path.object.size() >
                (std::numeric_limits<std::uint16_t>::max)() ||
            source.path.members.size() >
                (std::numeric_limits<std::uint16_t>::max)() ||
            !valid_scalar(source.value)) {

            return runtime_ic_codec_result::
                invalid_input;
        }

        const auto object_begin =
            component_cursor;

        for (const auto component :
             source.path.object) {

            const auto id =
                plan.find_string_id(
                    component);

            if (id == 0) {
                return runtime_ic_codec_result::
                    invalid_input;
            }

            write_u32(
                output.data() +
                    static_cast<std::size_t>(
                        plan.components_offset) +
                    static_cast<std::size_t>(
                        component_cursor) * 4,
                id);

            ++component_cursor;
        }

        const auto member_begin =
            component_cursor;

        for (const auto component :
             source.path.members) {

            const auto id =
                plan.find_string_id(
                    component);

            if (id == 0) {
                return runtime_ic_codec_result::
                    invalid_input;
            }

            write_u32(
                output.data() +
                    static_cast<std::size_t>(
                        plan.components_offset) +
                    static_cast<std::size_t>(
                        component_cursor) * 4,
                id);

            ++component_cursor;
        }

        auto* record =
            output.data() +
            static_cast<std::size_t>(
                plan.records_offset) +
            index *
                runtime_ic_binary_record_size;

        write_u32(
            record,
            object_begin);

        write_u16(
            record + 4,
            static_cast<std::uint16_t>(
                source.path.object.size()));

        write_u16(
            record + 6,
            static_cast<std::uint16_t>(
                source.path.members.size()));

        write_u32(
            record + 8,
            member_begin);

        write_u32(
            record + 12,
            value_cursor);

        write_u16(
            record + 16,
            source.value.size);

        record[18] =
            static_cast<std::byte>(
                source.value.type);

        record[19] =
            std::byte{0};

        write_u32(
            record + 20,
            0);

        std::memcpy(
            output.data() +
                static_cast<std::size_t>(
                    plan.values_offset) +
                value_cursor,
            source.value.bytes.data(),
            source.value.size);

        value_cursor +=
            source.value.size;
    }

    if (component_cursor !=
            plan.component_count_value ||
        value_cursor !=
            plan.value_bytes_value ||
        string_cursor !=
            plan.string_bytes_value) {

        return runtime_ic_codec_result::
            invalid_input;
    }

    write_u64(
        output.data() +
            header_payload_crc_offset,
        persistence_crc64(
            output.subspan(
                runtime_ic_binary_header_size)));

    return runtime_ic_codec_result::success;
}

runtime_ic_codec_result runtime_ic_binary_view::bind(
    std::span<const std::byte> image) noexcept {

    reset();

    if (image.size() <
            runtime_ic_binary_header_size ||
        !std::equal(
            image_magic.begin(),
            image_magic.end(),
            image.begin())) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    if (read_u32(image.data() + 8) !=
            runtime_ic_binary_format_version ||
        read_u32(image.data() + 12) !=
            endian_marker ||
        read_u32(image.data() + 16) !=
            runtime_ic_binary_header_size ||
        read_u32(image.data() + 20) !=
            runtime_ic_binary_string_record_size ||
        read_u32(image.data() + 24) !=
            sizeof(std::uint32_t) ||
        read_u32(image.data() + 28) !=
            runtime_ic_binary_record_size ||
        read_u64(image.data() + 32) !=
            image.size() ||
        read_u32(image.data() + 52) != 0 ||
        read_u64(
            image.data() +
            header_reserved_offset) != 0) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    const auto string_count =
        read_u32(
            image.data() + 40);

    const auto component_count =
        read_u32(
            image.data() + 44);

    const auto record_count =
        read_u32(
            image.data() + 48);

    const auto string_bytes_count =
        read_u64(
            image.data() + 56);

    const auto value_bytes_count =
        read_u64(
            image.data() + 64);

    const auto string_records_at =
        read_u64(
            image.data() + 72);

    const auto components_at =
        read_u64(
            image.data() + 80);

    const auto records_at =
        read_u64(
            image.data() + 88);

    const auto string_bytes_at =
        read_u64(
            image.data() + 96);

    const auto values_at =
        read_u64(
            image.data() + 104);

    std::uint64_t string_records_bytes = 0;
    std::uint64_t components_bytes = 0;
    std::uint64_t records_bytes = 0;

    if (!multiply_u64(
            string_count,
            runtime_ic_binary_string_record_size,
            string_records_bytes) ||
        !multiply_u64(
            component_count,
            sizeof(std::uint32_t),
            components_bytes) ||
        !multiply_u64(
            record_count,
            runtime_ic_binary_record_size,
            records_bytes)) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    const auto expected_components =
        align8(
            runtime_ic_binary_header_size +
            string_records_bytes);

    const auto expected_records =
        align8(
            expected_components +
            components_bytes);

    const auto expected_string_bytes =
        align8(
            expected_records +
            records_bytes);

    const auto expected_values =
        align8(
            expected_string_bytes +
            string_bytes_count);

    std::uint64_t expected_size = 0;

    if (string_records_at !=
            runtime_ic_binary_header_size ||
        components_at !=
            expected_components ||
        records_at !=
            expected_records ||
        string_bytes_at !=
            expected_string_bytes ||
        values_at !=
            expected_values ||
        !add_u64(
            expected_values,
            value_bytes_count,
            expected_size) ||
        expected_size != image.size() ||
        string_bytes_count >
            (std::numeric_limits<std::size_t>::max)() ||
        value_bytes_count >
            (std::numeric_limits<std::size_t>::max)()) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    if (read_u64(
            image.data() +
            header_payload_crc_offset) !=
        persistence_crc64(
            image.subspan(
                runtime_ic_binary_header_size))) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    const auto* string_record_data =
        image.data() +
        static_cast<std::size_t>(
            string_records_at);

    const auto* component_data =
        image.data() +
        static_cast<std::size_t>(
            components_at);

    const auto* record_data =
        image.data() +
        static_cast<std::size_t>(
            records_at);

    const auto* string_data =
        image.data() +
        static_cast<std::size_t>(
            string_bytes_at);

    const auto* value_data =
        image.data() +
        static_cast<std::size_t>(
            values_at);

    std::uint32_t string_cursor = 0;

    for (std::uint32_t index = 0;
         index < string_count;
         ++index) {

        const auto* record =
            string_record_data +
            static_cast<std::size_t>(
                index) *
                runtime_ic_binary_string_record_size;

        const auto offset =
            read_u32(record);

        const auto length =
            read_u32(record + 4);

        if (length == 0 ||
            offset != string_cursor ||
            offset >
                string_bytes_count ||
            length >
                string_bytes_count -
                offset) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        string_cursor += length;
    }

    if (string_cursor !=
        string_bytes_count) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    for (std::uint32_t index = 0;
         index < component_count;
         ++index) {

        const auto id =
            read_u32(
                component_data +
                static_cast<std::size_t>(
                    index) * 4);

        if (id == 0 ||
            id > string_count) {

            return runtime_ic_codec_result::
                invalid_image;
        }
    }

    std::uint32_t component_cursor = 0;
    std::uint32_t value_cursor = 0;

    for (std::uint32_t index = 0;
         index < record_count;
         ++index) {

        const auto* record =
            record_data +
            static_cast<std::size_t>(
                index) *
                runtime_ic_binary_record_size;

        const auto object_begin =
            read_u32(record);

        const auto object_count =
            read_u16(record + 4);

        const auto member_count =
            read_u16(record + 6);

        const auto member_begin =
            read_u32(record + 8);

        const auto value_offset =
            read_u32(record + 12);

        const auto value_size =
            read_u16(record + 16);

        const auto type =
            std::to_integer<std::uint8_t>(
                record[18]);

        const auto flags =
            std::to_integer<std::uint8_t>(
                record[19]);

        if (object_count == 0 ||
            object_begin !=
                component_cursor ||
            member_begin !=
                object_begin +
                object_count ||
            value_offset != value_cursor ||
            flags != 0 ||
            read_u32(record + 20) != 0 ||
            type == 0 ||
            type >
                static_cast<std::uint8_t>(
                    intrinsic_type::nullptr_type) ||
            type ==
                static_cast<std::uint8_t>(
                    intrinsic_type::void_type) ||
            type ==
                static_cast<std::uint8_t>(
                    intrinsic_type::nullptr_type) ||
            value_size == 0 ||
            value_size >
                runtime_ic_scalar_value{}.
                    bytes.size()) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        const auto record_components =
            static_cast<std::uint64_t>(
                object_count) +
            member_count;

        if (component_cursor >
                component_count ||
            record_components >
                static_cast<std::uint64_t>(
                    component_count -
                    component_cursor) ||
            value_cursor >
                value_bytes_count ||
            value_size >
                value_bytes_count -
                value_cursor) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        component_cursor +=
            static_cast<std::uint32_t>(
                record_components);

        value_cursor += value_size;
    }

    if (component_cursor !=
            component_count ||
        value_cursor !=
            value_bytes_count) {

        return runtime_ic_codec_result::
            invalid_image;
    }

    bytes = image;
    string_records = string_record_data;
    components = component_data;
    records = record_data;
    string_bytes = string_data;
    values = value_data;

    string_count_value = string_count;
    component_count_value = component_count;
    record_count_value = record_count;

    string_bytes_size =
        static_cast<std::size_t>(
            string_bytes_count);

    values_size =
        static_cast<std::size_t>(
            value_bytes_count);

    return runtime_ic_codec_result::success;
}

void runtime_ic_binary_view::reset() noexcept {
    bytes = {};
    string_records = nullptr;
    components = nullptr;
    records = nullptr;
    string_bytes = nullptr;
    values = nullptr;

    string_count_value = 0;
    component_count_value = 0;
    record_count_value = 0;
    string_bytes_size = 0;
    values_size = 0;
}

std::string_view runtime_ic_binary_view::string(
    std::uint32_t id) const noexcept {

    if (!valid() ||
        id == 0 ||
        id > string_count_value) {

        return {};
    }

    const auto* record =
        string_records +
        static_cast<std::size_t>(
            id - 1) *
            runtime_ic_binary_string_record_size;

    const auto offset =
        read_u32(record);

    const auto length =
        read_u32(record + 4);

    if (offset >
            string_bytes_size ||
        length >
            string_bytes_size -
            offset) {

        return {};
    }

    return {
        reinterpret_cast<const char*>(
            string_bytes + offset),
        length,
    };
}

std::uint32_t runtime_ic_binary_view::component(
    std::uint32_t index) const noexcept {

    if (!valid() ||
        index >= component_count_value) {

        return 0;
    }

    return read_u32(
        components +
        static_cast<std::size_t>(
            index) * 4);
}

bool runtime_ic_binary_view::record(
    std::size_t index,
    runtime_ic_binary_record_view& output) const noexcept {

    output = {};

    if (!valid() ||
        index >= record_count_value) {

        return false;
    }

    const auto* record =
        records +
        index *
            runtime_ic_binary_record_size;

    const auto value_offset =
        read_u32(record + 12);

    const auto value_size =
        read_u16(record + 16);

    if (value_offset >
            values_size ||
        value_size >
            values_size -
            value_offset) {

        return false;
    }

    output.object_begin =
        read_u32(record);

    output.object_count =
        read_u16(record + 4);

    output.member_count =
        read_u16(record + 6);

    output.member_begin =
        read_u32(record + 8);

    output.type =
        static_cast<intrinsic_type>(
            std::to_integer<std::uint8_t>(
                record[18]));

    output.value = {
        values + value_offset,
        value_size,
    };

    return true;
}

runtime_ic_codec_result encode_runtime_ic_text(
    std::span<const runtime_ic_record_source> records,
    std::string& output) noexcept {

    output.clear();

    if (records.empty()) {
        return runtime_ic_codec_result::
            invalid_input;
    }

    try {
        std::size_t estimate = 0;

        for (const auto& record : records) {
            estimate +=
                record.path.object.size() * 10 +
                record.path.members.size() * 10 +
                32;
        }

        output.reserve(estimate);
    }
    catch (...) {
        return runtime_ic_codec_result::failed;
    }

    for (const auto& record : records) {
        if (record.path.object.empty() ||
            !valid_scalar(
                record.value)) {

            output.clear();
            return runtime_ic_codec_result::
                invalid_input;
        }

        try {
            for (const auto component :
                 record.path.object) {

                if (!text_path_component(
                        component)) {

                    output.clear();
                    return runtime_ic_codec_result::
                        invalid_input;
                }

                output.append("::");
                output.append(component);
            }

            for (const auto component :
                 record.path.members) {

                if (!text_path_component(
                        component)) {

                    output.clear();
                    return runtime_ic_codec_result::
                        invalid_input;
                }

                output.push_back('.');
                output.append(component);
            }

            output.append(" = ");
        }
        catch (...) {
            output.clear();
            return runtime_ic_codec_result::failed;
        }

        const auto appended =
            append_scalar(
                record.value,
                output);

        if (appended !=
            runtime_ic_codec_result::success) {

            output.clear();
            return appended;
        }

        try {
            output.push_back('\n');
        }
        catch (...) {
            output.clear();
            return runtime_ic_codec_result::failed;
        }
    }

    return runtime_ic_codec_result::success;
}

runtime_ic_codec_result parse_runtime_ic_text(
    std::string_view text,
    void* context,
    runtime_ic_text_record_callback callback,
    std::size_t* error_line) noexcept {

    if (error_line != nullptr) {
        *error_line = 0;
    }

    if (callback == nullptr) {
        return runtime_ic_codec_result::
            invalid_input;
    }

    std::size_t line_number = 0;
    std::size_t begin = 0;

    while (begin <= text.size()) {
        ++line_number;

        const auto newline =
            text.find(
                '\n',
                begin);

        const auto end =
            newline == std::string_view::npos
                ? text.size()
                : newline;

        auto line =
            trim(
                text.substr(
                    begin,
                    end - begin));

        if (!line.empty() &&
            line.front() != '#') {

            const auto separator =
                line.find('=');

            if (separator ==
                    std::string_view::npos ||
                line.find(
                    '=',
                    separator + 1) !=
                    std::string_view::npos) {

                if (error_line != nullptr) {
                    *error_line = line_number;
                }

                return runtime_ic_codec_result::
                    invalid_image;
            }

            const auto path =
                trim(
                    line.substr(
                        0,
                        separator));

            const auto value =
                trim(
                    line.substr(
                        separator + 1));

            if (!path.starts_with("::") ||
                path.size() <= 2 ||
                value.empty() ||
                !callback(
                    context,
                    path,
                    value)) {

                if (error_line != nullptr) {
                    *error_line = line_number;
                }

                return runtime_ic_codec_result::
                    invalid_image;
            }
        }

        if (newline ==
            std::string_view::npos) {

            break;
        }

        begin = newline + 1;
    }

    return runtime_ic_codec_result::success;
}

runtime_ic_codec_result parse_runtime_ic_text_scalar(
    intrinsic_type type,
    std::uint8_t size,
    std::string_view text,
    runtime_ic_scalar_value& output) noexcept {

    output = {};

    text = trim(text);

    if (size == 0 ||
        size > output.bytes.size() ||
        text.empty()) {

        return runtime_ic_codec_result::
            invalid_input;
    }

    output.type = type;
    output.size = size;

    switch (type) {
    case intrinsic_type::bool_type: {
        if (size != 1) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        if (text == "true" ||
            text == "1") {

            output.bytes[0] =
                std::byte{1};

            return runtime_ic_codec_result::
                success;
        }

        if (text == "false" ||
            text == "0") {

            output.bytes[0] =
                std::byte{0};

            return runtime_ic_codec_result::
                success;
        }

        return runtime_ic_codec_result::
            invalid_image;
    }

    case intrinsic_type::signed_char:
    case intrinsic_type::signed_short:
    case intrinsic_type::signed_int:
    case intrinsic_type::signed_long:
    case intrinsic_type::signed_long_long: {
        if (size > sizeof(std::int64_t)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        std::int64_t value = 0;

        if (!parse_signed(
                text,
                signed_minimum(size),
                signed_maximum(size),
                value)) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        std::memcpy(
            output.bytes.data(),
            &value,
            size);

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::char_type:
    case intrinsic_type::unsigned_char:
    case intrinsic_type::wchar_type:
    case intrinsic_type::char8_type:
    case intrinsic_type::char16_type:
    case intrinsic_type::char32_type:
    case intrinsic_type::unsigned_short:
    case intrinsic_type::unsigned_int:
    case intrinsic_type::unsigned_long:
    case intrinsic_type::unsigned_long_long: {
        if (size > sizeof(std::uint64_t)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        std::uint64_t value = 0;

        if (!parse_unsigned(
                text,
                unsigned_maximum(size),
                value)) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        std::memcpy(
            output.bytes.data(),
            &value,
            size);

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::float_type: {
        if (size != sizeof(float)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        float value = 0;

        const auto converted =
            std::from_chars(
                text.data(),
                text.data() + text.size(),
                value,
                std::chars_format::general);

        if (converted.ec != std::errc{} ||
            converted.ptr !=
                text.data() + text.size()) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        store_value(
            value,
            output);

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::double_type:
    case intrinsic_type::long_double_type: {
        if (size != sizeof(double)) {
            return runtime_ic_codec_result::
                unsupported_value;
        }

        double value = 0;

        const auto converted =
            std::from_chars(
                text.data(),
                text.data() + text.size(),
                value,
                std::chars_format::general);

        if (converted.ec != std::errc{} ||
            converted.ptr !=
                text.data() + text.size()) {

            return runtime_ic_codec_result::
                invalid_image;
        }

        store_value(
            value,
            output);

        return runtime_ic_codec_result::success;
    }

    case intrinsic_type::none:
    case intrinsic_type::void_type:
    case intrinsic_type::nullptr_type:
        return runtime_ic_codec_result::
            unsupported_value;
    }

    return runtime_ic_codec_result::
        unsupported_value;
}

}
