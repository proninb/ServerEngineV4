#include "unload_heap_optimization.hpp"
#include <cstdlib>

using namespace cw::server;
namespace {
void check(bool value) { if (!value) std::abort(); }
struct backend {
    bool supported = true, fail_read = false, fail_optimize = false, fail_post = false;
    int reads = 0, calls = 0, availability_checks = 0;
    std::uint64_t before = unload_heap_threshold, after = 4096;
    bool available() noexcept { ++availability_checks; return supported; }
    bool read(std::uint64_t& value, std::uint32_t& error) noexcept {
        ++reads;
        if (fail_read || (reads == 2 && fail_post)) { error = 5; return false; }
        value = reads == 1 ? before : after;
        return true;
    }
    bool optimize(std::uint32_t& error) noexcept {
        ++calls;
        if (fail_optimize) { error = 87; return false; }
        return true;
    }
};
void warning(const heap_optimization_result& result) {
    diagnostic_collection diagnostics;
    report_unload_heap_result(result, operation_id{42}, diagnostics);
    check(!diagnostics.has_errors());
    check(diagnostics.records().size() == 1);
    check(diagnostics.records()[0].severity == diagnostic_severity::warning);
    check(diagnostics.records()[0].operation == operation_id{42});
}
}
int main() {
    backend b;
    check(optimize_unloaded_heap(false, b).state == heap_optimization_state::disabled);
    check(b.reads == 0 && b.calls == 0 && b.availability_checks == 0);
    b.supported = false;
    auto result = optimize_unloaded_heap(true, b);
    check(result.state == heap_optimization_state::unavailable && b.reads == 0);
    warning(result);
    b = {}; b.fail_read = true;
    result = optimize_unloaded_heap(true, b);
    check(result.state == heap_optimization_state::read_failed && result.error == 5 && b.calls == 0);
    warning(result);
    b = {}; b.before = unload_heap_threshold - 1;
    check(optimize_unloaded_heap(true, b).state == heap_optimization_state::below_threshold && b.calls == 0);
    b = {};
    check(optimize_unloaded_heap(true, b).state == heap_optimization_state::optimized && b.calls == 1 && b.reads == 2);
    b = {}; b.fail_optimize = true;
    result = optimize_unloaded_heap(true, b);
    check(result.state == heap_optimization_state::optimize_failed && result.error == 87 && b.reads == 1);
    warning(result);
    b = {}; b.fail_post = true;
    result = optimize_unloaded_heap(true, b);
    check(result.state == heap_optimization_state::post_read_failed && b.calls == 1);
    warning(result);
    b = {}; b.after = unload_heap_threshold;
    result = optimize_unloaded_heap(true, b);
    check(result.state == heap_optimization_state::still_above_threshold && b.calls == 1);
    warning(result);
    diagnostic_collection diagnostics;
    report_unload_heap_result({heap_optimization_state::optimized}, operation_id{1}, diagnostics);
    check(diagnostics.empty());
    // Real platform binding smoke test; no allocation pressure or forced trim.
    detail::unload_heap_backend real;
#ifdef _WIN32
    check(real.available());
    std::uint64_t bytes = 0; std::uint32_t error = 0;
    check(real.read(bytes, error) && bytes > 0);
#else
    check(!real.available());
#endif
}
