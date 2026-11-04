#pragma once
/* TASK-RUNTIME-SHM-06: one outstanding request per Task process. */
#include "task_protocol.hpp"
#include <Windows.h>
#include <cstdint>
namespace cw::server::task_runtime {
inline constexpr std::uint32_t control_magic = 0x36534b54; // TKS6
inline constexpr std::uint32_t control_version = 1;
struct alignas(64) task_control {
    std::uint32_t signature = control_magic;
    std::uint32_t version = control_version;
    volatile LONG request_sequence = 0;
    volatile LONG response_sequence = 0;
    message request{};
    message response{};
    shm_attachment attachment{};
};
static_assert(sizeof(task_control) <= 4096);
}
