/*
 * diagnostic_collection implementation.
 */
#include "diagnostic_collection.hpp"

#include <algorithm>
#include <string_view>
#include <tuple>
#include <utility>

namespace cw::server {

void diagnostic_collection::clear() noexcept {
    records_storage.clear();
    source_storage = {};
}

void diagnostic_collection::reserve(
    std::size_t count) {

    records_storage.reserve(count);
}

void diagnostic_collection::emit(
    diagnostic_record record) {

    records_storage.push_back(
        std::move(record));
}

diagnostic_file_id diagnostic_collection::add_source(
    std::filesystem::path path,
    std::string text) {

    return source_storage.add(
        std::move(path),
        std::move(text));
}

diagnostic_location diagnostic_collection::locate(
    diagnostic_file_id file,
    std::size_t offset,
    std::size_t length) const noexcept {

    return source_storage.locate(
        file,
        offset,
        length);
}

bool diagnostic_collection::empty() const noexcept {
    return records_storage.empty();
}

bool diagnostic_collection::has_errors() const noexcept {
    return std::ranges::any_of(
        records_storage,
        [](const diagnostic_record& record) {
            return record.severity == diagnostic_severity::error ||
                   record.severity == diagnostic_severity::fatal;
        });
}

std::span<const diagnostic_record>
diagnostic_collection::records() const noexcept {
    return records_storage;
}

const diagnostic_source_cache&
diagnostic_collection::sources() const noexcept {
    return source_storage;
}

void diagnostic_collection::sort_deterministic() {
    std::ranges::stable_sort(
        records_storage,
        {},
        [](const diagnostic_record& record) {
            return std::tuple{
                record.location.file.generic_string(),
                record.location.line,
                record.location.column,
                record.location.offset,
                record.location.length,
                record.id.value(),
                record.severity,
                record.operation.value(),
                std::string_view{record.message},
                std::string_view{record.detail},
            };
        });
}

}
