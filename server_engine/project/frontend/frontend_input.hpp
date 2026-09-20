/*
 * Streaming Frontend physical-input stack.
 *
 * frontend_input owns only active file traversal positions. File identity and
 * bytes remain owned by File Context; include resolution remains a construction
 * responsibility outside this class.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cw::server {

// Hard fail-closed limit for active physical include nesting. The limit is
// independent of Project file count and keeps frontend traversal allocation-free.
inline constexpr std::size_t frontend_include_depth_limit = 256;

// Active physical-input stack for one streaming frontend execution. Frames keep
// only file_id + byte offset so File Context arena growth cannot invalidate the
// saved parent position while an included file is resolved and materialized.
class frontend_input final {
public:
    explicit frontend_input(
        const file_context& files) noexcept
        : files(files) {
    }

    frontend_input(const frontend_input&) = delete;
    frontend_input& operator=(const frontend_input&) = delete;

    [[nodiscard]] server_status start(
        file_id root) noexcept;

    [[nodiscard]] server_status enter(
        file_id file) noexcept;

    [[nodiscard]] server_status advance(
        std::size_t count) noexcept;

    [[nodiscard]] server_status leave() noexcept;

    [[nodiscard]] file_id current_file() const noexcept;

    [[nodiscard]] std::size_t offset() const noexcept;

    // The view is intentionally short-lived. Do not retain it across any
    // File Context mutation; call remaining() again after include resolution.
    [[nodiscard]] std::string_view remaining() const noexcept;

    [[nodiscard]] bool finished() const noexcept;

    [[nodiscard]] bool empty() const noexcept {
        return stack_size == 0;
    }

    [[nodiscard]] std::size_t depth() const noexcept {
        return stack_size;
    }

private:
    struct frame final {
        file_id file{};
        std::uint32_t offset = 0;
    };

    static_assert(sizeof(frame) == 8);

    const file_context& files;
    std::array<frame, frontend_include_depth_limit> stack{};
    std::size_t stack_size = 0;
};

}
