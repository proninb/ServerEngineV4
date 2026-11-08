#include "project/task/runtime/task_runtime_table.hpp"
#include <cstring>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (false)

int main() {
    using namespace cw::server::task_runtime;
    runtime_table table;
    CHECK(table.magic == runtime_table_magic);
    CHECK(table.version == runtime_table_version);
    CHECK(std::strcmp(table.name, "system") == 0);
    const auto offset = align_up(sizeof(runtime_table), alignof(task_record));
    CHECK(offset % alignof(task_record) == 0);
    CHECK(sizeof(task_record) % alignof(task_record) == 0);
    const auto dep = align_up(offset + 3 * sizeof(task_record), alignof(std::uint32_t));
    CHECK(dep > offset);
}
