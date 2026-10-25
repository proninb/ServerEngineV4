#pragma once
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>

namespace runtime_object_lanes01 {
// CNG hashes logical resident bytes, including FIXED_DIRECT pointers, after timing.
inline std::string sha256(std::span<const std::byte> bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    struct cleanup {
        BCRYPT_ALG_HANDLE& algorithm; BCRYPT_HASH_HANDLE& hash;
        ~cleanup() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } release{algorithm, hash};
    const auto check = [](NTSTATUS status) { if (status < 0) throw std::runtime_error("SHM SHA-256 audit failed"); };
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
    while (!bytes.empty()) {
        const auto n = (std::min)(bytes.size(), std::size_t{64 * 1024 * 1024});
        check(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), static_cast<ULONG>(n), 0));
        bytes = bytes.subspan(n);
    }
    std::array<unsigned char, 32> digest{};
    check(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    for (auto c : digest) { output += hex[c >> 4]; output += hex[c & 15]; }
    return output;
}
}
