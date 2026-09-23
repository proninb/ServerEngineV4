#include "source_save.hpp"
#include "compiled_project.hpp"

#include "../../filesystem_path.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>
#endif

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'C'},
    std::byte{'W'},
    std::byte{'S'},
    std::byte{'R'},
    std::byte{'C'},
    std::byte{'0'},
    std::byte{'0'},
    std::byte{'4'},
};

constexpr std::uint32_t format_version = 4;

constexpr std::uint32_t current_member_flag = 0x00000001u;
constexpr std::uint32_t physical_present_flag = 0x00000002u;
constexpr std::uint32_t file_reference_flag = 0x00000004u;

constexpr std::uint32_t known_file_flags =
    current_member_flag |
    physical_present_flag |
    file_reference_flag;

constexpr std::uint32_t directory_watch_topology = 0x00000001u;
constexpr std::uint32_t directory_watch_known =
    directory_watch_topology;

constexpr std::size_t header_size = 80;
constexpr std::size_t record_size = 72;
constexpr std::size_t path_index_record_size = 8;
constexpr std::size_t file_index_record_size = 12;
constexpr std::size_t directory_index_record_size = 12;
constexpr std::size_t checksum_size = 32;

struct path_index_slot final {
    std::uint32_t fingerprint = 0;
    file_id file{};
};

struct file_index_slot final {
    std::uint64_t file_reference = 0;
    file_id file{};
};

struct directory_index_slot final {
    std::uint64_t file_reference = 0;
    std::uint32_t flags = 0;
};

[[nodiscard]] bool write_u32(
    std::span<std::byte> output,
    std::size_t& offset,
    std::uint32_t value) noexcept {

    if (offset > output.size() ||
        output.size() - offset < 4) {

        return false;
    }

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU);
    }

    offset += 4;

    return true;
}

[[nodiscard]] bool write_u64(
    std::span<std::byte> output,
    std::size_t& offset,
    std::uint64_t value) noexcept {

    if (offset > output.size() ||
        output.size() - offset < 8) {

        return false;
    }

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU);
    }

    offset += 8;

    return true;
}

[[nodiscard]] bool write_bytes(
    std::span<std::byte> output,
    std::size_t& offset,
    const std::byte* data,
    std::size_t size) noexcept {

    if (offset > output.size() ||
        size > output.size() - offset ||
        (size != 0 &&
         data == nullptr)) {

        return false;
    }

    if (size != 0) {
        std::copy_n(
            data,
            size,
            output.begin() +
                static_cast<std::ptrdiff_t>(
                    offset));
    }

    offset += size;

    return true;
}

[[nodiscard]] bool read_u32(
    std::span<const std::byte> input,
    std::size_t& offset,
    std::uint32_t& output) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 4) {

        return false;
    }

    output = 0;

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 4;
    return true;
}

[[nodiscard]] bool read_u64(
    std::span<const std::byte> input,
    std::size_t& offset,
    std::uint64_t& output) noexcept {

    if (offset > input.size() ||
        input.size() - offset < 8) {

        return false;
    }

    output = 0;

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    input[offset + index]))
            << (index * 8);
    }

    offset += 8;
    return true;
}

[[nodiscard]] bool add_size(
    std::size_t& value,
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            value) {

        return false;
    }

    value += additional;
    return true;
}

[[nodiscard]] bool multiply_size(
    std::size_t left,
    std::size_t right,
    std::size_t& output) noexcept {

    if (left != 0 &&
        right >
            (std::numeric_limits<std::size_t>::max)() /
                left) {

        return false;
    }

    output = left * right;
    return true;
}

[[nodiscard]] file_content_hash checksum(
    std::span<const std::byte> bytes) noexcept {

    return hash_file_content(
        std::string_view{
            reinterpret_cast<const char*>(
                bytes.data()),
            bytes.size()});
}

