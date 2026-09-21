#include "abi_layout.hpp"

namespace cw::server {

abi_layout_key make_abi_layout_key(
    const server_abi_configuration& configuration) noexcept {

    return {
        configuration.target,
        configuration.pack,
    };
}

bool valid_abi_layout_key(
    const abi_layout_key& key) noexcept {

    const auto target_valid =
        key.target == abi_target::windows_x64 ||
        key.target == abi_target::posix_x64;

    const auto pack_valid =
        key.pack == 1 ||
        key.pack == 2 ||
        key.pack == 4 ||
        key.pack == 8 ||
        key.pack == 16;

    return target_valid && pack_valid;
}

bool abi_layout_compatible(
    const abi_layout_key& key,
    const server_abi_configuration& configuration) noexcept {

    return
        valid_abi_layout_key(key) &&
        key == make_abi_layout_key(configuration);
}

}
