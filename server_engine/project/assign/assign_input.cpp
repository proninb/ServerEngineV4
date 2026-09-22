#include "assign_input.hpp"

#include "../construction/execution_lanes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

namespace cw::server {
namespace {

struct assign_lane_state final {
    file_acquire_job job;
    file_acquire_result result;
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

// Materializes Assign byte images in O(file_count) discovery time and
// O(CPU lanes) temporary state. File Context publication remains single-owner.
class assign_input_materializer final {
public:
    explicit assign_input_materializer(
        file_context& files) noexcept
        : files(files) {
    }

    [[nodiscard]] server_status run() noexcept {

        lane_capacity =
            execution_lane_capacity();

        if (lane_capacity == 0) {
            return server_status::io_error;
        }

        lanes.reset(
            new (std::nothrow)
                assign_lane_state[
                    lane_capacity]);

        if (!lanes) {
            return server_status::io_error;
        }

        std::size_t pending = 0;

        const auto count =
            files.size();

        for (std::size_t index = 0;
             index < count;
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (files.kind(file) !=
                file_kind::assign) {

                continue;
            }

            if (files.content_available(file)) {
                continue;
            }

            auto& lane =
                lanes[pending];

            lane = {};

            const auto prepared =
                files.prepare_acquire(
                    file,
                    lane.job);

            if (!succeeded(prepared)) {
                return prepared;
            }

            ++pending;

            if (pending != lane_capacity) {
                continue;
            }

            const auto flushed =
                flush(pending);

            if (!succeeded(flushed)) {
                return flushed;
            }

            pending = 0;
        }

        if (pending == 0) {
            return server_status::success;
        }

        return flush(
            pending);
    }

private:
    static void execute_lane(
        void* context,
        std::size_t lane) noexcept {

        auto& self =
            *static_cast<
                assign_input_materializer*>(
                    context);

        auto& state =
            self.lanes[lane];

        file_context::execute_acquire(
            state.job,
            state.result);
    }

    [[nodiscard]] server_status flush(
        std::size_t active_lanes) noexcept {

        if (active_lanes == 0 ||
            active_lanes > lane_capacity) {

            return server_status::project_configuration_invalid;
        }

        if (workers.size() == 0) {
            const auto started =
                workers.start(
                    active_lanes);

            if (!succeeded(started)) {
                return started;
            }
        }

        if (active_lanes >
            workers.size()) {

            return server_status::project_artifact_invalid;
        }

        const auto executed =
            workers.run(
                active_lanes,
                execute_lane,
                this);

        if (!succeeded(executed)) {
            return executed;
        }

        for (std::size_t lane = 0;
             lane < active_lanes;
             ++lane) {

            auto& state =
                lanes[lane];

            const auto acquired =
                acquisition_status(
                    state.result.kind);

            if (!succeeded(acquired)) {
                return acquired;
            }

            bool content_changed = false;

            const auto applied =
                files.apply_acquire(
                    state.result,
                    content_changed);

            if (!succeeded(applied)) {
                return applied;
            }

            if (!files.content_available(
                    state.job.file)) {

                return server_status::project_artifact_invalid;
            }
        }

        return server_status::success;
    }

    file_context& files;

    std::size_t lane_capacity = 0;
    std::unique_ptr<assign_lane_state[]> lanes;
    execution_lanes workers;
};

[[nodiscard]] std::string_view trim_assign_text(
    std::string_view value) noexcept {

    while (!value.empty() &&
           (value.front() == ' ' ||
            value.front() == '\t')) {

        value.remove_prefix(1);
    }

    while (!value.empty() &&
           (value.back() == ' ' ||
            value.back() == '\t')) {

        value.remove_suffix(1);
    }

    return value;
}

[[nodiscard]] server_status assign_failure(
    assign_parse_failure* failure,
    file_id file,
    std::uint32_t offset,
    std::uint32_t length,
    std::string_view detail) noexcept {

    if (failure != nullptr) {
        failure->file = file;
        failure->offset = offset;
        failure->length = length;
        failure->detail = detail;
    }

    return server_status::project_configuration_invalid;
}

[[nodiscard]] server_status parse_assign_file(
    file_id file,
    std::string_view content,
    assign_table& output,
    assign_parse_failure* failure) noexcept {

    std::size_t line_begin = 0;

    while (line_begin <= content.size()) {
        const auto newline =
            content.find(
                '\n',
                line_begin);

        const auto line_end =
            newline == std::string_view::npos
            ? content.size()
            : newline;

        auto physical_end =
            line_end;

        if (physical_end > line_begin &&
            content[physical_end - 1] == '\r') {

            --physical_end;
        }

        auto line =
            trim_assign_text(
                content.substr(
                    line_begin,
                    physical_end - line_begin));

        if (!line.empty()) {
            const auto equal =
                line.find('=');

            const auto tab =
                line.find('\t');

            std::string_view source;
            std::string_view target;

            if (equal !=
                std::string_view::npos) {

                if (tab !=
                        std::string_view::npos ||
                    line.find(
                        '=',
                        equal + 1) !=
                            std::string_view::npos) {

                    return assign_failure(
                        failure,
                        file,
                        static_cast<std::uint32_t>(
                            line_begin),
                        static_cast<std::uint32_t>(
                            physical_end - line_begin),
                        "Assign line must contain exactly one '=' or one tab separator");
                }

                target =
                    trim_assign_text(
                        line.substr(
                            0,
                            equal));

                source =
                    trim_assign_text(
                        line.substr(
                            equal + 1));
            }
            else {
                if (tab ==
                        std::string_view::npos ||
                    line.find(
                        '\t',
                        tab + 1) !=
                            std::string_view::npos) {

                    return assign_failure(
                        failure,
                        file,
                        static_cast<std::uint32_t>(
                            line_begin),
                        static_cast<std::uint32_t>(
                            physical_end - line_begin),
                        "Assign line must contain exactly one '=' or one tab separator");
                }

                source =
                    trim_assign_text(
                        line.substr(
                            0,
                            tab));

                target =
                    trim_assign_text(
                        line.substr(
                            tab + 1));
            }

            if (source.empty() ||
                target.empty()) {

                return assign_failure(
                    failure,
                    file,
                    static_cast<std::uint32_t>(
                        line_begin),
                    static_cast<std::uint32_t>(
                        physical_end - line_begin),
                    "Assign source and target must both be non-empty");
            }

            const auto added =
                output.add(
                    source,
                    target);

            if (!succeeded(added)) {
                return added;
            }
        }

        if (newline ==
            std::string_view::npos) {

            break;
        }

        line_begin =
            newline + 1;
    }

    return server_status::success;
}

}

server_status materialize_assign_inputs(
    file_context& files) noexcept {

    assign_input_materializer materializer{
        files};

    return materializer.run();
}

server_status parse_assign_inputs(
    const file_context& files,
    assign_table& output,
    assign_parse_failure* failure) noexcept {

    if (failure != nullptr) {
        *failure = {};
    }

    output.clear();

    const auto count =
        files.size();

    for (std::size_t index = 0;
         index < count;
         ++index) {

        const file_id file{
            static_cast<std::uint32_t>(
                index + 1)};

        if (files.kind(file) !=
            file_kind::assign) {

            continue;
        }

        if (!files.content_available(file)) {
            return server_status::project_artifact_invalid;
        }

        const auto parsed =
            parse_assign_file(
                file,
                files.content(file),
                output,
                failure);

        if (!succeeded(parsed)) {
            return parsed;
        }
    }

    return server_status::success;
}

}
