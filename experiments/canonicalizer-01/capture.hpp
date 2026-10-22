// Compiled only into the optional trace-enabled PUBLISH benchmark.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace canonicalizer01 {
struct trace_record {
    std::uint8_t type = 0, kind = 0;
    std::uint16_t reserved = 0;
    std::uint32_t unit = 0;
    std::uint64_t event = 0;
    std::uint32_t parent = 0, name = 0, result = 0, length = 0;
};
static_assert(sizeof(trace_record) == 32);
struct capture_state {
    std::mutex mutex;
    std::FILE* file = nullptr;
    const void* strings = nullptr;
    const void* identities = nullptr;
    std::uint64_t event = 0;
    std::uint32_t max_string = 0, max_identity = 1;
    bool failed = false;
};
inline capture_state capture;
inline thread_local std::uint32_t current_unit = 0;
inline void emit(trace_record r, std::string_view text = {}) noexcept {
    r.event = ++capture.event;
    r.unit = current_unit;
    r.length = static_cast<std::uint32_t>(text.size());
    if (std::fwrite(&r, sizeof(r), 1, capture.file) != 1 ||
        (!text.empty() && std::fwrite(text.data(), 1, text.size(), capture.file) != text.size()))
        capture.failed = true;
}
class capture_session {
public:
    capture_session(const void* strings, const void* identities) {
        const auto* path = std::getenv("CW_CANONICALIZER_TRACE");
        if (!path || !*path) return;
        std::lock_guard lock{capture.mutex};
        if (capture.file) throw std::runtime_error("canonicalizer capture already active");
        capture.file = std::fopen(path, "wb");
        if (!capture.file) throw std::runtime_error("cannot create canonicalizer capture");
        capture.strings = strings; capture.identities = identities;
        capture.event = 0; capture.max_string = 0; capture.max_identity = 1;
        capture.failed = std::fwrite("CWCA0101", 1, 8, capture.file) != 8;
        active = true;
    }
    ~capture_session() noexcept {
        if (!active) return;
        std::lock_guard lock{capture.mutex};
        // No valid footer on failure: the independent reader rejects the trace.
        if (!capture.failed) {
            trace_record footer{}; footer.type = 255;
            footer.parent = capture.max_string; footer.name = capture.max_identity;
            emit(footer);
        }
        if (std::fclose(capture.file) != 0)
            std::fputs("canonicalizer capture close failed\n", stderr);
        capture.file = nullptr; capture.strings = nullptr; capture.identities = nullptr;
    }
private:
    bool active = false;
};
struct capture_unit {
    std::uint32_t previous;
    explicit capture_unit(std::uint32_t unit) noexcept : previous(current_unit) { current_unit = unit; }
    ~capture_unit() noexcept { current_unit = previous; }
};
template<class F> struct exit_capture {
    F action;
    ~exit_capture() noexcept { action(); }
};
template<class F> exit_capture(F) -> exit_capture<F>;
inline void string_event(const void* table, std::string_view text, std::uint32_t id) noexcept {
    std::lock_guard lock{capture.mutex};
    if (!capture.file || table != capture.strings || !id) return;
    capture.max_string = (std::max)(capture.max_string, id);
    trace_record r{}; r.type = 1; r.result = id;
    emit(r, text);
}
inline void identity_event(const void* table, bool lookup, std::uint32_t parent,
    std::uint32_t name, std::uint8_t kind, std::uint32_t result) noexcept {
    std::lock_guard lock{capture.mutex};
    if (!capture.file || table != capture.identities) return;
    if (!lookup && !result) return;
    if (!parent || !name) return; // Invalid-argument lookups are not symbol events.
    if (!lookup) capture.max_identity = (std::max)(capture.max_identity, result & 0x3fffffffu);
    trace_record r{}; r.type = lookup ? 3 : 2; r.parent = parent;
    r.name = name; r.kind = kind; r.result = result;
    emit(r);
}
} // namespace canonicalizer01
