#include "lexical_generation.hpp"

#include "lexer.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
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

[[nodiscard]] server_status execute_parallel(
    std::span<lexical_file_work> work) noexcept {

    if (work.empty()) {
        return server_status::success;
    }

    const auto hardware =
        std::thread::hardware_concurrency();

    const auto worker_count =
        (std::min)(
            work.size(),
            static_cast<std::size_t>(
                hardware == 0 ? 1 : hardware));

    std::atomic_size_t next{0};

    const auto execute = [&]() noexcept {
        for (;;) {
            const auto index =
                next.fetch_add(
                    1,
                    std::memory_order_relaxed);

            if (index >= work.size()) {
                return;
            }

            auto& item =
                work[index];

            auto source =
                item.source;

            if (item.job.file) {
                file_context::execute_acquire(
                    item.job,
                    item.result);

                if (item.result.kind !=
                    file_acquire_result_kind::present) {

                    continue;
                }

                source =
                    item.result.snapshot.bytes;
            }

            item.lexical_status =
                lexer::tokenize(
                    item.file,
                    source,
                    item.lexical);
        }
    };

    if (worker_count == 1) {
        execute();
        return server_status::success;
    }

    try {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count - 1);

        for (std::size_t index = 1;
             index < worker_count;
             ++index) {

            workers.emplace_back(execute);
        }

        execute();
    }
    catch (...) {
        return server_status::io_error;
    }

    return server_status::success;
}

}

server_status build_lexical_generation(
    file_context& files,
    std::vector<lexical_stream>& output) noexcept {

    output.clear();

    std::vector<lexical_file_work> work;

    try {
        output.resize(
            files.size());

        work.reserve(
            files.size());
    }
    catch (...) {
        output.clear();
        return server_status::io_error;
    }

    for (std::uint32_t value = 1;
         value <= files.size();
         ++value) {

        const file_id file{value};

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
            output.clear();
            return server_status::
                project_configuration_invalid;
        }

        if (physical->present()) {
            if (!files.content_available(file)) {
                output.clear();
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
                output.clear();
                return prepared;
            }
        }

        try {
            work.push_back(
                std::move(item));
        }
        catch (...) {
            output.clear();
            return server_status::io_error;
        }
    }

    const auto executed =
        execute_parallel(
            work);

    if (!succeeded(executed)) {
        output.clear();
        return executed;
    }

    for (auto& item : work) {
        if (item.job.file) {
            const auto status =
                acquisition_status(
                    item.result.kind);

            if (!succeeded(status)) {
                output.clear();
                return status;
            }

            bool content_changed = false;

            const auto applied =
                files.apply_acquire(
                    item.result,
                    content_changed);

            if (!succeeded(applied)) {
                output.clear();
                return applied;
            }
        }

        if (!succeeded(
                item.lexical_status)) {

            output.clear();
            return item.lexical_status;
        }

        output[
            item.file.value() - 1] =
                std::move(
                    item.lexical);
    }

    return server_status::success;
}

}
