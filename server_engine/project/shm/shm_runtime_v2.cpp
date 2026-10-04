#include "shm_runtime_v2.hpp"

namespace cw::server {

shm_runtime_v2_result prepare_shm_runtime_v2(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry) noexcept {

    const auto result =
        prepare_shm_type_batch_inline64(
            project,
            abi,
            layout,
            output.area,
            telemetry);

    // The public Runtime V2 contract is fixed at 64. Fail closed if the
    // underlying experimental builder ever changes policy semantics.
    if (result ==
            shm_type_batch_result::success &&
        telemetry != nullptr &&
        telemetry->inline_leaf_limit !=
            shm_runtime_v2_inline_leaf_limit) {

        output.area = {};
        *telemetry = {};
        return shm_type_batch_result::
            invalid_input;
    }

    return result;
}

shm_runtime_v2_result
materialize_shm_runtime_v2_canonical(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry) noexcept {

    return materialize_shm_type_batch_canonical(
        runtime.area,
        abi,
        layout,
        shm,
        telemetry);
}

shm_runtime_v2_result
materialize_shm_runtime_v2_objects(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry) noexcept {

    return
        materialize_shm_type_batch_objects_object_major(
            runtime.area,
            abi,
            layout,
            shm,
            telemetry);
}

}
