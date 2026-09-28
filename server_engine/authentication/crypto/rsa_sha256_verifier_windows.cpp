/*
 * Windows CNG implementation of the RS256 verification boundary.
 */
#include "rsa_sha256_verifier.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace cw::server {
namespace {

class algorithm_handle final {
public:
    algorithm_handle() = default;

    ~algorithm_handle() {
        if (value != nullptr) {
            BCryptCloseAlgorithmProvider(
                value,
                0);
        }
    }

    algorithm_handle(
        const algorithm_handle&) = delete;

    algorithm_handle& operator=(
        const algorithm_handle&) = delete;

    BCRYPT_ALG_HANDLE value = nullptr;
};

class key_handle final {
public:
    key_handle() = default;

    ~key_handle() {
        if (value != nullptr) {
            BCryptDestroyKey(
                value);
        }
    }

    key_handle(
        const key_handle&) = delete;

    key_handle& operator=(
        const key_handle&) = delete;

    BCRYPT_KEY_HANDLE value = nullptr;
};

class hash_handle final {
public:
    hash_handle() = default;

    ~hash_handle() {
        if (value != nullptr) {
            BCryptDestroyHash(
                value);
        }
    }

    hash_handle(
        const hash_handle&) = delete;

    hash_handle& operator=(
        const hash_handle&) = delete;

    BCRYPT_HASH_HANDLE value = nullptr;
};

[[nodiscard]] bool success(
    NTSTATUS status) noexcept {

    return status >= 0;
}

[[nodiscard]] bool to_ulong(
    std::size_t value,
    ULONG& output) noexcept {

    if (value >
        static_cast<std::size_t>(
            std::numeric_limits<ULONG>::max())) {

        return false;
    }

    output =
        static_cast<ULONG>(value);

    return true;
}

}

rsa_sha256_verify_result verify_rsa_sha256(
    std::span<const std::byte> modulus,
    std::span<const std::byte> exponent,
    std::string_view message,
    std::span<const std::byte> signature) noexcept {

    try {
        if (modulus.empty() ||
            exponent.empty() ||
            signature.empty()) {

            return rsa_sha256_verify_result::invalid_key;
        }

        ULONG modulus_size = 0;
        ULONG exponent_size = 0;
        ULONG signature_size = 0;
        ULONG message_size = 0;

        if (!to_ulong(modulus.size(), modulus_size) ||
            !to_ulong(exponent.size(), exponent_size) ||
            !to_ulong(signature.size(), signature_size) ||
            !to_ulong(message.size(), message_size) ||
            modulus.size() >
                (std::numeric_limits<ULONG>::max() / 8u)) {

            return rsa_sha256_verify_result::invalid_key;
        }

        algorithm_handle rsa;

        if (!success(
                BCryptOpenAlgorithmProvider(
                    &rsa.value,
                    BCRYPT_RSA_ALGORITHM,
                    nullptr,
                    0))) {

            return rsa_sha256_verify_result::failure;
        }

        const std::size_t blob_size =
            sizeof(BCRYPT_RSAKEY_BLOB) +
            exponent.size() +
            modulus.size();

        std::vector<UCHAR> blob(
            blob_size);

        auto* header =
            reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(
                blob.data());

        header->Magic =
            BCRYPT_RSAPUBLIC_MAGIC;
        header->BitLength =
            static_cast<ULONG>(
                modulus.size() * 8u);
        header->cbPublicExp =
            exponent_size;
        header->cbModulus =
            modulus_size;
        header->cbPrime1 = 0;
        header->cbPrime2 = 0;

        auto* cursor =
            blob.data() +
            sizeof(BCRYPT_RSAKEY_BLOB);

        std::memcpy(
            cursor,
            exponent.data(),
            exponent.size());

        cursor +=
            exponent.size();

        std::memcpy(
            cursor,
            modulus.data(),
            modulus.size());

        ULONG blob_size_ulong = 0;

        if (!to_ulong(
                blob.size(),
                blob_size_ulong)) {

            return rsa_sha256_verify_result::invalid_key;
        }

        key_handle key;

        if (!success(
                BCryptImportKeyPair(
                    rsa.value,
                    nullptr,
                    BCRYPT_RSAPUBLIC_BLOB,
                    &key.value,
                    blob.data(),
                    blob_size_ulong,
                    0))) {

            return rsa_sha256_verify_result::invalid_key;
        }

        algorithm_handle sha256;

        if (!success(
                BCryptOpenAlgorithmProvider(
                    &sha256.value,
                    BCRYPT_SHA256_ALGORITHM,
                    nullptr,
                    0))) {

            return rsa_sha256_verify_result::failure;
        }

        ULONG object_size = 0;
        ULONG result_size = 0;

        if (!success(
                BCryptGetProperty(
                    sha256.value,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(
                        &object_size),
                    sizeof(object_size),
                    &result_size,
                    0))) {

            return rsa_sha256_verify_result::failure;
        }

        std::vector<UCHAR> hash_object(
            object_size);

        hash_handle hash;

        if (!success(
                BCryptCreateHash(
                    sha256.value,
                    &hash.value,
                    hash_object.data(),
                    object_size,
                    nullptr,
                    0,
                    0))) {

            return rsa_sha256_verify_result::failure;
        }

        if (!success(
                BCryptHashData(
                    hash.value,
                    reinterpret_cast<PUCHAR>(
                        const_cast<char*>(
                            message.data())),
                    message_size,
                    0))) {

            return rsa_sha256_verify_result::failure;
        }

        std::array<UCHAR, 32> digest{};

        if (!success(
                BCryptFinishHash(
                    hash.value,
                    digest.data(),
                    static_cast<ULONG>(
                        digest.size()),
                    0))) {

            return rsa_sha256_verify_result::failure;
        }

        BCRYPT_PKCS1_PADDING_INFO padding{
            BCRYPT_SHA256_ALGORITHM,
        };

        const auto verified =
            BCryptVerifySignature(
                key.value,
                &padding,
                digest.data(),
                static_cast<ULONG>(
                    digest.size()),
                reinterpret_cast<PUCHAR>(
                    const_cast<std::byte*>(
                        signature.data())),
                signature_size,
                BCRYPT_PAD_PKCS1);

        return success(verified)
            ? rsa_sha256_verify_result::success
            : rsa_sha256_verify_result::invalid_signature;
    }
    catch (...) {
        return rsa_sha256_verify_result::failure;
    }
}

}