[[nodiscard]] constexpr std::uint64_t mix64(
    std::uint64_t value) noexcept {

    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

[[nodiscard]] std::size_t next_capacity(
    std::size_t count) noexcept {

    if (count == 0) {
        return 0;
    }

    const auto quarter =
        count / 4 +
        (count % 4 != 0 ? 1u : 0u);

    if (count >
        (std::numeric_limits<std::size_t>::max)() -
            quarter -
            1) {

        return 0;
    }

    const auto required =
        count +
        quarter +
        1;

    std::size_t capacity = 16;

    while (capacity < required) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() /
                2) {

            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

[[nodiscard]] std::uint32_t persisted_path_fingerprint(
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

    auto output =
        static_cast<std::uint32_t>(hash) ^
        static_cast<std::uint32_t>(hash >> 32);

    return output != 0
        ? output
        : 1u;
}

[[nodiscard]] bool insert_path_identity(
    std::span<std::byte> output,
    std::size_t index_offset,
    std::uint32_t index_count,
    std::uint32_t fingerprint,
    file_id file) noexcept {

    if (fingerprint == 0 ||
        index_count == 0 ||
        !file) {

        return false;
    }

    const auto mask =
        static_cast<std::size_t>(
            index_count - 1);

    auto position =
        static_cast<std::size_t>(
            fingerprint) &
        mask;

    for (std::size_t probe = 0;
         probe < index_count;
         ++probe) {

        const auto slot_offset =
            index_offset +
            position *
                path_index_record_size;

        std::size_t cursor =
            slot_offset;

        std::uint32_t stored_fingerprint = 0;
        std::uint32_t stored_file = 0;

        if (!read_u32(
                output,
                cursor,
                stored_fingerprint) ||
            !read_u32(
                output,
                cursor,
                stored_file)) {

            return false;
        }

        if (stored_fingerprint == 0) {
            if (stored_file != 0) {
                return false;
            }

            cursor =
                slot_offset;

            return write_u32(
                       output,
                       cursor,
                       fingerprint) &&
                write_u32(
                       output,
                       cursor,
                       file.value()) &&
                cursor ==
                    slot_offset +
                        path_index_record_size;
        }

        if (stored_file == 0) {
            return false;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

[[nodiscard]] bool insert_file_identity(
    std::vector<std::uint64_t>& references,
    std::vector<file_id>& files,
    std::uint64_t reference,
    file_id file) noexcept {

    if (reference == 0 ||
        references.empty() ||
        references.size() !=
            files.size() ||
        !file) {

        return false;
    }

    const auto mask =
        references.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(reference)) &
        mask;

    for (std::size_t probe = 0;
         probe < references.size();
         ++probe) {

        auto& slot_reference =
            references[position];

        if (slot_reference == 0) {
            slot_reference = reference;
            files[position] = file;

            return true;
        }

        if (slot_reference == reference) {
            return files[position] == file;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

[[nodiscard]] bool insert_directory_identity(
    std::vector<std::uint64_t>& references,
    std::vector<std::uint32_t>& flags_by_slot,
    std::uint64_t reference,
    std::uint32_t flags) noexcept {

    if (reference == 0 ||
        references.empty() ||
        references.size() !=
            flags_by_slot.size() ||
        flags == 0 ||
        (flags &
            ~directory_watch_known) != 0) {

        return false;
    }

    const auto mask =
        references.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(reference)) &
        mask;

    for (std::size_t probe = 0;
         probe < references.size();
         ++probe) {

        auto& slot_reference =
            references[position];

        if (slot_reference == 0) {
            slot_reference = reference;
            flags_by_slot[position] =
                flags;

            return true;
        }

        if (slot_reference == reference) {
            flags_by_slot[position] |=
                flags;

            return true;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

#if defined(_WIN32)

// Compact construction-only directory dedupe. The canonical key owns lookup
// equivalence only; the original normalized path is retained separately because
// filesystem I/O must not use a case-folded lookup key on case-sensitive paths.
class unique_directory_set final {
public:
    struct entry final {
        filesystem_path_key key;
        std::filesystem::path path;
        std::uint32_t flags = 0;
    };

    [[nodiscard]] bool insert(
        std::filesystem::path path,
        std::uint32_t flags,
        bool& inserted) noexcept {

        inserted = false;

        if (path.empty() ||
            flags == 0 ||
            (flags &
                ~directory_watch_known) != 0) {

            return false;
        }

        try {
            filesystem_path_key key;

            if (make_filesystem_path_key(
                    path,
                    key) !=
                filesystem_path_result::success ||
                key.value.empty()) {

                return false;
            }

            if (slots.empty() ||
                (entries.size() + 1) * 2 >=
                    slots.size()) {

                const auto requested =
                    slots.empty()
                    ? std::size_t{16}
                    : slots.size() * 2;

                if (requested <
                        slots.size() ||
                    !grow(requested)) {

                    return false;
                }
            }

            const auto hash =
                filesystem_path_key_hash{}(
                    key);

            const auto mask =
                slots.size() - 1;

            auto position =
                hash &
                mask;

            for (std::size_t probe = 0;
                 probe < slots.size();
                 ++probe) {

                const auto stored =
                    slots[position];

                if (stored == 0) {
                    if (entries.size() >=
                        static_cast<std::size_t>(
                            (std::numeric_limits<
                                std::uint32_t>::max)())) {

                        return false;
                    }

                    entries.push_back({
                        std::move(key),
                        path.lexically_normal(),
                        flags,
                    });

                    slots[position] =
                        static_cast<std::uint32_t>(
                            entries.size());

                    inserted = true;

                    return true;
                }

                const auto index =
                    static_cast<std::size_t>(
                        stored - 1);

                if (index >=
                    entries.size()) {

                    return false;
                }

                auto& existing =
                    entries[index];

                if (existing.key ==
                    key) {

                    existing.flags |=
                        flags;

                    return true;
                }

                position =
                    (position + 1) &
                    mask;
            }

            return false;
        }
        catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries.size();
    }

    [[nodiscard]] const std::vector<entry>&
    values() const noexcept {
        return entries;
    }

private:
    [[nodiscard]] bool grow(
        std::size_t capacity) noexcept {

        if (capacity < 16 ||
            (capacity &
                (capacity - 1)) != 0) {

            return false;
        }

        try {
            std::vector<std::uint32_t>
                replacement(
                    capacity,
                    0);

            const auto mask =
                capacity - 1;

            for (std::size_t index = 0;
                 index < entries.size();
                 ++index) {

                const auto hash =
                    filesystem_path_key_hash{}(
                        entries[index].key);

                auto position =
                    hash &
                    mask;

                while (replacement[position] != 0) {
                    position =
                        (position + 1) &
                        mask;
                }

                replacement[position] =
                    static_cast<std::uint32_t>(
                        index + 1);
            }

            slots.swap(
                replacement);

            return true;
        }
        catch (...) {
            return false;
        }
    }

    std::vector<entry> entries;
    std::vector<std::uint32_t> slots;
};

class windows_handle final {
public:
    explicit windows_handle(
        HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : handle(value) {
    }

    windows_handle(
        const windows_handle&) = delete;

    windows_handle& operator=(
        const windows_handle&) = delete;

    ~windows_handle() {
        if (handle !=
                INVALID_HANDLE_VALUE &&
            handle != nullptr) {

            CloseHandle(handle);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle !=
                INVALID_HANDLE_VALUE &&
            handle != nullptr;
    }

private:
    HANDLE handle =
        INVALID_HANDLE_VALUE;
};

[[nodiscard]] bool query_directory_identity(
    const std::filesystem::path& path,
    std::uint64_t& volume,
    std::uint64_t& reference) noexcept {

    volume = 0;
    reference = 0;

    windows_handle directory{
        CreateFileW(
            path.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr)};

    if (!directory) {
        return false;
    }

    BY_HANDLE_FILE_INFORMATION information{};

    if (GetFileInformationByHandle(
            directory.get(),
            &information) == 0) {

        return false;
    }

    volume =
        information.dwVolumeSerialNumber;

    reference =
        (static_cast<std::uint64_t>(
            information.nFileIndexHigh) << 32) |
        information.nFileIndexLow;

    return volume != 0 &&
        reference != 0;
}

[[nodiscard]] bool volume_paths(
    const std::filesystem::path& source,
    std::wstring& root,
    std::wstring& device) noexcept {

    try {
        wchar_t root_buffer[MAX_PATH]{};

        if (GetVolumePathNameW(
                source.c_str(),
                root_buffer,
                static_cast<DWORD>(
                    std::size(root_buffer))) == 0) {

            return false;
        }

        root.assign(root_buffer);

        if (root.size() >= 2 &&
            root[1] == L':') {

            device = L"\\\\.\\";
            device.push_back(root[0]);
            device.push_back(L':');
            return true;
        }

        wchar_t volume_buffer[MAX_PATH]{};

        if (GetVolumeNameForVolumeMountPointW(
                root.c_str(),
                volume_buffer,
                static_cast<DWORD>(
                    std::size(volume_buffer))) == 0) {

            return false;
        }

        device.assign(volume_buffer);

        while (!device.empty() &&
               (device.back() == L'\\' ||
                device.back() == L'/')) {

            device.pop_back();
        }

        return !device.empty();
    }
    catch (...) {
        return false;
    }
}

[[nodiscard]] bool volume_serial(
    const std::wstring& root,
    std::uint64_t& output) noexcept {

    output = 0;

    DWORD serial = 0;

    if (GetVolumeInformationW(
            root.c_str(),
            nullptr,
            0,
            &serial,
            nullptr,
            nullptr,
            nullptr,
            0) == 0) {

        return false;
    }

    output = serial;
    return output != 0;
}

#endif

[[nodiscard]] bool build_tracking_capture(
    const file_context& files,
    file_change_checkpoint checkpoint,
    file_change_checkpoint& output_checkpoint,
    std::vector<std::uint64_t>& file_references,
    std::vector<file_id>& file_ids,
    std::vector<std::uint64_t>& directory_references,
    std::vector<std::uint32_t>& directory_flags) noexcept {

    const auto reset = [&]() noexcept {
        output_checkpoint = {};
        file_references.clear();
        file_ids.clear();
        directory_references.clear();
        directory_flags.clear();
    };

    reset();

#if !defined(_WIN32)
    (void)files;
    (void)checkpoint;

    return true;
#else
    if (!checkpoint ||
        checkpoint.backend !=
            file_change_backend::windows_usn ||
        checkpoint.volume_serial == 0 ||
        checkpoint.journal_id == 0 ||
        checkpoint.next_usn < 0) {

        return true;
    }

    try {
        const auto file_capacity =
            next_capacity(
                files.size());

        if (file_capacity == 0) {
            return false;
        }

        file_references.assign(
            file_capacity,
            0);

        file_ids.assign(
            file_capacity,
            file_id{});

        unique_directory_set
            directories;

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            const auto* physical =
                files.physical(
                    file);

            if (physical == nullptr ||
                !physical->present() ||
                !physical->has_change_token() ||
                !physical->change_token ||
                physical->change_token.volume_serial !=
                    checkpoint.volume_serial ||
                !insert_file_identity(
                    file_references,
                    file_ids,
                    physical->change_token.
                        file_reference,
                    file)) {

                reset();

                return true;
            }

            const auto path_view =
                files.path(
                    file);

            std::filesystem::path parent{
                path_view.begin(),
                path_view.end()};

            parent =
                parent.parent_path();

            const auto root =
                parent.root_path();

            while (!parent.empty()) {
                bool inserted = false;

                if (!directories.insert(
                        parent,
                        directory_watch_topology,
                        inserted)) {

                    output_checkpoint = {};
                    file_references.clear();
                    file_ids.clear();
                    directory_references.clear();
                    directory_flags.clear();

                    return false;
                }

                if (!inserted ||
                    parent == root) {

                    break;
                }

                const auto next =
                    parent.parent_path();

                if (next == parent) {
                    break;
                }

                parent =
                    next;
            }
        }

        const auto directory_capacity =
            next_capacity(
                directories.size());

        if (directories.size() != 0 &&
            directory_capacity == 0) {

            return false;
        }

        if (directory_capacity != 0) {
            directory_references.assign(
                directory_capacity,
                0);

            directory_flags.assign(
                directory_capacity,
                0);
        }

        for (const auto& directory :
             directories.values()) {

            std::uint64_t volume = 0;
            std::uint64_t reference = 0;

            if (!query_directory_identity(
                    directory.path,
                    volume,
                    reference) ||
                volume !=
                    checkpoint.volume_serial ||
                !insert_directory_identity(
                    directory_references,
                    directory_flags,
                    reference,
                    directory.flags)) {

                reset();

                return true;
            }
        }

        output_checkpoint =
            checkpoint;

        return true;
    }
    catch (...) {
        reset();

        return false;
    }
#endif
}


[[nodiscard]] bool valid_power_of_two_or_zero(
    std::uint32_t value) noexcept {

    return value == 0 ||
        (value &
            (value - 1)) == 0;
}

[[nodiscard]] bool decode_path_index_slot(
    std::span<const std::byte> bytes,
    std::size_t offset,
    path_index_slot& output) noexcept {

    output = {};

    std::uint32_t fingerprint = 0;
    std::uint32_t file = 0;

    if (!read_u32(
            bytes,
            offset,
            fingerprint) ||
        !read_u32(
            bytes,
            offset,
            file)) {

        return false;
    }

    output.fingerprint = fingerprint;
    output.file = file_id{file};
    return true;
}

[[nodiscard]] bool decode_file_index_slot(
    std::span<const std::byte> bytes,
    std::size_t offset,
    file_index_slot& output) noexcept {

    output = {};

    std::uint64_t reference = 0;
    std::uint32_t file = 0;

    if (!read_u64(
            bytes,
            offset,
            reference) ||
        !read_u32(
            bytes,
            offset,
            file)) {

        return false;
    }

    output.file_reference =
        reference;

    output.file =
        file_id{file};

    return true;
}

[[nodiscard]] bool decode_directory_index_slot(
    std::span<const std::byte> bytes,
    std::size_t offset,
    directory_index_slot& output) noexcept {

    output = {};

    return read_u64(
            bytes,
            offset,
            output.file_reference) &&
        read_u32(
            bytes,
            offset,
            output.flags);
}

}

file_id source_save_edge_view::operator[](
    std::size_t index) const noexcept {

    if (index >= count) {
        return {};
    }

    const auto offset =
        index *
        sizeof(std::uint32_t);

    if (offset > bytes.size() ||
        bytes.size() - offset <
            sizeof(std::uint32_t)) {

        return {};
    }

    std::size_t cursor =
        offset;

    std::uint32_t value = 0;

    return read_u32(
            bytes,
            cursor,
            value)
        ? file_id{value}
        : file_id{};
}

void source_save_view::reset() noexcept {
    presence_offset = 0;
    type_count = object_count = link_count = 0;
    bytes = {};
    records_offset = 0;
    paths_offset = 0;
    path_index_offset = 0;
    forward_offset = 0;
    reverse_offset = 0;
    file_index_offset = 0;
    directory_index_offset = 0;
    file_count_value = 0;
    path_bytes_value = 0;
    path_index_count_value = 0;
    forward_count_value = 0;
    reverse_count_value = 0;
    file_index_count_value = 0;
    directory_index_count_value = 0;
    checkpoint = {};
}

source_save_result source_save_view::bind(
    std::span<const std::byte> image) noexcept {

    reset();

    if (image.size() <
            header_size +
            record_size +
            checksum_size ||
        !std::equal(
            magic.begin(),
            magic.end(),
            image.begin())) {

        return source_save_result::
            invalid_image;
    }

    std::size_t offset =
        magic.size();

    std::uint32_t version = 0;
    std::uint32_t backend = 0;

    std::uint64_t volume = 0;
    std::uint64_t journal = 0;
    std::uint64_t next_usn = 0;

    if (!read_u32(image, offset, version) || version != format_version ||
        !read_u32(image, offset, file_count_value) || file_count_value == 0 ||
        !read_u32(image, offset, path_bytes_value) ||
        !read_u32(image, offset, forward_count_value) ||
        !read_u32(image, offset, reverse_count_value) ||
        forward_count_value != reverse_count_value || !read_u32(image, offset, backend) ||
        backend > static_cast<std::uint32_t>(file_change_backend::windows_usn) ||
        !read_u32(image, offset, file_index_count_value) ||
        !valid_power_of_two_or_zero(file_index_count_value) ||
        !read_u32(image, offset, directory_index_count_value) ||
        !valid_power_of_two_or_zero(directory_index_count_value) ||
        !read_u32(image, offset, path_index_count_value) ||
        path_index_count_value == 0 ||
        !valid_power_of_two_or_zero(path_index_count_value) ||
        !read_u64(image, offset, volume) ||
        !read_u64(image, offset, journal) || !read_u64(image, offset, next_usn) ||
        !read_u32(image, offset, type_count) || !read_u32(image, offset, object_count) ||
        !read_u32(image, offset, link_count) || offset != header_size) {

        reset();
        return source_save_result::
            invalid_image;
    }

    checkpoint.backend =
        static_cast<file_change_backend>(
            backend);

    checkpoint.volume_serial =
        volume;

    checkpoint.journal_id =
        journal;

    checkpoint.next_usn =
        static_cast<std::int64_t>(
            next_usn);

    if (!checkpoint) {
        if (volume != 0 ||
            journal != 0 ||
            next_usn != 0 ||
            file_index_count_value != 0 ||
            directory_index_count_value != 0) {

            reset();
            return source_save_result::
                invalid_image;
        }
    } else if (
        checkpoint.volume_serial == 0 ||
        checkpoint.journal_id == 0 ||
        checkpoint.next_usn < 0 ||
        file_index_count_value == 0) {

        reset();
        return source_save_result::
            invalid_image;
    }

    std::size_t records_size = 0;
    std::size_t path_index_size = 0;
    std::size_t forward_size = 0;
    std::size_t reverse_size = 0;
    std::size_t file_index_size = 0;
    std::size_t directory_index_size = 0;

    if (!multiply_size(
            file_count_value,
            record_size,
            records_size) ||
        !multiply_size(
            path_index_count_value,
            path_index_record_size,
            path_index_size) ||
        !multiply_size(
            forward_count_value,
            sizeof(std::uint32_t),
            forward_size) ||
        !multiply_size(
            reverse_count_value,
            sizeof(std::uint32_t),
            reverse_size) ||
        !multiply_size(
            file_index_count_value,
            file_index_record_size,
            file_index_size) ||
        !multiply_size(
            directory_index_count_value,
            directory_index_record_size,
            directory_index_size)) {

        reset();
        return source_save_result::
            invalid_image;
    }

    std::size_t presence_size = 0, presence_words = type_count;
    if (!add_size(presence_words, type_count) || !add_size(presence_words, object_count) ||
        !add_size(presence_words, link_count) || !multiply_size(presence_words, 4, presence_size)) {
        reset();
        return source_save_result::invalid_image;
    }
    std::size_t expected =
        header_size;

    if (!add_size(expected, records_size) || !add_size(expected, path_bytes_value) ||
        !add_size(expected, path_index_size) ||
        !add_size(expected, forward_size) || !add_size(expected, reverse_size) ||
        !add_size(expected, file_index_size) || !add_size(expected, directory_index_size) ||
        !add_size(expected, presence_size) || !add_size(expected, checksum_size) ||
        expected != image.size()) {

        reset();
        return source_save_result::
            invalid_image;
    }

    records_offset =
        header_size;

    paths_offset =
        records_offset +
        records_size;

    path_index_offset =
        paths_offset +
        path_bytes_value;

    forward_offset =
        path_index_offset +
        path_index_size;

    reverse_offset =
        forward_offset +
        forward_size;

    file_index_offset =
        reverse_offset +
        reverse_size;

    directory_index_offset =
        file_index_offset +
        file_index_size;

    presence_offset = directory_index_offset + directory_index_size;
    bytes = image;

    return source_save_result::success;
}

bool source_save_view::contains(
    file_id file) const noexcept {

    return valid() &&
        file &&
        file.value() <=
            file_count_value;
}

bool source_save_view::file(
    file_id id,
    source_save_file_view& output) const noexcept {

    output = {};

    if (!contains(id)) {
        return false;
    }

    auto offset =
        records_offset +
        static_cast<std::size_t>(
            id.value() - 1) *
            record_size;

    std::uint32_t path_offset = 0;
    std::uint32_t path_size = 0;
    std::uint32_t kind = 0;
    std::uint32_t flags = 0;
    std::uint64_t file_reference = 0;
    std::uint32_t dependency_offset = 0;
    std::uint32_t dependency_count = 0;
    std::uint32_t dependent_offset = 0;
    std::uint32_t dependent_count = 0;

    if (!read_u32(
            bytes,
            offset,
            path_offset) ||
        !read_u32(
            bytes,
            offset,
            path_size) ||
        path_size == 0 ||
        !read_u32(
            bytes,
            offset,
            kind) ||
        kind >
            static_cast<std::uint32_t>(
                file_kind::assign) ||
        !read_u32(
            bytes,
            offset,
            flags) ||
        (flags &
            ~known_file_flags) != 0) {

        return false;
    }

    file_content_hash content_hash{};

    if (offset > bytes.size() ||
        bytes.size() - offset <
            content_hash.bytes.size()) {

        return false;
    }

    std::copy_n(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                offset),
        content_hash.bytes.size(),
        content_hash.bytes.begin());

    offset +=
        content_hash.bytes.size();

    if (!read_u64(
            bytes,
            offset,
            file_reference) ||
        !read_u32(
            bytes,
            offset,
            dependency_offset) ||
        !read_u32(
            bytes,
            offset,
            dependency_count) ||
        !read_u32(
            bytes,
            offset,
            dependent_offset) ||
        !read_u32(
            bytes,
            offset,
            dependent_count) ||
        path_offset >
            path_bytes_value ||
        path_size >
            path_bytes_value -
                path_offset ||
        dependency_offset >
            forward_count_value ||
        dependency_count >
            forward_count_value -
                dependency_offset ||
        dependent_offset >
            reverse_count_value ||
        dependent_count >
            reverse_count_value -
                dependent_offset) {

        return false;
    }

    const auto current =
        (flags &
            current_member_flag) != 0;

    const auto present =
        (flags &
            physical_present_flag) != 0;

    const auto has_reference =
        (flags &
            file_reference_flag) != 0;

    if (current &&
        !present) {

        return false;
    }

    if (has_reference !=
        (file_reference != 0)) {

        return false;
    }

    output.file = id;

    output.kind =
        static_cast<file_kind>(
            kind);

    output.current_member =
        current;

    output.physical.content_hash =
        content_hash;

    if (present) {
        output.physical.flags |=
            file_physical_present;
    }

    output.file_reference =
        file_reference;

    output.path_utf8 = {
        reinterpret_cast<const char*>(
            bytes.data() +
            paths_offset +
            path_offset),
        path_size,
    };

    output.dependencies.bytes =
        bytes.subspan(
            forward_offset +
                static_cast<std::size_t>(
                    dependency_offset) *
                    sizeof(std::uint32_t),
            static_cast<std::size_t>(
                dependency_count) *
                sizeof(std::uint32_t));

    output.dependencies.count =
        dependency_count;

    output.dependents.bytes =
        bytes.subspan(
            reverse_offset +
                static_cast<std::size_t>(
                    dependent_offset) *
                    sizeof(std::uint32_t),
            static_cast<std::size_t>(
                dependent_count) *
                sizeof(std::uint32_t));

    output.dependents.count =
        dependent_count;

    return true;
}

server_status source_save_view::find_path(
    const std::filesystem::path& value,
    file_id& output) const noexcept {

    output = {};

    if (!valid() ||
        path_index_count_value == 0) {

        return server_status::
            project_artifact_invalid;
    }

    std::filesystem::path resolved;

    if (resolve_project_path(
            value,
            resolved) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    filesystem_path_key key;

    if (make_filesystem_path_key(
            resolved,
            key) !=
        filesystem_path_result::success) {

        return server_status::io_error;
    }

    const auto fingerprint =
        persisted_path_fingerprint(
            key);

    const auto mask =
        static_cast<std::size_t>(
            path_index_count_value - 1);

    auto position =
        static_cast<std::size_t>(
            fingerprint) &
        mask;

    for (std::size_t probe = 0;
         probe < path_index_count_value;
         ++probe) {

        path_index_slot slot;

        if (!decode_path_index_slot(
                bytes,
                path_index_offset +
                    position *
                        path_index_record_size,
                slot)) {

            return server_status::
                project_artifact_invalid;
        }

        if (slot.fingerprint == 0) {
            return slot.file
                ? server_status::project_artifact_invalid
                : server_status::success;
        }

        if (!slot.file ||
            !contains(slot.file)) {

            return server_status::
                project_artifact_invalid;
        }

        if (slot.fingerprint ==
            fingerprint) {

            source_save_file_view state;

            if (!file(
                    slot.file,
                    state)) {

                return server_status::
                    project_artifact_invalid;
            }

            std::filesystem::path candidate;

            if (filesystem_path_from_utf8(
                    state.path_utf8,
                    candidate) !=
                filesystem_path_result::success) {

                return server_status::
                    project_artifact_invalid;
            }

            filesystem_path_key candidate_key;

            if (make_filesystem_path_key(
                    candidate,
                    candidate_key) !=
                filesystem_path_result::success) {

                return server_status::
                    project_artifact_invalid;
            }

            if (candidate_key == key) {
                output = slot.file;
                return server_status::success;
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return server_status::
        project_artifact_invalid;
}

file_id source_save_view::find_file_reference(
    std::uint64_t reference) const noexcept {

    if (!valid() ||
        reference == 0 ||
        file_index_count_value == 0) {

        return {};
    }

    const auto mask =
        static_cast<std::size_t>(
            file_index_count_value - 1);

    auto position =
        static_cast<std::size_t>(
            mix64(reference)) &
        mask;

    for (std::size_t probe = 0;
         probe <
            file_index_count_value;
         ++probe) {

        file_index_slot slot;

        if (!decode_file_index_slot(
                bytes,
                file_index_offset +
                    position *
                        file_index_record_size,
                slot)) {

            return {};
        }

        if (slot.file_reference == 0) {
            return {};
        }

        if (slot.file_reference ==
            reference) {

            return contains(
                    slot.file)
                ? slot.file
                : file_id{};
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

std::uint32_t source_save_view::directory_watch_flags(
    std::uint64_t reference) const noexcept {

    if (!valid() ||
        reference == 0 ||
        directory_index_count_value == 0) {

        return 0;
    }

    const auto mask =
        static_cast<std::size_t>(
            directory_index_count_value - 1);

    auto position =
        static_cast<std::size_t>(
            mix64(reference)) &
        mask;

    for (std::size_t probe = 0;
         probe <
            directory_index_count_value;
         ++probe) {

        directory_index_slot slot;

        if (!decode_directory_index_slot(
                bytes,
                directory_index_offset +
                    position *
                        directory_index_record_size,
                slot)) {

            return 0;
        }

        if (slot.file_reference == 0) {
            return 0;
        }

        if (slot.file_reference ==
            reference) {

            return (slot.flags &
                    ~directory_watch_known) == 0
                ? slot.flags
                : 0;
        }

        position =
            (position + 1) &
            mask;
    }

    return 0;
}

source_save_result prepare_source_save_layout(const file_context &files,
                                              const source_map &sources,
                                              const source_save_build_options &options,
                                              source_save_layout &output) noexcept {

    output.reset();

    if (!files.dependency_topology_finalized() ||
        files.size() == 0 ||
        files.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return source_save_result::
            invalid_state;
    }

    const auto file_count =
        static_cast<std::uint32_t>(
            files.size());

    const auto path_index_capacity =
        next_capacity(
            files.size());

    if (path_index_capacity == 0 ||
        path_index_capacity >
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {

        return source_save_result::failed;
    }

    std::uint32_t path_bytes = 0;
    std::uint32_t forward_count = 0;
    std::uint32_t reverse_count = 0;

    for (std::uint32_t value = 1;
         value <= file_count;
         ++value) {

        const file_id file{value};

        const auto* physical =
            files.physical(
                file);

        if (physical == nullptr ||
            !physical->present()) {

            output.reset();

            return source_save_result::
                invalid_state;
        }

        std::size_t path_size = 0;

        if (filesystem_path_utf8_size(
                files.path(file),
                path_size) !=
                    filesystem_path_result::
                        success ||
            path_size == 0 ||
            path_size >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    path_bytes) {

            output.reset();

            return source_save_result::
                invalid_state;
        }

        path_bytes +=
            static_cast<std::uint32_t>(
                path_size);

        const auto dependencies =
            files.dependencies(
                file);

        const auto dependents =
            files.dependents(
                file);

        if (dependencies.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    forward_count ||
            dependents.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    reverse_count) {

            output.reset();

            return source_save_result::
                invalid_state;
        }

        forward_count +=
            static_cast<std::uint32_t>(
                dependencies.size());

        reverse_count +=
            static_cast<std::uint32_t>(
                dependents.size());
    }

    if (forward_count !=
        reverse_count) {

        output.reset();

        return source_save_result::
            invalid_state;
    }

    if (!build_tracking_capture(
            files,
            options.change_checkpoint,
            output.checkpoint,
            output.file_index_references,
            output.file_index_files,
            output.directory_index_references,
            output.directory_index_flags)) {

        output.reset();

        return source_save_result::failed;
    }

    if (output.file_index_references.size() !=
            output.file_index_files.size() ||
        output.directory_index_references.size() !=
            output.directory_index_flags.size() ||
        output.file_index_references.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)()) ||
        output.directory_index_references.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {

        output.reset();

        return source_save_result::failed;
    }

    const auto path_index_count =
        static_cast<std::uint32_t>(
            path_index_capacity);

    const auto file_index_count =
        static_cast<std::uint32_t>(
            output.file_index_references.size());

    const auto directory_index_count =
        static_cast<std::uint32_t>(
            output.directory_index_references.size());

    std::size_t records_size = 0;
    std::size_t path_index_size = 0;
    std::size_t forward_size = 0;
    std::size_t reverse_size = 0;
    std::size_t file_index_size = 0;
    std::size_t directory_index_size = 0;

    if (!multiply_size(
            file_count,
            record_size,
            records_size) ||
        !multiply_size(
            path_index_count,
            path_index_record_size,
            path_index_size) ||
        !multiply_size(
            forward_count,
            sizeof(std::uint32_t),
            forward_size) ||
        !multiply_size(
            reverse_count,
            sizeof(std::uint32_t),
            reverse_size) ||
        !multiply_size(
            file_index_count,
            file_index_record_size,
            file_index_size) ||
        !multiply_size(
            directory_index_count,
            directory_index_record_size,
            directory_index_size)) {

        output.reset();

        return source_save_result::failed;
    }

    std::size_t cursor =
        header_size;

    output.records_offset =
        cursor;

    if (!add_size(
            cursor,
            records_size)) {

        output.reset();

        return source_save_result::failed;
    }

    output.paths_offset =
        cursor;

    if (!add_size(
            cursor,
            path_bytes)) {

        output.reset();

        return source_save_result::failed;
    }

    output.path_index_offset =
        cursor;

    if (!add_size(
            cursor,
            path_index_size)) {

        output.reset();

        return source_save_result::failed;
    }

    output.forward_offset =
        cursor;

    if (!add_size(
            cursor,
            forward_size)) {

        output.reset();

        return source_save_result::failed;
    }

    output.reverse_offset =
        cursor;

    if (!add_size(
            cursor,
            reverse_size)) {

        output.reset();

        return source_save_result::failed;
    }

    output.file_index_offset =
        cursor;

    if (!add_size(
            cursor,
            file_index_size)) {

        output.reset();

        return source_save_result::failed;
    }

    output.directory_index_offset =
        cursor;

    if (!add_size(
            cursor,
            directory_index_size)) {

        output.reset();

        return source_save_result::failed;
    }

    if (!sources.finalized() || sources.file_entries().size() != files.size() ||
        sources.type_presence_entries().size() > UINT32_MAX ||
        sources.object_presence_entries().size() > UINT32_MAX ||
        sources.link_presence_entries().size() > UINT32_MAX) {
        output.reset();
        return source_save_result::invalid_state;
    }
    output.presence_offset = cursor;
    output.type_count = static_cast<std::uint32_t>(sources.type_presence_entries().size());
    output.object_count = static_cast<std::uint32_t>(sources.object_presence_entries().size());
    output.link_count = static_cast<std::uint32_t>(sources.link_presence_entries().size());
    std::size_t presence_words = output.type_count, presence_size = 0;
    if (!add_size(presence_words, output.type_count) ||
        !add_size(presence_words, output.object_count) ||
        !add_size(presence_words, output.link_count) ||
        !multiply_size(presence_words, 4, presence_size) || !add_size(cursor, presence_size)) {
        output.reset();
        return source_save_result::failed;
    }
    output.checksum_offset =
        cursor;

    if (!add_size(
            cursor,
            checksum_size)) {

        output.reset();

        return source_save_result::failed;
    }

    output.size_value = cursor;
    output.file_count = file_count;
    output.path_bytes = path_bytes;
    output.path_index_count = path_index_count;
    output.forward_count = forward_count;
    output.reverse_count = reverse_count;
    output.file_index_count =
        file_index_count;
    output.directory_index_count =
        directory_index_count;

    return source_save_result::success;
}

source_save_result prepare_source_save_layout(const file_context &files,
                                              const source_map &sources,
                                              source_save_layout &output) noexcept {

    return prepare_source_save_layout(files, sources, source_save_build_options{}, output);
}

source_save_result encode_source_save_image(const file_context &files,
                                            const source_map &sources,
                                            const source_save_layout &layout,
                                            std::span<std::byte> output) noexcept {

    if (!sources.finalized() || sources.file_entries().size() != files.size() ||
        sources.type_presence_entries().size() != layout.type_count ||
        sources.object_presence_entries().size() != layout.object_count ||
        sources.link_presence_entries().size() != layout.link_count)
        return source_save_result::invalid_state;
    if (layout.size_value == 0 ||
        output.size() !=
            layout.size_value ||
        !files.dependency_topology_finalized() ||
        files.size() !=
            layout.file_count ||
        layout.records_offset !=
            header_size ||
        layout.checksum_offset >
            output.size() ||
        output.size() -
            layout.checksum_offset !=
                checksum_size ||
        layout.path_index_count == 0 ||
        layout.path_index_offset >
            output.size() ||
        static_cast<std::size_t>(
            layout.path_index_count) >
            (output.size() -
                layout.path_index_offset) /
                    path_index_record_size ||
        layout.file_index_references.size() !=
            layout.file_index_count ||
        layout.file_index_files.size() !=
            layout.file_index_count ||
        layout.directory_index_references.size() !=
            layout.directory_index_count ||
        layout.directory_index_flags.size() !=
            layout.directory_index_count) {

        return source_save_result::
            invalid_state;
    }

    std::size_t header_cursor = 0;

    if (!write_bytes(output, header_cursor, magic.data(), magic.size()) ||
        !write_u32(output, header_cursor, format_version) ||
        !write_u32(output, header_cursor, layout.file_count) ||
        !write_u32(output, header_cursor, layout.path_bytes) ||
        !write_u32(output, header_cursor, layout.forward_count) ||
        !write_u32(output, header_cursor, layout.reverse_count) ||
        !write_u32(output, header_cursor, static_cast<std::uint32_t>(layout.checkpoint.backend)) ||
        !write_u32(output, header_cursor, layout.file_index_count) ||
        !write_u32(output, header_cursor, layout.directory_index_count) ||
        !write_u32(output, header_cursor, layout.path_index_count) ||
        !write_u64(output, header_cursor, layout.checkpoint.volume_serial) ||
        !write_u64(output, header_cursor, layout.checkpoint.journal_id) ||
        !write_u64(output, header_cursor, static_cast<std::uint64_t>(layout.checkpoint.next_usn)) ||
        !write_u32(output, header_cursor, layout.type_count) ||
        !write_u32(output, header_cursor, layout.object_count) ||
        !write_u32(output, header_cursor, layout.link_count) || header_cursor != header_size) {

        return source_save_result::failed;
    }

    const auto path_index_size =
        static_cast<std::size_t>(
            layout.path_index_count) *
        path_index_record_size;

    std::fill(
        output.begin() +
            static_cast<std::ptrdiff_t>(
                layout.path_index_offset),
        output.begin() +
            static_cast<std::ptrdiff_t>(
                layout.path_index_offset +
                path_index_size),
        std::byte{0});

    std::size_t record_cursor =
        layout.records_offset;

    std::size_t path_cursor = 0;

    std::uint32_t dependency_offset = 0;
    std::uint32_t dependent_offset = 0;

    for (std::uint32_t value = 1;
         value <= layout.file_count;
         ++value) {

        const file_id file{value};

        const auto* physical =
            files.physical(
                file);

        if (physical == nullptr ||
            !physical->present()) {

            return source_save_result::
                invalid_state;
        }

        const auto dependencies =
            files.dependencies(
                file);

        const auto dependents =
            files.dependents(
                file);

        if (dependencies.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    dependency_offset ||
            dependents.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    dependent_offset ||
            path_cursor >
                layout.path_bytes) {

            return source_save_result::
                invalid_state;
        }

        const auto remaining_path_bytes =
            static_cast<std::size_t>(
                layout.path_bytes) -
            path_cursor;

        std::size_t path_size = 0;

        auto* path_target =
            reinterpret_cast<char*>(
                output.data() +
                layout.paths_offset +
                path_cursor);

        const auto path_view =
            files.path(file);

        if (filesystem_path_to_utf8(
                path_view,
                std::span<char>{
                    path_target,
                    remaining_path_bytes},
                path_size) !=
                    filesystem_path_result::
                        success ||
            path_size == 0 ||
            path_size >
                remaining_path_bytes ||
            path_size >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)())) {

            return source_save_result::
                invalid_state;
        }

        try {
            filesystem_path_key path_key;

            if (make_filesystem_path_key(
                    std::filesystem::path{
                        path_view.begin(),
                        path_view.end()},
                    path_key) !=
                    filesystem_path_result::success ||
                !insert_path_identity(
                    output,
                    layout.path_index_offset,
                    layout.path_index_count,
                    persisted_path_fingerprint(
                        path_key),
                    file)) {

                return source_save_result::
                    invalid_state;
            }
        }
        catch (...) {
            return source_save_result::failed;
        }

        std::uint32_t flags =
            current_member_flag |
            physical_present_flag;

        const auto file_reference =
            physical->has_change_token() &&
                physical->change_token
            ? physical->change_token.
                file_reference
            : 0;

        if (file_reference != 0) {
            flags |=
                file_reference_flag;
        }

        if (!write_u32(
                output,
                record_cursor,
                static_cast<std::uint32_t>(
                    path_cursor)) ||
            !write_u32(
                output,
                record_cursor,
                static_cast<std::uint32_t>(
                    path_size)) ||
            !write_u32(
                output,
                record_cursor,
                static_cast<std::uint32_t>(
                    files.kind(file))) ||
            !write_u32(
                output,
                record_cursor,
                flags) ||
            !write_bytes(
                output,
                record_cursor,
                physical->content_hash.
                    bytes.data(),
                physical->content_hash.
                    bytes.size()) ||
            !write_u64(
                output,
                record_cursor,
                file_reference) ||
            !write_u32(
                output,
                record_cursor,
                dependency_offset) ||
            !write_u32(
                output,
                record_cursor,
                static_cast<std::uint32_t>(
                    dependencies.size())) ||
            !write_u32(
                output,
                record_cursor,
                dependent_offset) ||
            !write_u32(
                output,
                record_cursor,
                static_cast<std::uint32_t>(
                    dependents.size()))) {

            return source_save_result::failed;
        }

        path_cursor +=
            path_size;

        dependency_offset +=
            static_cast<std::uint32_t>(
                dependencies.size());

        dependent_offset +=
            static_cast<std::uint32_t>(
                dependents.size());
    }

    if (record_cursor !=
            layout.paths_offset ||
        path_cursor !=
            layout.path_bytes ||
        dependency_offset !=
            layout.forward_count ||
        dependent_offset !=
            layout.reverse_count) {

        return source_save_result::
            invalid_state;
    }

    std::size_t forward_cursor =
        layout.forward_offset;

    std::uint32_t forward_written = 0;

    for (std::uint32_t value = 1;
         value <= layout.file_count;
         ++value) {

        for (const auto target :
             files.dependencies(
                 file_id{value})) {

            if (!target ||
                target.value() >
                    layout.file_count ||
                !write_u32(
                    output,
                    forward_cursor,
                    target.value())) {

                return source_save_result::
                    invalid_state;
            }

            ++forward_written;
        }
    }

    if (forward_cursor !=
            layout.reverse_offset ||
        forward_written !=
            layout.forward_count) {

        return source_save_result::
            invalid_state;
    }

    std::size_t reverse_cursor =
        layout.reverse_offset;

    std::uint32_t reverse_written = 0;

    for (std::uint32_t value = 1;
         value <= layout.file_count;
         ++value) {

        for (const auto source :
             files.dependents(
                 file_id{value})) {

            if (!source ||
                source.value() >
                    layout.file_count ||
                !write_u32(
                    output,
                    reverse_cursor,
                    source.value())) {

                return source_save_result::
                    invalid_state;
            }

            ++reverse_written;
        }
    }

    if (reverse_cursor !=
            layout.file_index_offset ||
        reverse_written !=
            layout.reverse_count) {

        return source_save_result::
            invalid_state;
    }

    std::size_t file_index_cursor =
        layout.file_index_offset;

    for (std::size_t index = 0;
         index <
            layout.file_index_references.size();
         ++index) {

        if (!write_u64(
                output,
                file_index_cursor,
                layout.file_index_references[
                    index]) ||
            !write_u32(
                output,
                file_index_cursor,
                layout.file_index_files[
                    index].value())) {

            return source_save_result::failed;
        }
    }

    if (file_index_cursor !=
        layout.directory_index_offset) {

        return source_save_result::failed;
    }

    std::size_t directory_index_cursor =
        layout.directory_index_offset;

    for (std::size_t index = 0;
         index <
            layout.directory_index_references.size();
         ++index) {

        if (!write_u64(
                output,
                directory_index_cursor,
                layout.directory_index_references[
                    index]) ||
            !write_u32(
                output,
                directory_index_cursor,
                layout.directory_index_flags[
                    index])) {

            return source_save_result::failed;
        }
    }

    if (directory_index_cursor != layout.presence_offset) {

        return source_save_result::failed;
    }

    auto presence_cursor = layout.presence_offset;
    for (auto p : sources.type_presence_entries()) {
        if (p.definitions > p.declarations || !write_u32(output, presence_cursor, p.declarations) ||
            !write_u32(output, presence_cursor, p.definitions))
            return source_save_result::invalid_state;
    }
    for (auto p : sources.object_presence_entries())
        if (!write_u32(output, presence_cursor, p))
            return source_save_result::failed;
    for (auto p : sources.link_presence_entries())
        if (!write_u32(output, presence_cursor, p))
            return source_save_result::failed;
    if (presence_cursor != layout.checksum_offset)
        return source_save_result::invalid_state;

    const auto digest =
        checksum(
            output.first(
                layout.checksum_offset));

    std::size_t checksum_cursor =
        layout.checksum_offset;

    if (!write_bytes(
            output,
            checksum_cursor,
            digest.bytes.data(),
            digest.bytes.size()) ||
        checksum_cursor !=
            output.size()) {

        return source_save_result::failed;
    }

    return source_save_result::success;
}

source_save_result validate_source_save_image(
    std::span<const std::byte> image) noexcept {

    source_save_view view;

    if (view.bind(image) !=
        source_save_result::success) {

        return source_save_result::
            invalid_image;
    }

    const auto payload_size =
        image.size() -
        checksum_size;

    const auto expected =
        checksum(
            image.first(
                payload_size));

    if (!std::equal(
            expected.bytes.begin(),
            expected.bytes.end(),
            image.begin() +
                static_cast<std::ptrdiff_t>(
                    payload_size))) {

        return source_save_result::
            invalid_image;
    }

    for (std::size_t i = 0; i < view.type_presence_count(); ++i) {
        const auto p = view.type_presence(i);
        if (p.definitions > p.declarations)
            return source_save_result::invalid_image;
    }

    try {
        const auto file_count =
            static_cast<std::uint32_t>(
                view.file_count());

        // One cursor per target verifies that reverse topology is the exact
        // transpose of forward topology without an O(E) expected-edge copy.
        std::vector<std::uint32_t>
            reverse_cursor(file_count);

        // source.bin v4 persists the exact path lookup table used by
        // sparse BUILD. Every file_id must occur exactly once and every stored
        // fingerprint must match the platform filesystem-equivalence key.
        for (std::uint32_t slot = 0;
             slot <
                view.path_index_count_value;
             ++slot) {

            path_index_slot value;

            if (!decode_path_index_slot(
                    image,
                    view.path_index_offset +
                        static_cast<std::size_t>(slot) *
                            path_index_record_size,
                    value)) {

                return source_save_result::
                    invalid_image;
            }

            if (value.fingerprint == 0) {
                if (value.file) {
                    return source_save_result::
                        invalid_image;
                }

                continue;
            }

            if (!value.file ||
                !view.contains(value.file) ||
                reverse_cursor[
                    value.file.value() - 1] != 0) {

                return source_save_result::
                    invalid_image;
            }

            source_save_file_view state;
            std::filesystem::path path;
            filesystem_path_key key;

            if (!view.file(
                    value.file,
                    state) ||
                filesystem_path_from_utf8(
                    state.path_utf8,
                    path) !=
                    filesystem_path_result::success ||
                make_filesystem_path_key(
                    path,
                    key) !=
                    filesystem_path_result::success ||
                persisted_path_fingerprint(
                    key) !=
                    value.fingerprint) {

                return source_save_result::
                    invalid_image;
            }

            file_id found;

            if (!succeeded(
                    view.find_path(
                        path,
                        found)) ||
                found != value.file) {

                return source_save_result::
                    invalid_image;
            }

            reverse_cursor[
                value.file.value() - 1] = 1;
        }

        for (const auto value :
             reverse_cursor) {

            if (value == 0) {
                return source_save_result::
                    invalid_image;
            }
        }

        std::fill(
            reverse_cursor.begin(),
            reverse_cursor.end(),
            0);

        for (std::uint32_t target = 1;
             target <= file_count;
             ++target) {

            source_save_file_view state;

            if (!view.file(
                    file_id{target},
                    state) ||
                !state.current_member ||
                !state.physical.present()) {

                return source_save_result::
                    invalid_image;
            }

            std::filesystem::path path;

            if (filesystem_path_from_utf8(
                    state.path_utf8,
                    path) !=
                    filesystem_path_result::
                        success ||
                path.empty() ||
                !path.is_absolute()) {

                return source_save_result::
                    invalid_image;
            }

            std::uint32_t previous_source = 0;

            for (std::size_t index = 0;
                 index <
                    state.dependents.size();
                 ++index) {

                const auto source =
                    state.dependents[index];

                if (!view.contains(source) ||
                    source.value() <=
                        previous_source) {

                    return source_save_result::
                        invalid_image;
                }

                previous_source =
                    source.value();
            }
        }

        for (std::uint32_t source = 1;
             source <= file_count;
             ++source) {

            source_save_file_view state;

            if (!view.file(
                    file_id{source},
                    state)) {

                return source_save_result::
                    invalid_image;
            }

            for (std::size_t index = 0;
                 index <
                    state.dependencies.size();
                 ++index) {

                const auto target =
                    state.dependencies[index];

                if (!view.contains(target)) {
                    return source_save_result::
                        invalid_image;
                }

                source_save_file_view
                    target_state;

                if (!view.file(
                        target,
                        target_state)) {

                    return source_save_result::
                        invalid_image;
                }

                auto& cursor =
                    reverse_cursor[
                        target.value() - 1];

                if (cursor >=
                        target_state.dependents.size() ||
                    target_state.dependents[
                        cursor] !=
                        file_id{source}) {

                    return source_save_result::
                        invalid_image;
                }

                ++cursor;
            }
        }

        for (std::uint32_t target = 1;
             target <= file_count;
             ++target) {

            source_save_file_view state;

            if (!view.file(
                    file_id{target},
                    state) ||
                reverse_cursor[
                    target - 1] !=
                    state.dependents.size()) {

                return source_save_result::
                    invalid_image;
            }
        }

        if (view.change_checkpoint()) {
            // Reuse the topology cursor arena as dense file-index coverage.
            std::fill(
                reverse_cursor.begin(),
                reverse_cursor.end(),
                0);

            for (std::uint32_t slot = 0;
                 slot <
                    view.file_index_count_value;
                 ++slot) {

                file_index_slot value;

                if (!decode_file_index_slot(
                        image,
                        view.file_index_offset +
                            static_cast<std::size_t>(
                                slot) *
                                file_index_record_size,
                        value)) {

                    return source_save_result::
                        invalid_image;
                }

                if (value.file_reference == 0) {
                    if (value.file) {
                        return source_save_result::
                            invalid_image;
                    }

                    continue;
                }

                source_save_file_view state;

                if (!view.file(
                        value.file,
                        state) ||
                    state.file_reference !=
                        value.file_reference ||
                    reverse_cursor[
                        value.file.value() - 1] !=
                        0) {

                    return source_save_result::
                        invalid_image;
                }

                reverse_cursor[
                    value.file.value() - 1] =
                    1;
            }

            for (const auto value :
                 reverse_cursor) {

                if (value == 0) {
                    return source_save_result::
                        invalid_image;
                }
            }

            for (std::uint32_t slot = 0;
                 slot <
                    view.directory_index_count_value;
                 ++slot) {

                directory_index_slot value;

                if (!decode_directory_index_slot(
                        image,
                        view.directory_index_offset +
                            static_cast<std::size_t>(
                                slot) *
                                directory_index_record_size,
                        value) ||
                    (value.file_reference == 0 &&
                     value.flags != 0) ||
                    (value.file_reference != 0 &&
                     (value.flags == 0 ||
                      (value.flags &
                       ~directory_watch_known) != 0))) {

                    return source_save_result::
                        invalid_image;
                }
            }
        }

        return source_save_result::success;
    }
    catch (...) {
        return source_save_result::failed;
    }
}


namespace {

enum class journal_scan_result : std::uint8_t {
    success,
    fallback,
    failed,
};

#if defined(_WIN32)

[[nodiscard]] journal_scan_result scan_usn_journal(
    const source_save_view& persisted,
    std::vector<file_id>& dirty,
    source_save_change_scan_metrics& metrics) noexcept {

    dirty.clear();

    const auto checkpoint =
        persisted.change_checkpoint();

    if (!checkpoint ||
        checkpoint.backend !=
            file_change_backend::windows_usn) {

        return journal_scan_result::fallback;
    }

    source_save_file_view first;

    if (!persisted.file(
            file_id{1},
            first) ||
        first.path_utf8.empty()) {

        return journal_scan_result::fallback;
    }

    std::filesystem::path first_path;

    if (filesystem_path_from_utf8(
            first.path_utf8,
            first_path) !=
        filesystem_path_result::success) {

        return journal_scan_result::fallback;
    }

    std::wstring root;
    std::wstring device;

    if (!volume_paths(
            first_path,
            root,
            device)) {

        return journal_scan_result::fallback;
    }

    std::uint64_t serial = 0;

    if (!volume_serial(
            root,
            serial) ||
        serial !=
            checkpoint.volume_serial) {

        return journal_scan_result::fallback;
    }

    windows_handle volume{
        CreateFileW(
            device.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr)};

    if (!volume) {
        return journal_scan_result::fallback;
    }

    USN_JOURNAL_DATA_V0 journal{};
    DWORD returned = 0;

    if (DeviceIoControl(
            volume.get(),
            FSCTL_QUERY_USN_JOURNAL,
            nullptr,
            0,
            &journal,
            sizeof(journal),
            &returned,
            nullptr) == 0 ||
        returned <
            sizeof(journal) ||
        journal.UsnJournalID !=
            checkpoint.journal_id ||
        checkpoint.next_usn <
            journal.FirstUsn ||
        checkpoint.next_usn >
            journal.NextUsn) {

        return journal_scan_result::fallback;
    }

    try {
        std::vector<std::byte>
            buffer(1024u * 1024u);

        // Dense bitset gives deterministic ascending file_id enumeration
        // without sorting journal-order candidates. For 1M files this is
        // approximately 128 KiB.
        std::vector<std::uint64_t>
            candidates(
                (persisted.file_count() + 63) /
                    64);

        auto start =
            static_cast<USN>(
                checkpoint.next_usn);

        const auto target =
            journal.NextUsn;

        constexpr DWORD topology_reasons =
            USN_REASON_FILE_CREATE |
            USN_REASON_FILE_DELETE |
            USN_REASON_RENAME_OLD_NAME |
            USN_REASON_RENAME_NEW_NAME |
            USN_REASON_HARD_LINK_CHANGE |
            USN_REASON_REPARSE_POINT_CHANGE;

        while (start < target) {
            READ_USN_JOURNAL_DATA_V1 request{};

            request.StartUsn =
                start;

            request.ReasonMask =
                USN_REASON_DATA_OVERWRITE |
                USN_REASON_DATA_EXTEND |
                USN_REASON_DATA_TRUNCATION |
                topology_reasons;

            request.ReturnOnlyOnClose = 0;
            request.Timeout = 0;
            request.BytesToWaitFor = 0;

            request.UsnJournalID =
                checkpoint.journal_id;

            request.MinMajorVersion = 2;
            request.MaxMajorVersion = 2;

            returned = 0;

            if (DeviceIoControl(
                    volume.get(),
                    FSCTL_READ_USN_JOURNAL,
                    &request,
                    sizeof(request),
                    buffer.data(),
                    static_cast<DWORD>(
                        buffer.size()),
                    &returned,
                    nullptr) == 0 ||
                returned <
                    sizeof(USN)) {

                dirty.clear();
                return journal_scan_result::
                    fallback;
            }

            USN next = 0;

            std::memcpy(
                &next,
                buffer.data(),
                sizeof(next));

            std::size_t offset =
                sizeof(USN);

            while (offset < returned) {
                if (returned - offset <
                    sizeof(USN_RECORD_V2)) {

                    dirty.clear();
                    return journal_scan_result::
                        fallback;
                }

                USN_RECORD_V2 record{};

                std::memcpy(
                    &record,
                    buffer.data() + offset,
                    sizeof(record));

                if (record.RecordLength <
                        sizeof(USN_RECORD_V2) ||
                    record.RecordLength >
                        returned - offset ||
                    record.MajorVersion != 2) {

                    dirty.clear();
                    return journal_scan_result::
                        fallback;
                }

                ++metrics.journal_records;

                if ((record.FileAttributes &
                     FILE_ATTRIBUTE_DIRECTORY) != 0) {

                    if ((record.Reason &
                         topology_reasons) != 0 &&
                        (persisted.
                             directory_watch_flags(
                                 record.
                                     FileReferenceNumber) &
                         directory_watch_topology) != 0) {

                        dirty.clear();
                        return journal_scan_result::
                            fallback;
                    }
                }
                else {
                    const auto file =
                        persisted.
                            find_file_reference(
                                record.
                                    FileReferenceNumber);

                    if (file) {
                        if ((record.Reason &
                             topology_reasons) != 0) {

                            dirty.clear();
                            return journal_scan_result::
                                fallback;
                        }

                        const auto zero_based =
                            static_cast<std::size_t>(
                                file.value() - 1);

                        const auto word =
                            zero_based / 64;

                        const auto bit =
                            static_cast<std::uint32_t>(
                                zero_based % 64);

                        const auto mask =
                            std::uint64_t{1} << bit;

                        if ((candidates[word] &
                             mask) == 0) {

                            candidates[word] |=
                                mask;

                            ++metrics.matched_files;
                        }
                    }
                }

                offset +=
                    record.RecordLength;
            }

            if (next <= start) {
                dirty.clear();
                return journal_scan_result::
                    fallback;
            }

            start = next;
        }

        // A matching USN data event means that file is rebuilt. The dense
        // bitset only canonicalizes output into ascending file_id order; no
        // per-file reopen/hash is required on the journal fast path.
        for (std::size_t word = 0;
             word < candidates.size();
             ++word) {

            auto bits =
                candidates[word];

            while (bits != 0) {
                const auto bit =
                    static_cast<std::uint32_t>(
                        std::countr_zero(bits));

                const auto zero_based =
                    word * 64 +
                    bit;

                if (zero_based >=
                    persisted.file_count()) {

                    dirty.clear();
                    return journal_scan_result::
                        fallback;
                }

                const file_id file{
                    static_cast<std::uint32_t>(
                        zero_based + 1)};

                source_save_file_view state;

                if (!persisted.file(
                        file,
                        state) ||
                    !state.current_member ||
                    !state.physical.present()) {

                    dirty.clear();
                    return journal_scan_result::
                        fallback;
                }

                dirty.push_back(
                    file);

                bits &=
                    bits - 1;
            }
        }

        metrics.backend =
            file_change_backend::
                windows_usn;

        metrics.fast_path = true;

        metrics.dirty_files =
            dirty.size();

        return journal_scan_result::success;
    }
    catch (...) {
        dirty.clear();
        return journal_scan_result::failed;
    }
}

#endif

[[nodiscard]] server_status scan_exact_fallback(
    const source_save_view& persisted,
    std::vector<file_id>& dirty,
    source_save_change_scan_metrics& metrics) noexcept {

    dirty.clear();
    metrics.fallback = true;

    try {
        dirty.reserve(
            persisted.file_count() /
                32 +
            1);

        for (std::size_t index = 0;
             index <
                persisted.file_count();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            source_save_file_view state;

            if (!persisted.file(
                    file,
                    state)) {

                return server_status::
                    project_artifact_invalid;
            }

            if (!state.current_member) {
                continue;
            }

            ++metrics.current_files;

            std::filesystem::path path;

            if (filesystem_path_from_utf8(
                    state.path_utf8,
                    path) !=
                    filesystem_path_result::
                        success ||
                path.empty()) {

                return server_status::
                    project_artifact_invalid;
            }

            file_content_proof proof;

            const auto acquired =
                acquire_file_content_proof(
                    path,
                    proof);

            if (acquired ==
                file_content_result::
                    missing) {

                dirty.push_back(file);
                ++metrics.dirty_files;
                ++metrics.missing_files;
                continue;
            }

            if (acquired !=
                file_content_result::
                    acquired) {

                return server_status::io_error;
            }

            ++metrics.files_read;

            metrics.bytes_read +=
                static_cast<std::uint64_t>(
                    proof.observation.size);

            if (proof.content_hash ==
                state.physical.content_hash) {

                continue;
            }

            dirty.push_back(file);
            ++metrics.dirty_files;
        }

        return server_status::success;
    }
    catch (...) {
        dirty.clear();
        return server_status::io_error;
    }
}

}

server_status scan_source_save_changes(
    const source_save_view& persisted,
    std::vector<file_id>& dirty,
    source_save_change_scan* scan) noexcept {

    dirty.clear();

    source_save_change_scan local;

    if (!persisted.valid()) {
        if (scan != nullptr) {
            *scan = {};
        }

        return server_status::
            project_artifact_invalid;
    }

#if defined(_WIN32)
    // V3 contract: capture the checkpoint for the resulting BUILD before dirty
    // detection. Any later filesystem event remains visible to the next BUILD.
    const auto baseline_checkpoint =
        persisted.change_checkpoint();

    if (baseline_checkpoint) {
        source_save_file_view first;

        if (persisted.file(
                file_id{1},
                first) &&
            !first.path_utf8.empty()) {

            std::filesystem::path anchor;

            if (filesystem_path_from_utf8(
                    first.path_utf8,
                    anchor) ==
                filesystem_path_result::
                    success) {

                const auto captured =
                    capture_file_change_checkpoint(
                        anchor,
                        local.next_checkpoint);

                if (captured !=
                    file_token_result::available) {

                    local.next_checkpoint = {};
                }
            }
        }
    }

    const auto journal =
        scan_usn_journal(
            persisted,
            dirty,
            local.metrics);

    if (journal ==
        journal_scan_result::success) {

        local.metrics.dirty_files =
            dirty.size();

        if (scan != nullptr) {
            *scan =
                local;
        }

        return server_status::success;
    }

    if (journal ==
        journal_scan_result::failed) {

        if (scan != nullptr) {
            *scan = {};
        }

        return server_status::io_error;
    }
#endif

    // Same rule as V3: a portable full scan can determine what to rebuild, but
    // it does not advance journal identity continuity for the resulting persisted.
    local.next_checkpoint = {};

    const auto fallback =
        scan_exact_fallback(
            persisted,
            dirty,
            local.metrics);

    if (scan != nullptr) {
        *scan =
            local;
    }

    return fallback;
}

server_status collect_source_save_affected(
    const source_save_view& persisted,
    std::span<const file_id> dirty,
    std::vector<file_id>& affected) noexcept {

    affected.clear();

    if (!persisted.valid()) {
        return server_status::
            project_artifact_invalid;
    }

    try {
        std::vector<std::uint8_t>
            visited(
                persisted.file_count());

        affected.reserve(
            dirty.size());

        for (const auto file :
             dirty) {

            if (!persisted.contains(file)) {
                return server_status::
                    project_artifact_invalid;
            }

            auto& marker =
                visited[
                    file.value() - 1];

            if (marker != 0) {
                continue;
            }

            source_save_file_view state;

            if (!persisted.file(
                    file,
                    state) ||
                !state.current_member) {

                return server_status::
                    project_artifact_invalid;
            }

            marker = 1;
            affected.push_back(file);
        }

        for (std::size_t position = 0;
             position <
                affected.size();
             ++position) {

            source_save_file_view state;

            if (!persisted.file(
                    affected[position],
                    state) ||
                !state.current_member) {

                affected.clear();
                return server_status::
                    project_artifact_invalid;
            }

            for (std::size_t index = 0;
                 index <
                    state.dependents.size();
                 ++index) {

                const auto dependent =
                    state.dependents[index];

                if (!persisted.contains(dependent)) {
                    affected.clear();
                    return server_status::
                        project_artifact_invalid;
                }

                auto& marker =
                    visited[
                        dependent.value() - 1];

                // Dense marker first: common fan-in nodes are decoded once.
                if (marker != 0) {
                    continue;
                }

                source_save_file_view
                    dependent_state;

                if (!persisted.file(
                        dependent,
                        dependent_state) ||
                    !dependent_state.
                        current_member) {

                    affected.clear();
                    return server_status::
                        project_artifact_invalid;
                }

                marker = 1;
                affected.push_back(dependent);
            }
        }

        return server_status::success;
    }
    catch (...) {
        affected.clear();
        return server_status::io_error;
    }
}

source_type_presence source_save_view::type_presence(std::size_t index) const noexcept {
    source_type_presence result;
    if (!valid() || index >= type_count)
        return result;
    auto cursor = presence_offset + index * 8;
    (void)read_u32(bytes, cursor, result.declarations);
    (void)read_u32(bytes, cursor, result.definitions);
    return result;
}
std::uint32_t source_save_view::object_presence(std::size_t index) const noexcept {
    std::uint32_t result = 0;
    if (!valid() || index >= object_count)
        return result;
    auto cursor = presence_offset + std::size_t{type_count} * 8 + index * 4;
    (void)read_u32(bytes, cursor, result);
    return result;
}
std::uint32_t source_save_view::link_presence(std::size_t index) const noexcept {
    std::uint32_t result = 0;
    if (!valid() || index >= link_count)
        return result;
    auto cursor =
        presence_offset + std::size_t{type_count} * 8 + std::size_t{object_count} * 4 + index * 4;
    (void)read_u32(bytes, cursor, result);
    return result;
}
source_save_result verify_source_save_presence(const source_save_view &source,
                                               const compiled_project_view &compiled) noexcept {
    if (!source.valid() || !compiled.valid() ||
        source.file_count() != compiled.source_file_count() ||
        source.type_presence_count() != compiled.type_count() ||
        source.object_presence_count() != compiled.object_count() ||
        source.link_presence_count() != compiled.link_count() ||
        compiled.verify_sources() != compiled_project_image_result::success)
        return source_save_result::invalid_image;
    try {
        std::vector<source_type_presence> types(compiled.type_count());
        std::vector<std::uint32_t> objects(compiled.object_count()), links(compiled.link_count());
        const auto increment = [](std::uint32_t &value) {
            if (value == UINT32_MAX)
                return false;
            ++value;
            return true;
        };
        for (std::uint32_t i = 0; i < source.file_count(); ++i) {
            source_save_file_view physical;
            source_map_range file_range, range;
            std::string_view path;
            file_kind kind;
            if (!source.file(file_id{i + 1}, physical) ||
                !compiled.source_file(file_id{i + 1}, path, kind, file_range) ||
                physical.path_utf8 != path || physical.kind != kind ||
                !compiled.source_root(file_id{i + 1}, range))
                return source_save_result::invalid_image;
            for (std::uint32_t j = 0; j < range.count; ++j) {
                source_contribution_record c;
                if (!compiled.source_contribution(range.begin + j, c))
                    return source_save_result::invalid_image;
                if (c.data.kind() == source_data_kind::link) {
                    if (c.data.slot() > links.size() || !increment(links[c.data.slot() - 1]))
                        return source_save_result::invalid_image;
                } else if (c.data.kind() == source_data_kind::object) {
                    const auto handle =
                        compiled.find_object(compiled.identity_at_slot(c.data.slot()));
                    if (!handle || !increment(objects[handle.value() - 1]))
                        return source_save_result::invalid_image;
                } else {
                    const auto handle =
                        compiled.find_type(compiled.identity_at_slot(c.data.slot()));
                    if (!handle || !increment(types[handle.value() - 1].declarations))
                        return source_save_result::invalid_image;
                    if (c.data.kind() == source_data_kind::type_definition &&
                        !increment(types[handle.value() - 1].definitions))
                        return source_save_result::invalid_image;
                }
            }
        }
        for (std::size_t i = 0; i < types.size(); ++i)
            if (types[i] != source.type_presence(i))
                return source_save_result::invalid_image;
        for (std::size_t i = 0; i < objects.size(); ++i)
            if (objects[i] != source.object_presence(i))
                return source_save_result::invalid_image;
        for (std::size_t i = 0; i < links.size(); ++i)
            if (links[i] != source.link_presence(i))
                return source_save_result::invalid_image;
        return source_save_result::success;
    } catch (...) {
        return source_save_result::failed;
    }
}

} // namespace cw::server
