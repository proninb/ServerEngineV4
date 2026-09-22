/*
 * Project-construction canonical string table.
 *
 * string_table has two storage modes: fresh dense state for REBUILD, or an
 * immutable compiled.bin baseline plus append-only local state for BUILD.
 * Persisted spellings stay mmap-backed and are never copied into the overlay.
 */
#pragma once

#include "../../server_status.hpp"
#include "../../string_id.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cw::server {

class compiled_project_view;
class identity_space;

class string_table final {
public:
    string_table() = default;

    string_table(const string_table&) = delete;
    string_table& operator=(const string_table&) = delete;

    [[nodiscard]] server_status bind_baseline(
        const compiled_project_view& baseline) noexcept;

    [[nodiscard]] server_status intern(
        std::string_view value,
        string_id& output) noexcept;

    [[nodiscard]] string_id find(
        std::string_view value) const noexcept;

    [[nodiscard]] std::string_view get(
        string_id id) const noexcept;

    [[nodiscard]] std::string_view spelling(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] bool contains(
        string_id id) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return baseline_string_count +
            records.size();
    }

    [[nodiscard]] std::size_t byte_size() const noexcept {
        return baseline_byte_count +
            bytes.size();
    }

private:
    struct string_record final {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        std::uint32_t hash = 0;
    };

    struct string_slot final {
        std::uint32_t hash = 0;
        string_id id{};
    };

    static_assert(sizeof(string_record) == 12);
    static_assert(sizeof(string_slot) == 8);

    [[nodiscard]] static std::uint32_t hash_text(
        std::string_view value) noexcept;

    [[nodiscard]] server_status ensure_index_capacity(
        std::size_t additional) noexcept;

    void insert_index(
        std::vector<string_slot>& index,
        string_id id,
        std::uint32_t hash) const noexcept;

    const compiled_project_view* baseline = nullptr;
    std::size_t baseline_string_count = 0;
    std::size_t baseline_byte_count = 0;

    std::vector<string_record> records;
    std::vector<char> bytes;
    std::vector<string_slot> index;

    friend class identity_space;
};

}
