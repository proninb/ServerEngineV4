#pragma once
/* Manager-owned, bounded async trace. Flush/join before Task Manager unload. */
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace cw::server::task_runtime {
class task_trace_buffer final {
public:
    task_trace_buffer() {
        const char* path = std::getenv("TASK_SCHEDULER_TASK_CSV");
        if (!path || !*path) return;
        filename = path;
        try { writer = std::thread([this] { write_loop(); }); }
        catch (...) { filename.clear(); }
    }
    ~task_trace_buffer() { close(); }
    task_trace_buffer(const task_trace_buffer&) = delete;
    task_trace_buffer& operator=(const task_trace_buffer&) = delete;

    void record(const char* mode, std::uint64_t cycle, const std::string& id,
                const char* event, long long duration_us = 0) noexcept {
        if (filename.empty()) return;
        entry item{};
        std::snprintf(item.line.data(), item.line.size(), "%s,%llu,%s,%s,%lld\n",
            mode, static_cast<unsigned long long>(cycle), id.c_str(), event, duration_us);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) return;
            if (count == capacity) { ++dropped; return; }
            buffer[tail] = item;
            tail = (tail + 1) % capacity;
            ++count;
        }
        cv.notify_one();
    }

    void close() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        cv.notify_one();
        if (writer.joinable()) writer.join();
    }
private:
    struct entry { std::array<char, 256> line{}; };
    static constexpr std::size_t capacity = 65536;
    void write_loop() noexcept {
        FILE* file = nullptr;
        if (fopen_s(&file, filename.c_str(), "a+") != 0 || !file) return;
        std::fseek(file, 0, SEEK_END);
        if (std::ftell(file) == 0)
            std::fputs("mode,cycle,task,event,duration_us\n", file);
        for (;;) {
            entry item{};
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return stopping || count != 0; });
                if (!count && stopping) break;
                item = buffer[head];
                head = (head + 1) % capacity;
                --count;
            }
            std::fputs(item.line.data(), file);
        }
        std::fprintf(file, "#dropped,%llu\n",
                     static_cast<unsigned long long>(dropped));
        std::fflush(file);
        std::fclose(file);
    }
    std::string filename;
    std::mutex mutex;
    std::condition_variable cv;
    std::array<entry, capacity> buffer{};
    std::thread writer;
    std::size_t head = 0, tail = 0, count = 0;
    std::uint64_t dropped = 0;
    bool stopping = false;
};
}
