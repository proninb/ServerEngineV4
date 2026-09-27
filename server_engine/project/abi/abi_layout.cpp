#include "abi_layout.hpp"

namespace cw::server {

abi_layout_key make_abi_layout_key(
    const server_abi_configuration& configuration) noexcept {

    return {
        configuration.target,
        configuration.pack,
    };
}

bool abi_layout_properties(
    abi_target target,
    abi_properties& output) noexcept {

    output = {};

    switch (target) {
    case abi_target::windows_x86:
        output = {
            4,
            4,
            4,
            4,
        };
        return true;

    case abi_target::windows_x64:
    case abi_target::posix_x64:
        output = {
            8,
            8,
            8,
            8,
        };
        return true;
    }

    return false;
}

bool valid_abi_layout_key(
    const abi_layout_key& key) noexcept {

    abi_properties properties;

    const auto target_valid =
        abi_layout_properties(
            key.target,
            properties);

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
