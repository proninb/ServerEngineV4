/* Independent named-SHM reader for timing E2E. Reads only under the mutex. */
#include "project/task/runtime/task_runtime_table.hpp"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace cw::server::task_runtime;

namespace {
std::string quote(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 32) {
                out += "\\u00";
                out += hex[ch >> 4];
                out += hex[ch & 15];
            } else out.push_back(static_cast<char>(ch));
        }
    }
    out += '"';
    return out;
}

template <std::size_t N>
std::string_view cstring(const char (&value)[N]) {
    std::size_t size = 0;
    while (size < N && value[size] != '\0') ++size;
    return {value, size};
}

template <typename T>
bool take(const std::vector<std::byte>& bytes, std::size_t offset, T& target) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    std::memcpy(&target, bytes.data() + offset, sizeof(T));
    return true;
}

struct handle {
    HANDLE value = nullptr;
    ~handle() { if (value) CloseHandle(value); }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    explicit handle(HANDLE input) : value(input) {}
    explicit operator bool() const { return value != nullptr; }
};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: task_timing_shm_probe.exe Local\\SEV4_System_...\n";
        return 2;
    }
    const std::wstring name = argv[1];
    handle mutex(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L"_mutex").c_str()));
    handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
    if (!mutex || !mapping) { std::cerr << "Cannot open SHM or mutex: " << GetLastError() << "\n"; return 3; }
    const DWORD locked = WaitForSingleObject(mutex.value, 2000);
    if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) {
        std::cerr << "System table mutex timeout\n";
        return 4;
    }
    const auto* view = static_cast<const std::byte*>(MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0));
    std::vector<std::byte> bytes;
    int error = 0;
    if (!view) {
        error = 5;
    } else {
        MEMORY_BASIC_INFORMATION info{};
        runtime_table table{};
        if (VirtualQuery(view, &info, sizeof(info)) == 0 ||
            info.RegionSize < sizeof(runtime_table)) error = 6;
        else {
            std::memcpy(&table, view, sizeof(table));
            if (table.magic != runtime_table_magic ||
                table.version != runtime_table_version ||
                table.total_size < sizeof(runtime_table) ||
                table.total_size > info.RegionSize ||
                table.total_size > 64ull * 1024 * 1024 ||
                table.task_count > 65535 ||
                table.tasks_offset % alignof(task_record) != 0 ||
                table.tasks_offset < sizeof(runtime_table) ||
                table.tasks_offset > table.total_size ||
                static_cast<std::uint64_t>(table.task_count) * sizeof(task_record) >
                    table.total_size - table.tasks_offset ||
                table.dependencies_offset < table.tasks_offset +
                    static_cast<std::uint64_t>(table.task_count) * sizeof(task_record) ||
                table.dependencies_offset > table.total_size ||
                static_cast<std::uint64_t>(table.dependency_count) * sizeof(std::uint32_t) >
                    table.total_size - table.dependencies_offset) error = 7;
            else {
                bytes.resize(static_cast<std::size_t>(table.total_size));
                std::memcpy(bytes.data(), view, bytes.size());
            }
        }
        UnmapViewOfFile(view);
    }
    ReleaseMutex(mutex.value);
    if (error) { std::cerr << "Invalid system table or mapping (" << error << ")\n"; return error; }
    runtime_table h{};
    if (!take(bytes, 0, h) || cstring(h.name) != "system") return 8;
    const auto& s = h.system;
    std::cout << "{\"name\":\"system\",\"version\":" << h.version
              << ",\"generation\":" << h.generation << ",\"task_count\":" << h.task_count
              << ",\"dependency_count\":" << h.dependency_count << ",\"system\":{"
              << "\"mode\":" << s.mode << ",\"period_us\":" << s.period_us
              << ",\"deadline_enabled\":" << s.deadline_enabled
              << ",\"server_time_unix_ns\":" << s.server_time_unix_ns
              << ",\"simulation_time_ns\":" << s.simulation_time_ns
              << ",\"elapsed_real_ns\":" << s.elapsed_real_ns
              << ",\"speed_ppm\":" << s.speed_ppm
              << ",\"completed_cycles\":" << s.completed_cycles
              << ",\"last_cycle_ns\":" << s.last_cycle_ns
              << ",\"max_cycle_ns\":" << s.max_cycle_ns
              << ",\"deadline_delta_ns\":" << s.deadline_delta_ns
              << ",\"time_drift_ns\":" << s.time_drift_ns
              << ",\"wakeup_jitter_ns\":" << s.wakeup_jitter_ns
              << ",\"deadline_misses\":" << s.deadline_misses
              << ",\"warnings\":" << s.warnings
              << ",\"stop_reason\":" << s.stop_reason
              << ",\"stop_pending\":" << s.stop_pending
              << ",\"project_state\":" << s.project_state
              << ",\"last_error_cycle\":" << s.last_error_cycle
              << "},\"tasks\":[";
    for (std::uint32_t i = 0; i < h.task_count; ++i) {
        task_record record{};
        if (!take(bytes, static_cast<std::size_t>(h.tasks_offset) +
                    static_cast<std::size_t>(i) * sizeof(task_record), record)) return 9;
        const auto& p = record.parameters;
        const auto& d = record.diagnostics;
        if (static_cast<std::uint64_t>(p.dependency_offset) + p.dependency_count > h.dependency_count) return 10;
        if (i) std::cout << ',';
        std::cout << "{\"id\":" << quote(cstring(p.id))
                  << ",\"executable\":" << quote(cstring(p.executable))
                  << ",\"enabled\":" << p.enabled
                  << ",\"run_in_freeze\":" << p.run_in_freeze
                  << ",\"calls\":" << p.calls
                  << ",\"cycles\":" << p.cycles
                  << ",\"starting_from_cycle\":" << p.starting_from_cycle
                  << ",\"dependencies\":[";
        for (std::uint32_t j = 0; j < p.dependency_count; ++j) {
            std::uint32_t index = 0;
            const std::size_t off = static_cast<std::size_t>(h.dependencies_offset) +
                static_cast<std::size_t>(p.dependency_offset + j) * sizeof(std::uint32_t);
            if (!take(bytes, off, index) || index >= h.task_count) return 11;
            if (j) std::cout << ',';
            std::cout << index;
        }
        std::cout << "],\"diagnostics\":{\"state\":" << d.state
                  << ",\"last_cycle\":" << d.last_cycle
                  << ",\"scheduled\":" << d.scheduled
                  << ",\"completed\":" << d.completed
                  << ",\"failed\":" << d.failed
                  << ",\"total_calls\":" << d.total_calls
                  << ",\"last_duration_ns\":" << d.last_duration_ns
                  << ",\"min_duration_ns\":" << d.min_duration_ns
                  << ",\"max_duration_ns\":" << d.max_duration_ns
                  << ",\"total_duration_ns\":" << d.total_duration_ns
                  << ",\"deadline_delta_ns\":" << d.deadline_delta_ns
                  << ",\"deadline_misses\":" << d.deadline_misses
                  << ",\"warnings\":" << d.warnings
                  << ",\"last_error_code\":" << d.last_error_code
                  << "}}";
    }
    std::cout << "]}\n";
    return 0;
}
