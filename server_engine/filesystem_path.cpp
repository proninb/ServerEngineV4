#include "filesystem_path.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <cstdint>
#endif

namespace cw::server {
namespace {

#if !defined(_WIN32)

[[nodiscard]] bool valid_utf8(std::string_view value) noexcept {
    std::size_t offset = 0;

    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset]);

        if (first <= 0x7Fu) {
            ++offset;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;

        if (first >= 0xC2u && first <= 0xDFu) {
            length = 2;
            codepoint = first & 0x1Fu;
            minimum = 0x80u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            length = 3;
            codepoint = first & 0x0Fu;
            minimum = 0x800u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            length = 4;
            codepoint = first & 0x07u;
            minimum = 0x10000u;
        } else {
            return false;
        }

        if (offset + length > value.size()) {
            return false;
        }

        for (std::size_t index = 1; index < length; ++index) {
            const auto next = static_cast<unsigned char>(value[offset + index]);

            if ((next & 0xC0u) != 0x80u) {
                return false;
            }

            codepoint =
                (codepoint << 6u) |
                static_cast<std::uint32_t>(next & 0x3Fu);
        }

        if (codepoint < minimum ||
            codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {

            return false;
        }

        offset += length;
    }

    return true;
}

#endif

}

filesystem_path_result filesystem_path_from_utf8(
    std::string_view value,
    std::filesystem::path& output) noexcept {

    output.clear();

    try {
#if defined(_WIN32)
        if (value.size() >
            static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return filesystem_path_result::failed;
        }

        if (value.empty()) {
            return filesystem_path_result::success;
        }

        const auto input_size = static_cast<int>(value.size());

        const int wide_size = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            nullptr,
            0);

        if (wide_size <= 0) {
            return GetLastError() == ERROR_NO_UNICODE_TRANSLATION
                ? filesystem_path_result::invalid_utf8
                : filesystem_path_result::failed;
        }

        std::wstring wide(
            static_cast<std::size_t>(wide_size),
            L'\0');

        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                input_size,
                wide.data(),
                wide_size) != wide_size) {

            return GetLastError() == ERROR_NO_UNICODE_TRANSLATION
                ? filesystem_path_result::invalid_utf8
                : filesystem_path_result::failed;
        }

        output = std::filesystem::path(std::move(wide));
#else
        if (!valid_utf8(value)) {
            return filesystem_path_result::invalid_utf8;
        }

        output = std::filesystem::path(std::string(value));
#endif

        return filesystem_path_result::success;
    }
    catch (...) {
        output.clear();
        return filesystem_path_result::failed;
    }
}

filesystem_path_result filesystem_path_utf8_size(
    filesystem_native_path_view value,
    std::size_t& output) noexcept {

    output = 0;

#if defined(_WIN32)
    if (value.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {

        return filesystem_path_result::failed;
    }

    if (value.empty()) {
        return filesystem_path_result::success;
    }

    const auto input_size =
        static_cast<int>(
            value.size());

    const auto required =
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            nullptr,
            0,
            nullptr,
            nullptr);

    if (required <= 0) {
        return GetLastError() ==
                ERROR_NO_UNICODE_TRANSLATION
            ? filesystem_path_result::
                invalid_utf8
            : filesystem_path_result::
                failed;
    }

    output =
        static_cast<std::size_t>(
            required);
#else
    const std::string_view bytes{
        value.data(),
        value.size()};

    if (!valid_utf8(bytes)) {
        return filesystem_path_result::
            invalid_utf8;
    }

    output = bytes.size();
#endif

    return filesystem_path_result::success;
}

filesystem_path_result filesystem_path_to_utf8(
    filesystem_native_path_view value,
    std::span<char> output,
    std::size_t& written) noexcept {

    written = 0;

#if defined(_WIN32)
    if (value.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {

        return filesystem_path_result::failed;
    }

    if (value.empty()) {
        return filesystem_path_result::success;
    }

    if (output.empty()) {
        return filesystem_path_result::failed;
    }

    const auto input_size =
        static_cast<int>(
            value.size());

    const auto capacity =
        static_cast<int>(
            (std::min)(
                output.size(),
                static_cast<std::size_t>(
                    (std::numeric_limits<int>::max)())));

    const auto converted =
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            output.data(),
            capacity,
            nullptr,
            nullptr);

    if (converted <= 0) {
        return GetLastError() ==
                ERROR_NO_UNICODE_TRANSLATION
            ? filesystem_path_result::
                invalid_utf8
            : filesystem_path_result::
                failed;
    }

    written =
        static_cast<std::size_t>(
            converted);

    for (std::size_t index = 0;
         index < written;
         ++index) {

        if (output[index] == '\\') {
            output[index] = '/';
        }
    }
#else
    const std::string_view bytes{
        value.data(),
        value.size()};

    if (!valid_utf8(bytes)) {
        return filesystem_path_result::
            invalid_utf8;
    }

    if (output.size() <
        bytes.size()) {

        return filesystem_path_result::failed;
    }

    std::copy(
        bytes.begin(),
        bytes.end(),
        output.begin());

    written = bytes.size();
#endif

    return filesystem_path_result::success;
}

filesystem_path_result filesystem_path_to_utf8(
    const std::filesystem::path& value,
    std::string& output) noexcept {

    output.clear();

    try {
        const auto& native =
            value.native();

        const filesystem_native_path_view view{
            native.data(),
            native.size()};

        std::size_t required = 0;

        const auto measured =
            filesystem_path_utf8_size(
                view,
                required);

        if (measured !=
            filesystem_path_result::success) {

            return measured;
        }

        output.resize(
            required);

        std::size_t written = 0;

        const auto encoded =
            filesystem_path_to_utf8(
                view,
                std::span<char>{
                    output.data(),
                    output.size()},
                written);

        if (encoded !=
                filesystem_path_result::success ||
            written != required) {

            output.clear();

            return encoded ==
                    filesystem_path_result::success
                ? filesystem_path_result::failed
                : encoded;
        }

        return filesystem_path_result::success;
    }
    catch (...) {
        output.clear();

        return filesystem_path_result::failed;
    }
}

filesystem_path_result make_filesystem_path_key(
    const std::filesystem::path& path,
    filesystem_path_key& output) noexcept {

    output = {};

    try {
        const auto normalized =
            path.lexically_normal();

#if defined(_WIN32)
        const auto native =
            normalized.native();

        if (native.empty()) {
            output.value =
                normalized;

            return filesystem_path_result::
                success;
        }

        if (native.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<int>::max)())) {

            return filesystem_path_result::
                failed;
        }

        const auto length =
            static_cast<int>(
                native.size());

        const auto required =
            LCMapStringEx(
                LOCALE_NAME_INVARIANT,
                LCMAP_LOWERCASE,
                native.data(),
                length,
                nullptr,
                0,
                nullptr,
                nullptr,
                0);

        if (required <= 0) {
            return filesystem_path_result::
                failed;
        }

        std::wstring lowered(
            static_cast<std::size_t>(
                required),
            L'\0');

        const auto written =
            LCMapStringEx(
                LOCALE_NAME_INVARIANT,
                LCMAP_LOWERCASE,
                native.data(),
                length,
                lowered.data(),
                required,
                nullptr,
                nullptr,
                0);

        if (written != required) {
            return filesystem_path_result::
                failed;
        }

        output.value =
            std::filesystem::path{
                std::move(lowered)};
#else
        output.value =
            normalized;
#endif

        return filesystem_path_result::
            success;
    }
    catch (...) {
        output = {};

        return filesystem_path_result::
            failed;
    }
}


}
