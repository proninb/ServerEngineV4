#pragma once
/* TASK-RUNTIME-01A: versioned control protocol; no SHM ABI changes. */
#include <cstdint>
namespace cw::server::task_runtime {
inline constexpr std::uint32_t magic = 0x31534b54; // TKS1
inline constexpr std::uint32_t version = 1;
enum class command : std::uint32_t { init=1, run=2, stop=3, ready=4, done=5, stopped=6, error=7, attach=8, attached=9 };
struct message {
    std::uint32_t signature = magic;
    std::uint32_t protocol_version = version;
    command action = command::error;
    std::uint32_t reserved = 0;
    std::uint64_t cycle = 0;
    std::uint32_t calls = 0;
    std::uint32_t status = 0;
};
static_assert(sizeof(message)==32);
// Optional pre-INIT attachment handshake. Existing INIT-only clients remain valid.
// No change to message ABI or compiled.bin / Runtime SHM layout.
struct shm_attachment {
    std::uint64_t size = 0;
    std::uint64_t address = 0;
    char name[129]{};
};
static_assert(sizeof(shm_attachment) == 152);

}
