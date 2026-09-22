#include "assign_table.hpp"

#include <limits>

namespace cw::server {

server_status assign_table::add(
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

        bytes.reserve(
            bytes.size() +
            source_value.size() +
            target_value.size());

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

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

void assign_table::clear() noexcept {

    entries.clear();
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

}
