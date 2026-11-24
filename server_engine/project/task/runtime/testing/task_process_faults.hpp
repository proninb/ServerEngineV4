#pragma once
// Compiled only into the dedicated lifecycle regression executable.
#include <atomic>
namespace cw::server::task_runtime::testing {
inline std::atomic_bool deny_termination{false};
}
