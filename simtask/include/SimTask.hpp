#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

/* User-defined entry points. SimTask.lib owns main() and the control protocol. */
inline constexpr int TASK_OK = 0;
inline constexpr int TASK_FAIL = 1;

namespace SimTask {
    void* SharedMemory() noexcept;
    std::size_t SharedMemorySize() noexcept;

    // A bit has a scalar address and an index. Its lifetime is the SHM attachment.
    struct Variable final {
        void* address = nullptr;
        std::uint8_t size = 0;
        std::uint8_t bit = 0xff;

        explicit operator bool() const noexcept { return address != nullptr; }
        bool is_bit() const noexcept {
            return address && bit != 0xff && bit / 8 < size;
        }
        bool read_bit(bool& value) const noexcept {
            if (!is_bit()) return false;
            auto& byte = static_cast<std::uint8_t*>(address)[bit / 8];
            value = (std::atomic_ref{byte}.load(std::memory_order_relaxed) & (1u << (bit % 8))) != 0;
            return true;
        }
        bool write_bit(bool value) const noexcept {
            if (!is_bit()) return false;
            auto& byte = static_cast<std::uint8_t*>(address)[bit / 8];
            const auto mask = static_cast<std::uint8_t>(1u << (bit % 8));
            std::atomic_ref cell{byte};
            if (value) cell.fetch_or(mask, std::memory_order_relaxed);
            else cell.fetch_and(static_cast<std::uint8_t>(~mask), std::memory_order_relaxed);
            return true;
        }
    };

    Variable getvar(std::string_view name) noexcept;
}

void TASKINIT();
int TASKMAIN();
void TASKDONE();
