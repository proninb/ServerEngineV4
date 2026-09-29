#include "communication/protocol/protocol_contract.hpp"
#include "communication/server_request_message.hpp"
#include "communication/server_response.hpp"

#include <iostream>

using namespace cw::server;

namespace {

struct capture final {
    request_id request;
    bool called = false;
};

void present(void* context, request_id request, const server_response&) {
    auto& value = *static_cast<capture*>(context);
    value.request = request;
    value.called = true;
}

}

int main() {
    static_assert(
        project_state_can_run(
            project_state::loaded));

    static_assert(
        project_state_can_run(
            project_state::freeze));

    static_assert(
        !project_state_can_run(
            project_state::run));

    static_assert(
        project_state_can_freeze(
            project_state::run));

    static_assert(
        !project_state_can_freeze(
            project_state::loaded));

    static_assert(
        project_state_can_unload(
            project_state::loaded));

    static_assert(
        project_state_can_unload(
            project_state::freeze));

    static_assert(
        !project_state_can_unload(
            project_state::run));

    static_assert(
        static_cast<std::uint8_t>(
            snap_ic_options::none) == 0x00);

    static_assert(
        static_cast<std::uint8_t>(
            reset_ic_options::none) == 0x00);

    static_assert(
        static_cast<std::uint8_t>(
            reset_ic_options::constants) == 0x01);

    static_assert(
        static_cast<std::uint8_t>(
            reset_ic_options::variables) == 0x02);

    static_assert(
        static_cast<std::uint8_t>(
            reset_ic_options::defaults) == 0x04);

    constexpr auto excluded =
        reset_ic_options::constants |
        reset_ic_options::variables;

    static_assert(
        has_option(
            excluded,
            reset_ic_options::constants));

    static_assert(
        has_option(
            excluded,
            reset_ic_options::variables));

    static_assert(
        !has_option(
            excluded,
            reset_ic_options::defaults));

    if (request_id{}.valid() || !request_id{42}.valid()) return 1;

    capture value;
    server_request_origin origin{&value, &present, request_id{42}};
    origin.present(server_response{});

    if (!value.called || value.request != request_id{42}) return 2;

    client_message broadcast;
    if (!broadcast.login.empty()) return 3;

    server_state_action state{
        project_state::unloaded,
        project_state::loaded,
    };

    if (state.old_state != project_state::unloaded ||
        state.new_state != project_state::loaded) return 4;

    return 0;
}

