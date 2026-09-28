#include "authentication/authentication.hpp"

#include <iostream>

namespace {

using namespace cw::server;

[[nodiscard]] bool verify_none() {
    authentication_service authentication;

    authentication_configuration configuration;
    configuration.mode = authentication_mode::none;

    if (!succeeded(authentication.start(configuration)) ||
        !authentication.ready() ||
        authentication.mode() != authentication_mode::none) {
        return false;
    }

    authentication.stop();
    return !authentication.ready();
}

[[nodiscard]] bool verify_unsupported(authentication_mode mode) {
    authentication_service authentication;

    authentication_configuration configuration;
    configuration.mode = mode;

    return authentication.start(configuration) == server_status::unsupported &&
           !authentication.ready();
}

}

int main() {
    if (!verify_none()) {
        std::cerr << "authentication mode=none lifecycle failed\n";
        return 1;
    }

    if (!verify_unsupported(authentication_mode::contract)) {
        std::cerr << "authentication mode=contract must fail closed until implemented\n";
        return 1;
    }

    if (!verify_unsupported(authentication_mode::external)) {
        std::cerr << "authentication mode=external must fail closed until implemented\n";
        return 1;
    }

    return 0;
}
