#include "project/persistence/crc64_ecma.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

// Independent bitwise ECMA-182 reference: no production tables or folding.
constexpr std::uint64_t reference_crc(std::span<const std::byte> bytes) {
    std::uint64_t crc = 0;
    for (const auto byte : bytes) {
        crc ^= std::uint64_t{std::to_integer<std::uint8_t>(byte)} << 56;
        for (unsigned bit = 0; bit != 8; ++bit) {
            const bool high = (crc >> 63) != 0;
            crc <<= 1;
            if (high) {
                crc ^= 0x42f0e1eba9ea3693ULL;
            }
        }
    }
    return crc;
}

constexpr auto compile_time_input = [] {
    std::array<std::byte, 49> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(index * 37);
    }
    return bytes;
}();
static_assert(cw::server::persistence_crc64(compile_time_input) ==
              reference_crc(compile_time_input));
static_assert(cw::server::persistence_crc64({}) == 0);

bool check(std::span<const std::byte> bytes, std::size_t offset) {
    if (cw::server::persistence_crc64(bytes) == reference_crc(bytes)) {
        return true;
    }
    std::cerr << "CRC mismatch: offset=" << offset << ", size=" << bytes.size() << '\n';
    return false;
}

}

int main() {
    std::vector<std::byte> bytes(65536 + 32);
    std::uint32_t random = 0x9e3779b9;
    for (unsigned pattern = 0; pattern != 4; ++pattern) {
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            bytes[index] = static_cast<std::byte>(pattern == 0 ? 0 :
                pattern == 1 ? 255 : pattern == 2 ? index & 255 : random & 255);
        }
        for (std::size_t offset = 0; offset != 16; ++offset) {
            const auto view = std::span<const std::byte>{bytes}.subspan(offset);
            for (std::size_t size = 0; size <= 320; ++size) {
                if (!check(view.first(size), offset)) {
                    return 1;
                }
            }
            for (const std::size_t size : {1023u, 1024u, 1025u, 4095u, 4096u, 4097u, 65536u}) {
                if (!check(view.first(size), offset)) {
                    return 1;
                }
            }
        }
    }
    std::cout << "CRC-64 reference, alignment and tail tests passed\n";
    return 0;
}
