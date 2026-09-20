#include "lexical_generation.hpp"

#include "lexer.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

struct lexical_file_work final {
    file_id file{};
    file_acquire_job job;
    file_acquire_result result;
    std::string_view source;
    lexical_stream lexical;
    lexical_error error;
    server_status lexical_status = server_status::success;
};

[[nodiscard]] server_status acquisition_status(
    file_acquire_result_kind kind) noexcept {

    switch (kind) {
    case file_acquire_result_kind::present:
        return server_status::success;

    case file_acquire_result_kind::missing:
        return server_status::project_configuration_invalid;

    case file_acquire_result_kind::unchanged:
        return server_status::project_artifact_invalid;

    case file_acquire_result_kind::changed_during_read:
    case file_acquire_result_kind::failed:
    case file_acquire_result_kind::allocation_failed:
        return server_status::io_error;
    }

    return server_status::io_error;
}

void execute_lexical_work(
    lexical_file_work& item) noexcept {

    auto source =
        item.source;

    if (item.job.file) {
        file_context::execute_acquire(
            item.job,
            item.result);

        if (item.result.kind !=
            file_acquire_result_kind::present) {

            return;
        }

        source =
            item.result.snapshot.bytes;
    }

    item.lexical_status =
        lexer::tokenize(
            item.file,
            source,
            item.lexical,
            &item.error);
}

// Persistent construction executor for one lexical-generation build. Threads are
// created once, while each bounded wave still completes before owner publication.
class lexical_wave_executor final {
public:
    explicit lexical_wave_executor(
        std::size_t worker_count) {

        if (worker_count <= 1) {
            return;
        }

        workers.reserve(
            worker_count - 1);

        for (std::size_t index = 1;
             index < worker_count;
             ++index) {

            workers.emplace_back(
                [this](std::stop_token stop) noexcept {
                    worker_loop(stop);
                });
        }
    }

    lexical_wave_executor(
        const lexical_wave_executor&) = delete;

    lexical_wave_executor& operator=(
        const lexical_wave_executor&) = delete;

    ~lexical_wave_executor() {
        {
            std::lock_guard lock{gate};
            stopping = true;
        }

        ready.notify_all();
    }

    [[nodiscard]] server_status execute(
        std::span<lexical_file_work> wave) noexcept {

        if (wave.empty()) {
            return server_status::success;
        }

        if (workers.empty()) {
            for (auto& item : wave) {
                execute_lexical_work(
                    item);
            }

            return server_status::success;
        }

        {
            std::lock_guard lock{gate};

            current = wave;
            next.store(
                0,
                std::memory_order_relaxed);

            pending =
                workers.size();

            ++generation;
        }

        ready.notify_all();

        execute_current();

        std::unique_lock lock{gate};

        completed.wait(
            lock,
            [&]() noexcept {
                return pending == 0;
            });

        current = {};
        return server_status::success;
    }

private:
    void execute_current() noexcept {
        for (;;) {
            const auto index =
                next.fetch_add(
                    1,
                    std::memory_order_relaxed);

            if (index >= current.size()) {
                return;
            }

            execute_lexical_work(
                current[index]);
        }
    }

    void worker_loop(
        std::stop_token stop) noexcept {

        std::uint64_t observed = 0;

        for (;;) {
            {
                std::unique_lock lock{gate};

                ready.wait(
                    lock,
                    stop,
                    [&]() noexcept {
                        return stopping ||
                            generation != observed;
                    });

                if (stop.stop_requested() ||
                    stopping) {

                    return;
                }

                observed =
                    generation;
            }

            execute_current();

            {
                std::lock_guard lock{gate};

                if (pending != 0) {
                    --pending;
                }

                if (pending == 0) {
                    completed.notify_one();
                }
            }
        }
    }

    std::mutex gate;
    std::condition_variable_any ready;
    std::condition_variable completed;
    std::span<lexical_file_work> current;
    std::atomic_size_t next{0};
    std::size_t pending = 0;
    std::uint64_t generation = 0;
    bool stopping = false;

    // Declared last so jthread destruction joins workers before synchronization
    // state is destroyed, including constructor-unwind paths.
    std::vector<std::jthread> workers;
};

}

