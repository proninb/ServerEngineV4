/*
 * SourceSave mmap read-format primitives.
 *
 * This internal header owns constants and path fingerprinting shared by the
 * persistence engine and the lightweight read-only SourceSave view.
 */
#pragma once

#include "../../filesystem_path.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <type_traits>

namespace cw::server {

inline constexpr std::uint32_t source_save_current_member_flag =
    0x00000001u;

inline constexpr std::uint32_t source_save_physical_present_flag =
    0x00000002u;

inline constexpr std::uint32_t source_save_file_reference_flag =
    0x00000004u;

inline constexpr std::uint32_t source_save_known_file_flags =
    source_save_current_member_flag |
    source_save_physical_present_flag |
    source_save_file_reference_flag;

inline constexpr std::size_t source_save_record_size = 72;
inline constexpr std::size_t source_save_path_index_record_size = 8;

[[nodiscard]] inline std::uint32_t source_save_path_fingerprint(
    const filesystem_path_key& key) noexcept {

    constexpr std::uint64_t offset =
        1469598103934665603ULL;

    constexpr std::uint64_t prime =
        1099511628211ULL;

    std::uint64_t hash = offset;

    using native_char =
        std::filesystem::path::value_type;

    using unsigned_char =
        std::make_unsigned_t<native_char>;

    for (const auto value :
         key.value.native()) {

        auto code =
            static_cast<std::uint64_t>(
                static_cast<unsigned_char>(
                    value));

#if defined(_WIN32)
        if (code ==
            static_cast<std::uint64_t>(
                L'\\')) {

            code =
                static_cast<std::uint64_t>(
                    L'/');
        }
#endif

        hash ^= code;
        hash *= prime;
    }

    const auto output =
        static_cast<std::uint32_t>(hash) ^
        static_cast<std::uint32_t>(
            hash >> 32);

    return output != 0
        ? output
        : 1u;
}

}
