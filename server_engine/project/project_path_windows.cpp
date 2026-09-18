#include "project_path.hpp"

#if !defined(_WIN32)
#error project_path_windows.cpp requires Windows
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <limits>
#include <string>
#include <utility>

namespace cw::server {

project_path_key_result make_project_path_key(
    const std::filesystem::path& path,
    project_path_key& output) noexcept {

    output = {};

    try {
        const auto normalized =
            path.lexically_normal();

        const auto native =
            normalized.native();

        if (native.empty()) {
            output.value =
                normalized;

            return project_path_key_result::
                success;
        }

        if (native.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<int>::max)())) {

            return project_path_key_result::
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
            return project_path_key_result::
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
            return project_path_key_result::
                failed;
        }

        output.value =
            std::filesystem::path{
                std::move(lowered)};

        return project_path_key_result::
            success;
    }
    catch (...) {
        output = {};

        return project_path_key_result::
            failed;
    }
}

}
