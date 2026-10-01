#include "assign_table.hpp"

#include "../persistence/compiled_project.hpp"

#include <algorithm>
#include <limits>

namespace cw::server {

server_status assign_table::add(
    file_id file,
    std::string_view source_value,
    std::string_view target_value) noexcept {

    if (source_value.empty() ||
        target_value.empty()) {

        return server_status::project_configuration_invalid;
    }

    constexpr auto maximum =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (source_value.size() > maximum ||
        target_value.size() > maximum ||
        bytes.size() > maximum - source_value.size() ||
        bytes.size() + source_value.size() >
            maximum - target_value.size()) {

        return server_status::project_configuration_invalid;
    }

    const auto source_offset =
        static_cast<std::uint32_t>(
            bytes.size());

    const auto target_offset =
        static_cast<std::uint32_t>(
            bytes.size() +
            source_value.size());

    try {
        entries.reserve(
            entries.size() + 1);

        files.reserve(
            files.size() + 1);

        bytes.reserve(
            bytes.size() +
            source_value.size() +
            target_value.size());
    }
    catch (...) {
        return server_status::io_error;
    }

    bytes.insert(
        bytes.end(),
        source_value.begin(),
        source_value.end());

    bytes.insert(
        bytes.end(),
        target_value.begin(),
        target_value.end());

    entries.push_back({
        source_offset,
        static_cast<std::uint32_t>(
            source_value.size()),
        target_offset,
        static_cast<std::uint32_t>(
            target_value.size()),
    });

    files.push_back(
        file);

    return server_status::success;
}

void assign_table::clear() noexcept {

    entries.clear();
    files.clear();
    bytes.clear();
}

std::string_view assign_table::source(
    const assign_record& record) const noexcept {

    return text(
        record.source_offset,
        record.source_length);
}

std::string_view assign_table::target(
    const assign_record& record) const noexcept {

    return text(
        record.target_offset,
        record.target_length);
}

std::string_view assign_table::text(
    std::uint32_t offset,
    std::uint32_t length) const noexcept {

    const auto begin =
        static_cast<std::size_t>(offset);

    const auto size =
        static_cast<std::size_t>(length);

    if (begin > bytes.size() ||
        size > bytes.size() - begin) {

        return {};
    }

    return {
        bytes.data() + begin,
        size,
    };
}

bool assign_overlay_view::replaced_file(
    file_id file) const noexcept {

    return std::binary_search(
        replaced.begin(),
        replaced.end(),
        file,
        [](file_id left,
           file_id right) noexcept {
            return left.value() <
                right.value();
        });
}

void assign_overlay_view::reset() noexcept {

    baseline = nullptr;
    replacements = nullptr;
    replaced.clear();
    record_count = 0;
    bytes_count = 0;
}

server_status assign_overlay_view::bind(
    const compiled_project_view& baseline_value,
    const assign_table& replacement_value,
    std::span<const file_id> replaced_files) noexcept {

    reset();

    if (!baseline_value.valid() ||
        replacement_value.records().size() !=
            replacement_value.file_entries().size()) {

        return server_status::
            project_artifact_invalid;
    }

    file_id previous;

    for (const auto file :
         replaced_files) {

        if (!file ||
            (previous &&
             file.value() <=
                 previous.value())) {

            return server_status::
                project_artifact_invalid;
        }

        previous =
            file;
    }

    constexpr auto maximum =
        static_cast<std::size_t>(
            (std::numeric_limits<
                std::uint32_t>::max)());

    std::size_t final_records = 0;
    std::size_t final_bytes = 0;

    file_id previous_baseline_file;

    for (std::size_t index = 0;
         index <
            baseline_value.assign_count();
         ++index) {

        file_id file;
        std::string_view source;
        std::string_view target;

        if (!baseline_value.assign_file(
                index,
                file) ||
            !baseline_value.assign(
                index,
                source,
                target) ||
            !file ||
            source.empty() ||
            target.empty() ||
            (previous_baseline_file &&
             file.value() <
                previous_baseline_file.value())) {

            return server_status::
                project_artifact_invalid;
        }

        previous_baseline_file =
            file;

        if (std::binary_search(
                replaced_files.begin(),
                replaced_files.end(),
                file,
                [](file_id left,
                   file_id right) noexcept {
                    return left.value() <
                        right.value();
                })) {

            continue;
        }

        if (final_records == maximum ||
            source.size() >
                maximum - final_bytes ||
            target.size() >
                maximum -
                final_bytes -
                source.size()) {

            return server_status::io_error;
        }

        ++final_records;
        final_bytes +=
            source.size() +
            target.size();
    }

    file_id previous_replacement_file;

    const auto records =
        replacement_value.records();

    for (std::size_t index = 0;
         index < records.size();
         ++index) {

        const auto file =
            replacement_value.file(
                index);

        const auto source =
            replacement_value.source(
                records[index]);

        const auto target =
            replacement_value.target(
                records[index]);

        if (!file ||
            source.empty() ||
            target.empty() ||
            !std::binary_search(
                replaced_files.begin(),
                replaced_files.end(),
                file,
                [](file_id left,
                   file_id right) noexcept {
                    return left.value() <
                        right.value();
                }) ||
            (previous_replacement_file &&
             file.value() <
                previous_replacement_file.value())) {

            return server_status::
                project_artifact_invalid;
        }

        previous_replacement_file =
            file;

        if (final_records == maximum ||
            source.size() >
                maximum - final_bytes ||
            target.size() >
                maximum -
                final_bytes -
                source.size()) {

            return server_status::io_error;
        }

        ++final_records;
        final_bytes +=
            source.size() +
            target.size();
    }

    try {
        replaced.assign(
            replaced_files.begin(),
            replaced_files.end());
    }
    catch (...) {
        reset();
        return server_status::io_error;
    }

    baseline =
        &baseline_value;

    replacements =
        &replacement_value;

    record_count =
        final_records;

    bytes_count =
        final_bytes;

    return server_status::success;
}

server_status assign_overlay_view::visit(
    void* context,
    visitor_function visitor) const noexcept {

    if (!valid() ||
        visitor == nullptr) {

        return server_status::
            project_configuration_invalid;
    }

    std::size_t baseline_index = 0;
    std::size_t replacement_index = 0;
    std::size_t emitted = 0;

    const auto replacement_records =
        replacements->records();

    while (true) {
        file_id baseline_file;
        std::string_view baseline_source;
        std::string_view baseline_target;

        while (baseline_index <
               baseline->assign_count()) {

            if (!baseline->assign_file(
                    baseline_index,
                    baseline_file) ||
                !baseline->assign(
                    baseline_index,
                    baseline_source,
                    baseline_target)) {

                return server_status::
                    project_artifact_invalid;
            }

            if (!replaced_file(
                    baseline_file)) {

                break;
            }

            ++baseline_index;
        }

        if (baseline_index >=
            baseline->assign_count()) {

            baseline_file = {};
            baseline_source = {};
            baseline_target = {};
        }
        const auto replacement_file =
            replacement_index <
                    replacement_records.size()
                ? replacements->file(
                    replacement_index)
                : file_id{};

        if (!baseline_file &&
            !replacement_file) {

            break;
        }

        if (!replacement_file ||
            (baseline_file &&
             baseline_file.value() <
                replacement_file.value())) {

            const auto visited =
                visitor(
                    context,
                    baseline_file,
                    baseline_source,
                    baseline_target);

            if (!succeeded(visited)) {
                return visited;
            }

            ++baseline_index;
            ++emitted;
            continue;
        }

        if (baseline_file &&
            baseline_file ==
                replacement_file) {

            return server_status::
                project_artifact_invalid;
        }

        const auto& record =
            replacement_records[
                replacement_index];

        const auto source =
            replacements->source(
                record);

        const auto target =
            replacements->target(
                record);

        const auto visited =
            visitor(
                context,
                replacement_file,
                source,
                target);

        if (!succeeded(visited)) {
            return visited;
        }

        ++replacement_index;
        ++emitted;
    }

    return emitted == record_count
        ? server_status::success
        : server_status::
            project_artifact_invalid;
}

}
