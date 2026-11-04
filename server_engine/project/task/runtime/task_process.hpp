#pragma once
#include "task_trace.hpp"
/* Parent-owned Task process. SHM control + named Windows events, no pipes. */
#include "task_protocol.hpp"
#include <Windows.h>
#include <cstdint>
#include <string>
namespace cw::server::task_runtime {
struct task_control;
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
};
}
