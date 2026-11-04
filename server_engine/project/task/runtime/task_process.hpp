#pragma once
/* Parent-owned child process with strict request/reply sequencing. Windows only. */
#include "task_protocol.hpp"
#include <Windows.h>
#include <cstdint>
#include <string>
namespace cw::server::task_runtime {
class process final {
public:
    process() = default;
    ~process();
    process(const process&) = delete;
    process& operator=(const process&) = delete;
    bool start(const std::wstring& executable, std::uint32_t timeout_ms, std::string& error);
    bool run(std::uint64_t cycle, std::uint32_t calls, std::string& error);
    bool stop(std::string& error);
    void terminate() noexcept;
private:
    bool exchange(message request, command expected, std::string& error);
    HANDLE child = nullptr;
    HANDLE thread = nullptr;
    HANDLE input = nullptr;
    HANDLE output = nullptr;
    std::uint32_t timeout = 5000;
    bool initialized = false;
};
}
