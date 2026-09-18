/*
 * diagnostic_source_cache implementation.
 */
#include "diagnostic_source_cache.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::uint32_t narrow_offset(
    std::size_t value) noexcept {

    const auto maximum =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    return static_cast<std::uint32_t>(
        value > maximum
            ? maximum
            : value);
}

}

diagnostic_file_id diagnostic_source_cache::add(
    std::filesystem::path path,
    std::string text) {

    diagnostic_source_file file;
    file.id =
        static_cast<diagnostic_file_id>(
            files.size());
    file.path =
        std::move(path);
    file.text =
        std::move(text);

    build_line_offsets(file);

    files.push_back(
        std::move(file));

    return files.back().id;
}

const diagnostic_source_file*
diagnostic_source_cache::find(
    diagnostic_file_id id) const noexcept {

    const auto index =
        static_cast<std::size_t>(id);

    if (index >= files.size()) {
        return nullptr;
    }

    return &files[index];
}

std::string_view diagnostic_source_cache::line_text(
    diagnostic_file_id id,
    std::uint32_t line) const noexcept {

    const auto* file =
        find(id);

    if (file == nullptr ||
        line == 0 ||
        line > file->line_offsets.size()) {
        return {};
    }

    const auto begin =
        static_cast<std::size_t>(
            file->line_offsets[line - 1]);

    const auto end =
        line < file->line_offsets.size()
            ? static_cast<std::size_t>(
                  file->line_offsets[line])
            : file->text.size();

    std::string_view value(
        file->text.data() + begin,
        end - begin);

    while (!value.empty() &&
           (value.back() == '\n' ||
            value.back() == '\r')) {
        value.remove_suffix(1);
    }

    return value;
}

diagnostic_location diagnostic_source_cache::locate(
    diagnostic_file_id id,
    std::size_t offset,
    std::size_t length) const noexcept {

    diagnostic_location result;
    result.file_id = id;

    const auto* file =
        find(id);

    if (file == nullptr) {
        result.offset = narrow_offset(offset);
        result.length = narrow_offset(length);
        return result;
    }

    result.file = file->path;

    const auto safe_offset =
        std::min(
            offset,
            file->text.size());

    result.offset =
        narrow_offset(safe_offset);

    result.length =
        narrow_offset(
            std::min(
                length,
                file->text.size() - safe_offset));

    const auto position =
        std::upper_bound(
            file->line_offsets.begin(),
            file->line_offsets.end(),
            narrow_offset(safe_offset));

    const auto line_index =
        position == file->line_offsets.begin()
            ? std::size_t{0}
            : static_cast<std::size_t>(
                  std::distance(
                      file->line_offsets.begin(),
                      position) - 1);

    result.line =
        narrow_offset(line_index + 1);

    const auto line_offset =
        static_cast<std::size_t>(
            file->line_offsets[line_index]);

    result.column =
        narrow_offset(
            safe_offset - line_offset + 1);

    return result;
}

void diagnostic_source_cache::build_line_offsets(
    diagnostic_source_file& file) {

    file.line_offsets.clear();
    file.line_offsets.push_back(0);

    for (std::size_t i = 0;
         i < file.text.size();
         ++i) {

        if (file.text[i] == '\n' &&
            i + 1 < file.text.size()) {
            file.line_offsets.push_back(
                narrow_offset(i + 1));
        }
    }
}

}
