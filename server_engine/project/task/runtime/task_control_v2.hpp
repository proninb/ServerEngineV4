#pragma once
/* V2 private control transport. Not part of Project SHM or compiled.bin ABI. */
#include <Windows.h>
#include <cstdint>
namespace cw::server::task_runtime {
inline constexpr LONG control_v2_magic = 0x32534b54;
enum class control_v2_command : LONG { init=1, run=2, stop=3 };
struct alignas(64) control_v2 {
    LONG magic = control_v2_magic;
    LONG version = 2;
    LONG command = 0;
    LONG status = 0; // 0 success, nonzero failure
    LONG sequence = 0;
    LONG completed = 0;
    std::uint64_t cycle = 0;
    std::uint32_t calls = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(control_v2)==64);
}
