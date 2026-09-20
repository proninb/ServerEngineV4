#include "lexical_generation.hpp"

#include "lexer.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
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

    if (word_arena.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)()) ||
        values.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)()) -
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
    std::vector<lexical_file_work>& work) noexcept {

    const auto executed =
        execute_parallel(
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
    lexical_generation& output) noexcept {

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
                    work);

            if (!succeeded(published)) {
                return published;
            }
        }
    }

    if (!work.empty()) {
        return publish_batch(
            files,
            output,
            work);
    }

    return server_status::success;
}

}
