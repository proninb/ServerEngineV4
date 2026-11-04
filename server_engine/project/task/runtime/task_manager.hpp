/*
 * Task process orchestration and strict per-tick barrier.
 * Owns child processes; never owns Project SHM or Graph.
 */
#pragma once
#include "../task_configuration.hpp"
#include "task_process.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cw::server::task_runtime {
class manager final {
public:
    manager() = default;
    ~manager();
    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;

    bool start(task_configuration configuration,
               const std::filesystem::path& project_directory,
               std::uint32_t timeout_ms, std::string& error);
    bool start(task_configuration configuration,
               const std::filesystem::path& project_directory,
               std::uint32_t timeout_ms, const shm_attachment& attachment,
               std::string& error);
    bool tick(std::uint64_t cycle, bool frozen, std::string& error);
    bool stop(std::string& error);
    void terminate() noexcept;
    [[nodiscard]] bool active() const noexcept { return started; }
private:
    struct worker {
        std::unique_ptr<process> child;
    };
    task_configuration configuration;
    std::vector<worker> workers;
    bool started = false;
};
}
