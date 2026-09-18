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

#include <string>
#include <utility>

namespace cw::server {

project_path_key make_project_path_key(
    const std::filesystem::path& path) {

    const auto normalized =
        path.lexically_normal();

    const auto native =
        normalized.native();

    if (native.empty()) {
        return {
            normalized,
        };
    }

    const auto required =
        LCMapStringEx(
            LOCALE_NAME_INVARIANT,
            LCMAP_LOWERCASE,
            native.data(),
            static_cast<int>(
                native.size()),
            nullptr,
            0,
            nullptr,
            nullptr,
            0);

    if (required <= 0) {
        return {
            normalized,
        };
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
            static_cast<int>(
                native.size()),
            lowered.data(),
            required,
            nullptr,
            nullptr,
            0);

    if (written != required) {
        return {
            normalized,
        };
    }

    return {
        std::filesystem::path{
            std::move(lowered)},
    };
}

}
