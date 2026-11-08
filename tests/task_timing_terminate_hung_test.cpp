/* Verify process-level emergency termination while TASKMAIN never returns.
 * This is NOT a Server STOP command or a proof that Server LOCKSTEP can abort.
 */
#include "task_process.hpp"
#include <Windows.h>
#include <iostream>
#include <string>

using cw::server::task_runtime::process;

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: task_timing_terminate_hung_test.exe <fixture>\n";
        return 2;
    }
    process child;
    std::string error;
    if (!child.start(argv[1], 5000, error)) {
        std::cerr << "TASKINIT failed: " << error << '\n';
        return 3;
    }
    // Kill-on-close keeps the child from surviving if CTest kills the parent.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                        &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, child.process_handle())) {
        std::cerr << "Cannot put fixture in protective Windows Job Object: "
                  << GetLastError() << '\n';
        child.terminate();
        if (job) CloseHandle(job);
        return 8;
    }
    const DWORD pid = GetProcessId(child.process_handle());
    HANDLE observer = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!observer) {
        std::cerr << "Cannot open child process for exit observation\n";
        child.terminate();
        CloseHandle(job);
        return 4;
    }
    int result = 0;
    if (!child.begin_run(1, 1, error)) {
        std::cerr << "TASKMAIN dispatch failed: " << error << '\n';
        result = 5;
    } else {
        const DWORD reply = WaitForSingleObject(child.completion_handle(), 150);
        if (reply != WAIT_TIMEOUT) {
            std::cerr << "Expected a hung TASKMAIN without a completion reply, wait=" << reply << '\n';
            result = 6;
        }
    }
    const auto before = GetTickCount64();
    child.terminate();
    const auto termination_ms = GetTickCount64() - before;
    const DWORD exited = WaitForSingleObject(observer, 1000);
    CloseHandle(observer);
    CloseHandle(job);
    if (exited != WAIT_OBJECT_0) {
        std::cerr << "Child PID " << pid << " survived process::terminate()\n";
        return 7;
    }
    if (!result) {
        std::cout << "TASK PROCESS FORCE TERMINATE PASS: child=" << pid
                  << " termination_ms=" << termination_ms << '\n';
    }
    return result;
}
