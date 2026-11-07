#pragma once
#include "task_trace.hpp"
/* Parent-owned Task process. SHM control + named Windows events, no pipes. */
#include "task_protocol.hpp"
#include <Windows.h>
#include <cstdint>
#include <string>
namespace cw::server::task_runtime {
struct task_control;
struct task_ipc_ownership {
    std::uint64_t handles;
    std::uint64_t views;
};
[[nodiscard]] task_ipc_ownership task_ipc_snapshot() noexcept;
class process final {
public:
    process()=default;
    ~process();
    process(const process&)=delete;
    process& operator=(const process&)=delete;
    bool start(const std::wstring& executable,std::uint32_t timeout_ms,std::string& error);
    bool start(const std::wstring& executable,std::uint32_t timeout_ms,
               const shm_attachment& attachment,std::string& error);
    bool run(std::uint64_t cycle,std::uint32_t calls,std::string& error);
    // LOCKSTEP: one outstanding request per child, scheduler owns waiting.
    bool begin_run(std::uint64_t cycle, std::uint32_t calls, std::string& error);
    bool complete_run(std::string& error);
    [[nodiscard]] HANDLE completion_handle() const noexcept { return completion_event; }
    [[nodiscard]] HANDLE process_handle() const noexcept { return child; }
    [[nodiscard]] std::uint32_t timeout_ms() const noexcept { return timeout; }
    bool stop(std::string& error);
    void terminate() noexcept;
private:
    perf07::trace perf_trace;
    bool exchange(message request,command expected,std::string& error);
    bool start_impl(const std::wstring& executable,std::uint32_t timeout_ms,
                    const shm_attachment* attachment,std::string& error);
    HANDLE child=nullptr,thread=nullptr,mapping=nullptr;
    HANDLE command_event=nullptr,completion_event=nullptr;
    task_control* control=nullptr;
    std::uint32_t timeout=5000;
    LONG sequence=0;
    bool initialized=false;
    bool in_flight=false;
    LONG in_flight_sequence=0;
    std::uint64_t in_flight_cycle=0;
    std::int64_t in_flight_start=0;
    std::int64_t dispatch_before=0;
    std::int64_t dispatch_after=0;
    FILE* dispatch_file=nullptr;
};
}