server_status lexical_generation::reset(
    std::size_t file_count) noexcept {

    records.clear();
    word_arena.clear();

    if (file_count >
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    try {
        records.resize(
            file_count);
    }
    catch (...) {
        records.clear();
        return server_status::io_error;
    }

    return server_status::success;
}

server_status lexical_generation::publish(
    file_id file,
    const lexical_stream& stream) noexcept {

    if (!file ||
        stream.file() != file ||
        stream.word_count() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::project_configuration_invalid;
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() - 1);

    try {
        if (records.size() <= index) {
            records.resize(
                index + 1);
        }
    }
    catch (...) {
        return server_status::io_error;
    }

    if (records[index].available()) {
        return server_status::project_configuration_invalid;
    }

    const auto values =
        stream.words();

    const auto invalid_offset =
        static_cast<std::size_t>(
            invalid_lexical_offset);

    if (word_arena.size() >=
            invalid_offset ||
        values.size() >
            invalid_offset -
                word_arena.size()) {

        return server_status::io_error;
    }

    const auto offset =
        static_cast<std::uint32_t>(
            word_arena.size());

    try {
        word_arena.insert(
            word_arena.end(),
            values.begin(),
            values.end());
    }
    catch (...) {
        return server_status::io_error;
    }

    records[index] = {
        offset,
        static_cast<std::uint32_t>(
            values.size()),
        stream.token_count(),
    };

    return server_status::success;
}

bool lexical_generation::contains(
    file_id file) const noexcept {

    if (!file) {
        return false;
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() - 1);

    return index < records.size() &&
        records[index].available();
}

std::span<const std::uint32_t>
lexical_generation::words(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    const auto& record =
        records[
            file.value() - 1];

    if (record.word_count == 0) {
        return {};
    }

    return std::span<const std::uint32_t>{
        word_arena.data() + record.offset,
        record.word_count,
    };
}

std::uint32_t lexical_generation::token_count(
    file_id file) const noexcept {

    if (!contains(file)) {
        return 0;
    }

    return records[
        file.value() - 1].token_count;
}

[[nodiscard]] server_status publish_batch(
    file_context& files,
    lexical_generation& output,
    lexical_wave_executor& executor,
    std::vector<lexical_file_work>& work,
    lexical_failure* failure) noexcept {

    const auto executed =
        executor.execute(
            work);

    if (!succeeded(executed)) {
        return executed;
    }

    for (auto& item : work) {
        if (item.job.file) {
            const auto status =
                acquisition_status(
                    item.result.kind);

            if (!succeeded(status)) {
                return status;
            }

            bool content_changed = false;

            const auto applied =
                files.apply_acquire(
                    item.result,
                    content_changed);

            if (!succeeded(applied)) {
                return applied;
            }
        }

        if (!succeeded(
                item.lexical_status)) {

            if (failure != nullptr) {
                failure->file = item.file;
                failure->error = item.error;
            }

            return item.lexical_status;
        }

        const auto published =
            output.publish(
                item.file,
                item.lexical);

        if (!succeeded(published)) {
            return published;
        }
    }

    work.clear();
    return server_status::success;
}

server_status build_lexical_generation(
    file_context& files,
    lexical_generation& output,
    lexical_failure* failure) noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    const auto reset =
        output.reset(
            files.size());

    if (!succeeded(reset)) {
        return reset;
    }

    const auto hardware =
        std::thread::hardware_concurrency();

    const auto batch_capacity =
        static_cast<std::size_t>(
            hardware == 0 ? 1 : hardware);

    std::vector<lexical_file_work> work;

    try {
        work.reserve(
            batch_capacity);
    }
    catch (...) {
        return server_status::io_error;
    }

    try {
        lexical_wave_executor executor{
            batch_capacity};

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

        const auto kind =
            files.kind(file);

        if (kind != file_kind::header &&
            kind != file_kind::source) {

            continue;
        }

        lexical_file_work item;
        item.file = file;

        const auto* physical =
            files.physical(file);

        if (physical == nullptr) {
            return server_status::
                project_configuration_invalid;
        }

        if (physical->present()) {
            if (!files.content_available(file)) {
                return server_status::
                    project_artifact_invalid;
            }

            item.source =
                files.content(file);
        } else {
            const auto prepared =
                files.prepare_acquire(
                    file,
                    item.job);

            if (!succeeded(prepared)) {
                return prepared;
            }
        }

        try {
            work.push_back(
                std::move(item));
        }
        catch (...) {
            return server_status::io_error;
        }

        if (work.size() ==
            batch_capacity) {

            const auto published =
                publish_batch(
                    files,
                    output,
                    executor,
                    work,
                    failure);

            if (!succeeded(published)) {
                return published;
            }
        }
    }

        if (!work.empty()) {
            return publish_batch(
                files,
                output,
                executor,
                work,
                failure);
        }

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

}
