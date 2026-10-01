/*
 * Compiled Project Assign user-data table.
 *
 * Assign records are Studio-facing user data. They are not C++ semantic
 * identities, Graph links, Runtime connections, or File Context dependencies.
 * Records preserve declaration order and store text in one compact arena.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

class compiled_project_view;

struct assign_record final {
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
    std::uint32_t target_offset = 0;
    std::uint32_t target_length = 0;
};

static_assert(sizeof(assign_record) == 16);

// Ordered compact storage for normalized Assign source/target text pairs.
class assign_table final {
public:
    assign_table() = default;

    assign_table(const assign_table&) = delete;
    assign_table& operator=(const assign_table&) = delete;

    [[nodiscard]] server_status add(
        file_id file,
        std::string_view source,
        std::string_view target) noexcept;

    [[nodiscard]] server_status add(
        std::string_view source,
        std::string_view target) noexcept {
        return add(
            {},
            source,
            target);
    }

    void clear() noexcept;

    [[nodiscard]] std::span<const assign_record> records() const noexcept {
        return entries;
    }

    [[nodiscard]] std::string_view source(
        const assign_record& record) const noexcept;

    [[nodiscard]] std::string_view target(
        const assign_record& record) const noexcept;

    [[nodiscard]] file_id file(
        std::size_t index) const noexcept {
        return index < files.size()
            ? files[index]
            : file_id{};
    }

    [[nodiscard]] std::span<const file_id>
    file_entries() const noexcept {
        return files;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return entries.empty();
    }

    [[nodiscard]] std::size_t byte_size() const noexcept {
        return bytes.size();
    }

private:
    [[nodiscard]] std::string_view text(
        std::uint32_t offset,
        std::uint32_t length) const noexcept;

    std::vector<assign_record> entries;
    std::vector<file_id> files;
    std::vector<char> bytes;

};

// BUILD-only ordered Assign candidate. OLD records remain mmap-backed;
// selected file_id groups are replaced by a sparse assign_table.
class assign_overlay_view final {
public:
    using visitor_function =
        server_status (*)(
            void* context,
            file_id file,
            std::string_view source,
            std::string_view target) noexcept;

    [[nodiscard]] server_status bind(
        const compiled_project_view& baseline,
        const assign_table& replacements,
        std::span<const file_id> replaced_files) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return baseline != nullptr &&
            replacements != nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return record_count;
    }

    [[nodiscard]] std::size_t byte_size() const noexcept {
        return bytes_count;
    }

    [[nodiscard]] std::size_t replaced_file_count() const noexcept {
        return replaced.size();
    }

    [[nodiscard]] server_status visit(
        void* context,
        visitor_function visitor) const noexcept;

private:
    [[nodiscard]] bool replaced_file(
        file_id file) const noexcept;

    const compiled_project_view* baseline = nullptr;
    const assign_table* replacements = nullptr;
    std::vector<file_id> replaced;
    std::size_t record_count = 0;
    std::size_t bytes_count = 0;
};

}
