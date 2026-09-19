#include "file_context.hpp"

#include <cassert>
#include <limits>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t next_index_capacity(
    std::size_t required) noexcept {

    std::size_t capacity = 16;

    while (capacity / 2 < required) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

}

std::uint32_t file_context::fingerprint(
    const project_path_key& key) noexcept {

    const auto value =
        static_cast<std::uint64_t>(
            project_path_key_hash{}(key));

    auto output =
        static_cast<std::uint32_t>(value) ^
        static_cast<std::uint32_t>(value >> 32);

    if (output == 0) {
        output = 1;
    }

    return output;
}

server_status file_context::find_key(
    const project_path_key& key,
    std::uint32_t hash,
    file_id& output) const noexcept {

    output = {};

    if (path_index.empty()) {
        return server_status::success;
    }

    const auto mask =
        path_index.size() - 1;

    auto position =
        static_cast<std::size_t>(hash) &
        mask;

    for (std::size_t probe = 0;
         probe < path_index.size();
         ++probe) {

        const auto& slot =
            path_index[position];

        if (!slot.file) {
            return server_status::success;
        }

        if (slot.fingerprint == hash &&
            files[slot.file.value() - 1].key == key) {

            output = slot.file;
            return server_status::success;
        }

        position =
            (position + 1) &
            mask;
    }

    return server_status::io_error;
}

void file_context::insert_index(
    std::vector<path_slot>& index,
    file_id file,
    std::uint32_t hash) const noexcept {

    const auto mask =
        index.size() - 1;

    auto position =
        static_cast<std::size_t>(hash) &
        mask;

    while (index[position].file) {
        position =
            (position + 1) &
            mask;
    }

    index[position] = {
        hash,
        file,
    };
}

server_status file_context::ensure_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            files.size()) {

        return server_status::io_error;
    }

    const auto required =
        files.size() + additional;

    if (!path_index.empty() &&
        required <= path_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<path_slot> candidate(
            capacity);

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            insert_index(
                candidate,
                file,
                fingerprint(files[index].key));
        }

        path_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status file_context::resolve(
    const std::filesystem::path& value,
    file_id& output) noexcept {

    output = {};

    std::filesystem::path resolved;

    if (resolve_project_path(
            value,
            resolved) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    project_path_key key;

    if (make_project_path_key(
            resolved,
            key) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    const auto hash =
        fingerprint(key);

    const auto found =
        find_key(
            key,
            hash,
            output);

    if (!succeeded(found) || output) {
        return found;
    }

    if (files.size() >=
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    try {
        files.push_back({
            std::move(resolved),
            std::move(key),
            root_role::none,
        });

        output = file_id{
            static_cast<std::uint32_t>(
                files.size())};

        insert_index(
            path_index,
            output,
            hash);

        return server_status::success;
    }
    catch (...) {
        output = {};
        return server_status::io_error;
    }
}

server_status file_context::find(
    const std::filesystem::path& value,
    file_id& output) const noexcept {

    output = {};

    std::filesystem::path resolved;

    if (resolve_project_path(
            value,
            resolved) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    project_path_key key;

    if (make_project_path_key(
            resolved,
            key) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    return find_key(
        key,
        fingerprint(key),
        output);
}

server_status file_context::add_root(
    file_id file,
    file_role role) noexcept {

    if (!contains(file)) {
        return server_status::project_configuration_invalid;
    }

    auto& record =
        files[file.value() - 1];

    const auto required =
        role == file_role::type
        ? root_role::type
        : root_role::source;

    if (record.role == required) {
        return server_status::success;
    }

    if (record.role != root_role::none) {
        return server_status::project_configuration_invalid;
    }

    try {
        root_files.push_back({
            file,
            role,
        });

        record.role = required;
        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

bool file_context::contains(
    file_id file) const noexcept {

    return file &&
        static_cast<std::size_t>(
            file.value()) <=
            files.size();
}

const std::filesystem::path& file_context::path(
    file_id file) const noexcept {

    assert(contains(file));
    return files[file.value() - 1].path;
}

}
