#pragma once
/* Persistent Task worker pool; no per-cycle thread creation. */
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
namespace cw::server::task_runtime {
class task_executor final {
public:
    explicit task_executor(std::size_t count) {
        try {
            for (std::size_t i = 0; i < count; ++i)
                threads.emplace_back([this] { loop(); });
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~task_executor() { shutdown(); }
    task_executor(const task_executor&) = delete;
    task_executor& operator=(const task_executor&) = delete;
    template<class F>
    auto submit(F&& fn) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) throw std::runtime_error("Task executor stopped");
            queue.emplace_back([task] { (*task)(); });
        }
        wake.notify_one();
        return result;
    }
private:
    void loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping && queue.empty()) return;
                job = std::move(queue.front());
                queue.pop_front();
            }
            job();
        }
    }
    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (auto& t : threads) if (t.joinable()) t.join();
    }
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    bool stopping = false;
};
}
