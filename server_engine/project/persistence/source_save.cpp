#include "source_save.hpp"

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
#include <unordered_set>
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
    std::byte{'C'}, std::byte{'W'}, std::byte{'S'}, std::byte{'R'},
    std::byte{'C'}, std::byte{'0'}, std::byte{'0'}, std::byte{'2'},
};

constexpr std::uint32_t format_version = 2;

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
constexpr std::size_t file_index_record_size = 12;
constexpr std::size_t directory_index_record_size = 12;
constexpr std::size_t checksum_size = 32;

struct file_index_slot final {
    std::uint64_t file_reference = 0;
    file_id file{};
};

struct directory_index_slot final {
    std::uint64_t file_reference = 0;
    std::uint32_t flags = 0;
};

struct tracking_capture final {
    file_change_checkpoint checkpoint;
    std::vector<file_index_slot> files;
    std::vector<directory_index_slot> directories;

    void reset() noexcept {
        checkpoint = {};
        files.clear();
        directories.clear();
    }
};

void append_u32(
    std::vector<std::byte>& output,
    std::uint32_t value) {

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_u64(
    std::vector<std::byte>& output,
    std::uint64_t value) {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output.push_back(
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffU));
    }
}

void append_bytes(
    std::vector<std::byte>& output,
    const std::byte* data,
    std::size_t size) {

    if (size != 0) {
        output.insert(
            output.end(),
            data,
            data + size);
    }
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

[[nodiscard]] bool insert_file_identity(
    std::vector<file_index_slot>& slots,
    std::uint64_t reference,
    file_id file) noexcept {

    if (reference == 0 ||
        slots.empty() ||
        !file) {

        return false;
    }

    const auto mask =
        slots.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(reference)) &
        mask;

    for (std::size_t probe = 0;
         probe < slots.size();
         ++probe) {

        auto& slot =
            slots[position];

        if (slot.file_reference == 0) {
            slot.file_reference = reference;
            slot.file = file;
            return true;
        }

        if (slot.file_reference == reference) {
            return slot.file == file;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

[[nodiscard]] bool insert_directory_identity(
    std::vector<directory_index_slot>& slots,
    std::uint64_t reference,
    std::uint32_t flags) noexcept {

    if (reference == 0 ||
        slots.empty() ||
        flags == 0 ||
        (flags &
            ~directory_watch_known) != 0) {

        return false;
    }

    const auto mask =
        slots.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(reference)) &
        mask;

    for (std::size_t probe = 0;
         probe < slots.size();
         ++probe) {

        auto& slot =
            slots[position];

        if (slot.file_reference == 0) {
            slot.file_reference = reference;
            slot.flags = flags;
            return true;
        }

        if (slot.file_reference == reference) {
            slot.flags |= flags;
            return true;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

#if defined(_WIN32)

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

[[nodiscard]] std::wstring directory_key(
    const std::filesystem::path& path) {

    auto value =
        path.lexically_normal().
            native();

    for (auto& character :
         value) {

        if (character >= L'A' &&
            character <= L'Z') {

            character =
                static_cast<wchar_t>(
                    character - L'A' + L'a');
        }
    }

    return value;
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
    tracking_capture& output) noexcept {

    output.reset();

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

        output.files.assign(
            file_capacity,
            file_index_slot{});

        std::unordered_set<std::wstring>
            known_directories;

        std::vector<std::filesystem::path>
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
                    output.files,
                    physical->change_token.
                        file_reference,
                    file)) {

                output.reset();
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
                const auto key =
                    directory_key(
                        parent);

                const auto inserted =
                    known_directories.
                        insert(key).
                        second;

                if (inserted) {
                    directories.push_back(
                        parent);
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

        if (!directories.empty() &&
            directory_capacity == 0) {

            return false;
        }

        if (directory_capacity != 0) {
            output.directories.assign(
                directory_capacity,
                directory_index_slot{});
        }

        for (const auto& directory :
             directories) {

            std::uint64_t volume = 0;
            std::uint64_t reference = 0;

            if (!query_directory_identity(
                    directory,
                    volume,
                    reference) ||
                volume !=
                    checkpoint.volume_serial ||
                !insert_directory_identity(
                    output.directories,
                    reference,
                    directory_watch_topology)) {

                output.reset();
                return true;
            }
        }

        output.checkpoint =
            checkpoint;

        return true;
    }
    catch (...) {
        output.reset();
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
    bytes = {};
    records_offset = 0;
    paths_offset = 0;
    forward_offset = 0;
    reverse_offset = 0;
    file_index_offset = 0;
    directory_index_offset = 0;
    file_count_value = 0;
    path_bytes_value = 0;
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
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
    std::uint32_t reserved2 = 0;
    std::uint32_t reserved3 = 0;

    std::uint64_t volume = 0;
    std::uint64_t journal = 0;
    std::uint64_t next_usn = 0;

    if (!read_u32(
            image,
            offset,
            version) ||
        version != format_version ||
        !read_u32(
            image,
            offset,
            file_count_value) ||
        file_count_value == 0 ||
        !read_u32(
            image,
            offset,
            path_bytes_value) ||
        !read_u32(
            image,
            offset,
            forward_count_value) ||
        !read_u32(
            image,
            offset,
            reverse_count_value) ||
        forward_count_value !=
            reverse_count_value ||
        !read_u32(
            image,
            offset,
            backend) ||
        backend >
            static_cast<std::uint32_t>(
                file_change_backend::
                    windows_usn) ||
        !read_u32(
            image,
            offset,
            file_index_count_value) ||
        !valid_power_of_two_or_zero(
            file_index_count_value) ||
        !read_u32(
            image,
            offset,
            directory_index_count_value) ||
        !valid_power_of_two_or_zero(
            directory_index_count_value) ||
        !read_u32(
            image,
            offset,
            reserved0) ||
        reserved0 != 0 ||
        !read_u64(
            image,
            offset,
            volume) ||
        !read_u64(
            image,
            offset,
            journal) ||
        !read_u64(
            image,
            offset,
            next_usn) ||
        !read_u32(
            image,
            offset,
            reserved1) ||
        reserved1 != 0 ||
        !read_u32(
            image,
            offset,
            reserved2) ||
        reserved2 != 0 ||
        !read_u32(
            image,
            offset,
            reserved3) ||
        reserved3 != 0 ||
        offset != header_size) {

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
    std::size_t forward_size = 0;
    std::size_t reverse_size = 0;
    std::size_t file_index_size = 0;
    std::size_t directory_index_size = 0;

    if (!multiply_size(
            file_count_value,
            record_size,
            records_size) ||
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

    std::size_t expected =
        header_size;

    if (!add_size(
            expected,
            records_size) ||
        !add_size(
            expected,
            path_bytes_value) ||
        !add_size(
            expected,
            forward_size) ||
        !add_size(
            expected,
            reverse_size) ||
        !add_size(
            expected,
            file_index_size) ||
        !add_size(
            expected,
            directory_index_size) ||
        !add_size(
            expected,
            checksum_size) ||
        expected !=
            image.size()) {

        reset();
        return source_save_result::
            invalid_image;
    }

    records_offset =
        header_size;

    paths_offset =
        records_offset +
        records_size;

    forward_offset =
        paths_offset +
        path_bytes_value;

    reverse_offset =
        forward_offset +
        forward_size;

    file_index_offset =
        reverse_offset +
        reverse_size;

    directory_index_offset =
        file_index_offset +
        file_index_size;

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

source_save_result build_source_save_image(
    const file_context& files,
    const source_save_build_options& options,
    project_artifact_image& output) noexcept {

    output = {};

    if (!files.dependency_topology_finalized() ||
        files.size() == 0 ||
        files.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return source_save_result::
            invalid_state;
    }

    try {
        tracking_capture tracking;

        if (!build_tracking_capture(
                files,
                options.change_checkpoint,
                tracking)) {

            return source_save_result::failed;
        }

        const auto file_count =
            static_cast<std::uint32_t>(
                files.size());

        std::vector<std::string> paths;
        paths.reserve(
            files.size());

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

                return source_save_result::
                    invalid_state;
            }

            const auto path_view =
                files.path(
                    file);

            const std::filesystem::path path{
                path_view.begin(),
                path_view.end()};

            std::string utf8;

            if (filesystem_path_to_utf8(
                    path,
                    utf8) !=
                    filesystem_path_result::
                        success ||
                utf8.empty() ||
                utf8.size() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<
                            std::uint32_t>::max)()) -
                        path_bytes) {

                return source_save_result::
                    invalid_state;
            }

            path_bytes +=
                static_cast<std::uint32_t>(
                    utf8.size());

            paths.push_back(
                std::move(
                    utf8));

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

            return source_save_result::
                invalid_state;
        }

        const auto file_index_count =
            static_cast<std::uint32_t>(
                tracking.files.size());

        const auto directory_index_count =
            static_cast<std::uint32_t>(
                tracking.directories.size());

        std::size_t records_size = 0;
        std::size_t forward_size = 0;
        std::size_t reverse_size = 0;
        std::size_t file_index_size = 0;
        std::size_t directory_index_size = 0;

        if (!multiply_size(
                file_count,
                record_size,
                records_size) ||
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

            return source_save_result::failed;
        }

        std::size_t total_size =
            header_size;

        if (!add_size(
                total_size,
                records_size) ||
            !add_size(
                total_size,
                path_bytes) ||
            !add_size(
                total_size,
                forward_size) ||
            !add_size(
                total_size,
                reverse_size) ||
            !add_size(
                total_size,
                file_index_size) ||
            !add_size(
                total_size,
                directory_index_size) ||
            !add_size(
                total_size,
                checksum_size)) {

            return source_save_result::failed;
        }

        output.bytes.reserve(
            total_size);

        append_bytes(
            output.bytes,
            magic.data(),
            magic.size());

        append_u32(output.bytes, format_version);
        append_u32(output.bytes, file_count);
        append_u32(output.bytes, path_bytes);
        append_u32(output.bytes, forward_count);
        append_u32(output.bytes, reverse_count);

        append_u32(
            output.bytes,
            static_cast<std::uint32_t>(
                tracking.checkpoint.backend));

        append_u32(output.bytes, file_index_count);
        append_u32(output.bytes, directory_index_count);
        append_u32(output.bytes, 0);

        append_u64(
            output.bytes,
            tracking.checkpoint.volume_serial);

        append_u64(
            output.bytes,
            tracking.checkpoint.journal_id);

        append_u64(
            output.bytes,
            static_cast<std::uint64_t>(
                tracking.checkpoint.next_usn));

        append_u32(output.bytes, 0);
        append_u32(output.bytes, 0);
        append_u32(output.bytes, 0);

        std::uint32_t path_offset = 0;
        std::uint32_t dependency_offset = 0;
        std::uint32_t dependent_offset = 0;

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            const file_id file{value};

            const auto* physical =
                files.physical(file);

            if (physical == nullptr ||
                !physical->present()) {

                output = {};
                return source_save_result::
                    invalid_state;
            }

            const auto& path =
                paths[value - 1];

            const auto dependencies =
                files.dependencies(file);

            const auto dependents =
                files.dependents(file);

            append_u32(output.bytes, path_offset);

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    path.size()));

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    files.kind(file)));

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
                flags |= file_reference_flag;
            }

            append_u32(output.bytes, flags);

            append_bytes(
                output.bytes,
                physical->content_hash.
                    bytes.data(),
                physical->content_hash.
                    bytes.size());

            append_u64(
                output.bytes,
                file_reference);

            append_u32(
                output.bytes,
                dependency_offset);

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    dependencies.size()));

            append_u32(
                output.bytes,
                dependent_offset);

            append_u32(
                output.bytes,
                static_cast<std::uint32_t>(
                    dependents.size()));

            path_offset +=
                static_cast<std::uint32_t>(
                    path.size());

            dependency_offset +=
                static_cast<std::uint32_t>(
                    dependencies.size());

            dependent_offset +=
                static_cast<std::uint32_t>(
                    dependents.size());
        }

        for (const auto& path : paths) {
            append_bytes(
                output.bytes,
                reinterpret_cast<const std::byte*>(
                    path.data()),
                path.size());
        }

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            for (const auto target :
                 files.dependencies(
                     file_id{value})) {

                append_u32(
                    output.bytes,
                    target.value());
            }
        }

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            for (const auto source :
                 files.dependents(
                     file_id{value})) {

                append_u32(
                    output.bytes,
                    source.value());
            }
        }

        for (const auto& slot :
             tracking.files) {

            append_u64(
                output.bytes,
                slot.file_reference);

            append_u32(
                output.bytes,
                slot.file.value());
        }

        for (const auto& slot :
             tracking.directories) {

            append_u64(
                output.bytes,
                slot.file_reference);

            append_u32(
                output.bytes,
                slot.flags);
        }

        const auto digest =
            checksum(
                std::span<const std::byte>{
                    output.bytes});

        append_bytes(
            output.bytes,
            digest.bytes.data(),
            digest.bytes.size());

        if (output.bytes.size() !=
            total_size) {

            output = {};
            return source_save_result::failed;
        }

        finalize_project_artifact_image(output);

        return source_save_result::success;
    }
    catch (...) {
        output = {};
        return source_save_result::failed;
    }
}

