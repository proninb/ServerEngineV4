#pragma once

#include "heap_optimization_policy.hpp"
#include "diagnostics/diagnostic_builder.hpp"
#include "diagnostics/diagnostic_collection.hpp"
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>
#endif

namespace cw::server {
namespace detail {
struct unload_heap_backend {
#ifdef _WIN32
    using read_fn = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    using optimize_fn = BOOL (WINAPI*)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);
    // Resolve optional entry points without loading a DLL or retaining a handle.
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    read_fn read_memory = kernel ? reinterpret_cast<read_fn>(
        GetProcAddress(kernel, "K32GetProcessMemoryInfo")) : nullptr;
    optimize_fn set_information = kernel ? reinterpret_cast<optimize_fn>(
        GetProcAddress(kernel, "HeapSetInformation")) : nullptr;
    bool available() const noexcept { return read_memory && set_information; }
    bool read(std::uint64_t& bytes, std::uint32_t& error) const noexcept {
        PROCESS_MEMORY_COUNTERS_EX info{};
        info.cb = sizeof(info);
        if (!read_memory(GetCurrentProcess(),
                reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&info), sizeof(info))) {
            error = GetLastError();
            return false;
        }
        bytes = info.PrivateUsage;
        return true;
    }
    bool optimize(std::uint32_t& error) const noexcept {
        HEAP_OPTIMIZE_RESOURCES_INFORMATION info{};
        info.Version = HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION;
        // Older Windows may export HeapSetInformation but reject this class.
        if (!set_information(nullptr, HeapOptimizeResources, &info, sizeof(info))) {
            error = GetLastError();
            return false;
        }
        return true;
    }
#else
    bool available() const noexcept { return false; }
    bool read(std::uint64_t&, std::uint32_t&) const noexcept { return false; }
    bool optimize(std::uint32_t&) const noexcept { return false; }
#endif
};
}

// Best effort after Project destruction. Neither the API nor diagnostics may
// turn an already completed UNLOAD into failure; no retry/trim/restart fallback.
inline void report_unload_heap_result(
    const heap_optimization_result& result,
    operation_id operation, diagnostic_collection& diagnostics) noexcept {
    const char* reason = nullptr;
    switch (result.state) {
    case heap_optimization_state::disabled:
    case heap_optimization_state::below_threshold:
    case heap_optimization_state::optimized: return;
    case heap_optimization_state::unavailable: reason = "API unavailable; optimization skipped"; break;
    case heap_optimization_state::read_failed: reason = "PrivateUsage read failed; optimization skipped"; break;
    case heap_optimization_state::optimize_failed: reason = "HeapOptimizeResources failed or is unsupported"; break;
    case heap_optimization_state::post_read_failed: reason = "Post-optimization PrivateUsage read failed"; break;
    case heap_optimization_state::still_above_threshold: reason = "PrivateUsage remains above candidate threshold"; break;
    }
    char message[320]{};
    std::snprintf(message, sizeof(message), "%s; win32_error=%lu; before=%llu; after=%llu; threshold=%llu",
        reason, static_cast<unsigned long>(result.error),
        static_cast<unsigned long long>(result.before), static_cast<unsigned long long>(result.after),
        static_cast<unsigned long long>(unload_heap_threshold));
    try {
        diagnostics.emit(diagnostic(diagnostics::server_heap_optimization_warning, operation)
            .detail(message).build());
    } catch (...) {
        // Last-resort sink when allocating the normal diagnostic itself fails.
        std::fprintf(stderr, "server.heap_optimization_warning: %s\n", message);
    }
}

inline void optimize_heap_after_unload(
    operation_id operation, diagnostic_collection& diagnostics) noexcept {
    detail::unload_heap_backend backend;
    report_unload_heap_result(optimize_unloaded_heap(true, backend), operation, diagnostics);
}
}
