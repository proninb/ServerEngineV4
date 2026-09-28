/*
 * Runtime IC storage codecs.
 *
 * Binary IC is a canonical, mmap-friendly semantic snapshot. Its string IDs
 * are file-local and never expose compiled-G slots. Text IC keeps canonical
 * names and values human-readable. Both codecs are command-local; neither
 * creates resident Project state.
 */
#pragma once

#include "runtime_ic.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cw::server {

inline constexpr std::uint32_t runtime_ic_binary_format_version = 1;
inline constexpr std::size_t runtime_ic_binary_header_size = 128;
inline constexpr std::size_t runtime_ic_binary_string_record_size = 8;
inline constexpr std::size_t runtime_ic_binary_record_size = 24;

enum class runtime_ic_codec_result : std::uint8_t {
    success = 0,
    invalid_input,
    invalid_image,
    unsupported_value,
    overflow,
    failed,
};

class runtime_ic_binary_plan final {
public:
    runtime_ic_binary_plan() = default;

    runtime_ic_binary_plan(
        const runtime_ic_binary_plan&) = delete;
    runtime_ic_binary_plan& operator=(
        const runtime_ic_binary_plan&) = delete;

    runtime_ic_binary_plan(
        runtime_ic_binary_plan&&) noexcept = default;
    runtime_ic_binary_plan& operator=(
        runtime_ic_binary_plan&&) noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept {
        return size_value;
    }

    [[nodiscard]] std::uint32_t string_count() const noexcept {
        return static_cast<std::uint32_t>(
            strings.size());
    }

    [[nodiscard]] std::uint32_t component_count() const noexcept {
        return component_count_value;
    }

    [[nodiscard]] std::uint32_t record_count() const noexcept {
        return record_count_value;
    }

private:
    struct string_slot final {
        std::uint32_t hash = 0;
        std::uint32_t id = 0;
    };

    [[nodiscard]] std::uint32_t find_string_id(
        std::string_view value) const noexcept;

    [[nodiscard]] runtime_ic_codec_result append_string(
        std::string_view value) noexcept;

    std::vector<std::string_view> strings;
    std::vector<string_slot> string_index;

    std::uint64_t string_bytes_value = 0;
    std::uint64_t value_bytes_value = 0;

    std::uint64_t string_records_offset = 0;
    std::uint64_t components_offset = 0;
    std::uint64_t records_offset = 0;
    std::uint64_t string_bytes_offset = 0;
    std::uint64_t values_offset = 0;

    std::size_t size_value = 0;
    std::uint32_t component_count_value = 0;
    std::uint32_t record_count_value = 0;

    friend runtime_ic_codec_result
    prepare_runtime_ic_binary(
        std::span<const runtime_ic_record_source>,
        runtime_ic_binary_plan&) noexcept;

    friend runtime_ic_codec_result
    encode_runtime_ic_binary(
        std::span<const runtime_ic_record_source>,
        const runtime_ic_binary_plan&,
        std::span<std::byte>) noexcept;
};

[[nodiscard]] runtime_ic_codec_result
prepare_runtime_ic_binary(
    std::span<const runtime_ic_record_source> records,
    runtime_ic_binary_plan& output) noexcept;

[[nodiscard]] runtime_ic_codec_result
encode_runtime_ic_binary(
    std::span<const runtime_ic_record_source> records,
    const runtime_ic_binary_plan& plan,
    std::span<std::byte> output) noexcept;

struct runtime_ic_binary_record_view final {
    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;
    std::uint16_t member_count = 0;
    std::uint32_t member_begin = 0;
    intrinsic_type type = intrinsic_type::none;
    std::span<const std::byte> value;
};

class runtime_ic_binary_view final {
public:
    runtime_ic_binary_view() noexcept = default;

    [[nodiscard]] runtime_ic_codec_result bind(
        std::span<const std::byte> image) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return bytes.data() != nullptr;
    }

    [[nodiscard]] std::size_t string_count() const noexcept {
        return string_count_value;
    }

    [[nodiscard]] std::size_t component_count() const noexcept {
        return component_count_value;
    }

    [[nodiscard]] std::size_t record_count() const noexcept {
        return record_count_value;
    }

    [[nodiscard]] std::string_view string(
        std::uint32_t id) const noexcept;

    [[nodiscard]] std::uint32_t component(
        std::uint32_t index) const noexcept;

    [[nodiscard]] bool record(
        std::size_t index,
        runtime_ic_binary_record_view& output) const noexcept;

private:
    std::span<const std::byte> bytes;
    const std::byte* string_records = nullptr;
    const std::byte* components = nullptr;
    const std::byte* records = nullptr;
    const std::byte* string_bytes = nullptr;
    const std::byte* values = nullptr;

    std::size_t string_count_value = 0;
    std::size_t component_count_value = 0;
    std::size_t record_count_value = 0;
    std::size_t string_bytes_size = 0;
    std::size_t values_size = 0;
};

[[nodiscard]] runtime_ic_codec_result
encode_runtime_ic_text(
    std::span<const runtime_ic_record_source> records,
    std::string& output) noexcept;

using runtime_ic_text_record_callback =
    bool (*)(
        void* context,
        std::string_view path,
        std::string_view value) noexcept;

[[nodiscard]] runtime_ic_codec_result
parse_runtime_ic_text(
    std::string_view text,
    void* context,
    runtime_ic_text_record_callback callback,
    std::size_t* error_line = nullptr) noexcept;

[[nodiscard]] runtime_ic_codec_result
parse_runtime_ic_text_scalar(
    intrinsic_type type,
    std::uint8_t size,
    std::string_view text,
    runtime_ic_scalar_value& output) noexcept;

}