source_save_result build_source_save_image(
    const file_context& files,
    project_artifact_image& output) noexcept {

    return build_source_save_image(
        files,
        source_save_build_options{},
        output);
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

    try {
        const auto file_count =
            static_cast<std::uint32_t>(
                view.file_count());

        std::vector<std::uint32_t>
            reverse_counts(file_count);

        std::vector<std::uint32_t>
            marker(file_count);

        for (std::uint32_t value = 1;
             value <= file_count;
             ++value) {

            const file_id file{value};
            source_save_file_view state;

            if (!view.file(
                    file,
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
                        target_state) ||
                    !target_state.
                        current_member) {

                    return source_save_result::
                        invalid_image;
                }

                auto& seen =
                    marker[
                        target.value() - 1];

                if (seen == value) {
                    return source_save_result::
                        invalid_image;
                }

                seen = value;

                auto& count =
                    reverse_counts[
                        target.value() - 1];

                if (count ==
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                    return source_save_result::
                        invalid_image;
                }

                ++count;
            }
        }

        std::vector<std::uint32_t>
            reverse_offsets(file_count);

        std::uint32_t total = 0;

        for (std::uint32_t index = 0;
             index < file_count;
             ++index) {

            reverse_offsets[index] =
                total;

            if (reverse_counts[index] >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    total) {

                return source_save_result::
                    invalid_image;
            }

            total +=
                reverse_counts[index];
        }

        std::vector<std::uint32_t>
            cursor = reverse_offsets;

        std::vector<std::uint32_t>
            expected_reverse(total);

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

                expected_reverse[
                    cursor[
                        target.value() - 1]++] =
                    source;
            }
        }

        for (std::uint32_t target = 1;
             target <= file_count;
             ++target) {

            source_save_file_view state;

            if (!view.file(
                    file_id{target},
                    state) ||
                state.dependents.size() !=
                    reverse_counts[
                        target - 1]) {

                return source_save_result::
                    invalid_image;
            }

            const auto begin =
                reverse_offsets[
                    target - 1];

            for (std::size_t index = 0;
                 index <
                    state.dependents.size();
                 ++index) {

                const auto persisted =
                    state.dependents[index];

                if (!persisted ||
                    persisted.value() !=
                        expected_reverse[
                            begin + index]) {

                    return source_save_result::
                        invalid_image;
                }
            }
        }

        if (view.change_checkpoint()) {
            std::vector<std::uint8_t>
                indexed(file_count);

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
                    indexed[
                        value.file.value() - 1] !=
                        0) {

                    return source_save_result::
                        invalid_image;
                }

                indexed[
                    value.file.value() - 1] =
                    1;
            }

            for (const auto value :
                 indexed) {

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
    const source_save_view& baseline,
    std::vector<file_id>& dirty,
    source_save_change_scan_metrics& metrics) noexcept {

    dirty.clear();

    const auto checkpoint =
        baseline.change_checkpoint();

    if (!checkpoint ||
        checkpoint.backend !=
            file_change_backend::windows_usn) {

        return journal_scan_result::fallback;
    }

    source_save_file_view first;

    if (!baseline.file(
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
                (baseline.file_count() + 63) /
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
                        (baseline.
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
                        baseline.
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
                    baseline.file_count()) {

                    dirty.clear();
                    return journal_scan_result::
                        fallback;
                }

                const file_id file{
                    static_cast<std::uint32_t>(
                        zero_based + 1)};

                source_save_file_view state;

                if (!baseline.file(
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
    const source_save_view& baseline,
    std::vector<file_id>& dirty,
    source_save_change_scan_metrics& metrics) noexcept {

    dirty.clear();
    metrics.fallback = true;

    try {
        dirty.reserve(
            baseline.file_count() /
                32 +
            1);

        for (std::size_t index = 0;
             index <
                baseline.file_count();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            source_save_file_view state;

            if (!baseline.file(
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
    const source_save_view& baseline,
    std::vector<file_id>& dirty,
    source_save_change_scan* scan) noexcept {

    dirty.clear();

    source_save_change_scan local;

    if (!baseline.valid()) {
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
        baseline.change_checkpoint();

    if (baseline_checkpoint) {
        source_save_file_view first;

        if (baseline.file(
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
            baseline,
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
    // it does not advance journal identity continuity for the resulting baseline.
    local.next_checkpoint = {};

    const auto fallback =
        scan_exact_fallback(
            baseline,
            dirty,
            local.metrics);

    if (scan != nullptr) {
        *scan =
            local;
    }

    return fallback;
}

server_status collect_source_save_affected(
    const source_save_view& baseline,
    std::span<const file_id> dirty,
    std::vector<file_id>& affected) noexcept {

    affected.clear();

    if (!baseline.valid()) {
        return server_status::
            project_artifact_invalid;
    }

    try {
        std::vector<std::uint8_t>
            visited(
                baseline.file_count());

        affected.reserve(
            dirty.size());

        for (const auto file :
             dirty) {

            if (!baseline.contains(file)) {
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

            if (!baseline.file(
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

            if (!baseline.file(
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

                if (!baseline.contains(dependent)) {
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

                if (!baseline.file(
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

}
