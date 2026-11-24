/* SimTask executable entry point. No user-defined main() is required. */
#include <SimTask.hpp>
#include "../../server_engine/project/task/runtime/task_host.hpp"
#include "../../server_engine/project/task/runtime/task_variables.hpp"

#include <Windows.h>
#include <charconv>
#include <string>

namespace {
bool initialize() {
    TASKINIT();
    return true;
}
bool execute() {
    return TASKMAIN() == 0;
}
void finalize() {
    TASKDONE();
}

struct catalog_view {
    const std::byte* bytes = nullptr;

    explicit catalog_view(std::string_view mapping) noexcept {
        try { open(mapping); } catch (...) {}
    }
    catalog_view(const catalog_view&) = delete;
    catalog_view& operator=(const catalog_view&) = delete;
    ~catalog_view() { if (bytes) UnmapViewOfFile(bytes); }

    void open(std::string_view mapping) {
        using namespace cw::server::task_runtime;
        std::wstring name(mapping.begin(), mapping.end());
        name.append(variable_catalog_suffix.begin(), variable_catalog_suffix.end());
        const auto handle = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
        if (!handle) return;
        bytes = static_cast<const std::byte*>(MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0));
        CloseHandle(handle);
    }
};
}

int main() {
    return cw::server::task_runtime::run_host({
        &initialize, &execute, &finalize
    });
}

namespace SimTask {
void* SharedMemory() noexcept {
    return cw::server::task_runtime::shared_memory_data();
}
std::size_t SharedMemorySize() noexcept {
    return cw::server::task_runtime::shared_memory_size();
}

Variable getvar(std::string_view name) noexcept {
    using namespace cw::server::task_runtime;
    if (!SharedMemory()) return {};
    // Each host attaches once and exits after detaching.
    static const catalog_view catalog(shared_memory_name());
    if (!catalog.bytes) return {};
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(catalog.bytes, &region, sizeof region) || region.RegionSize < sizeof(variable_catalog_header))
        return {};
    const auto& header = *reinterpret_cast<const variable_catalog_header*>(catalog.bytes);
    if (header.magic != variable_catalog_magic || header.size < sizeof header || header.size > region.RegionSize ||
        header.count > (header.size - sizeof header) / sizeof(variable_catalog_entry)) return {};
    const auto* entries = reinterpret_cast<const variable_catalog_entry*>(catalog.bytes + sizeof header);
    const auto names_offset = sizeof header + header.count * sizeof(variable_catalog_entry);
    const auto names_size = header.size - names_offset;
    const auto* names = reinterpret_cast<const char*>(catalog.bytes + names_offset);
    if (name.starts_with("::")) name.remove_prefix(2);
    for (std::uint32_t i = 0; i < header.count; ++i) {
        const auto& entry = entries[i];
        if (entry.name_offset > names_size || entry.name_size > names_size - entry.name_offset ||
            !entry.size || entry.offset > SharedMemorySize() || entry.size > SharedMemorySize() - entry.offset)
            return {};
        const std::string_view spelling(names + entry.name_offset, entry.name_size);
        auto bit = entry.bit;
        if (name != spelling) {
            if (!entry.bit_count || !name.starts_with(spelling) ||
                name.size() <= spelling.size() + 1 || name[spelling.size()] != '.') continue;
            const auto suffix = name.substr(spelling.size() + 1);
            unsigned value = 0;
            const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() ||
                value >= entry.bit_count) continue;
            bit = static_cast<std::uint8_t>(value);
        }
        if (bit != no_bit && bit / 8 >= entry.size) return {};
        return {static_cast<std::byte*>(SharedMemory()) + entry.offset, entry.size, bit};
    }
    return {};
}
}
