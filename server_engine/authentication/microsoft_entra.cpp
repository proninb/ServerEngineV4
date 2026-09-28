/*
 * Microsoft Entra offline startup cache loader.
 */
#include "microsoft_entra.hpp"

#include "entra_jwt.hpp"
#include "../json/json_parser.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace cw::server {
namespace {

enum class cache_context : std::uint8_t {
    root,
    keys,
    key,
};

enum class cache_field : std::uint8_t {
    none = 0,
    version,
    tenant_id,
    audience,
    issuer,
    jwks_uri,
    retrieved_at,
    keys,
    kid,
    kty,
    alg,
    n,
    e,
    key_issuer,
};

struct cache_frame final {
    cache_context context = cache_context::root;
    cache_field field = cache_field::none;
    std::uint32_t seen = 0;
};

struct cache_state final {
    std::uint32_t version = 0;
    std::string tenant_id;
    std::string audience;
    std::string issuer;
    std::string jwks_uri;
    std::string retrieved_at;
    std::vector<microsoft_entra_signing_key> keys;
    std::vector<cache_frame> stack;
    bool root_started = false;
    bool root_completed = false;
    bool valid = true;
    std::string detail;
};

[[nodiscard]] constexpr std::uint32_t field_bit(
    cache_field field) noexcept {

    const auto value =
        static_cast<std::uint8_t>(field);

    return value == 0
        ? 0
        : (std::uint32_t{1} << value);
}

[[nodiscard]] bool seen(
    const cache_frame& frame,
    cache_field field) noexcept {

    return (frame.seen &
            field_bit(field)) != 0;
}

class cache_handler final : public json_event_handler {
public:
    explicit cache_handler(
        cache_state& state)
        : state(state) {

        state.stack.reserve(3);
    }

    void object_begin() override {
        if (!state.valid) {
            return;
        }

        if (state.stack.empty()) {
            if (state.root_started) {
                fail("entra.cache must contain exactly one root object");
                return;
            }

            state.root_started = true;
            state.stack.push_back({
                cache_context::root,
                cache_field::none,
                0,
            });
            return;
        }

        if (state.stack.back().context ==
            cache_context::keys) {

            state.keys.emplace_back();

            state.stack.push_back({
                cache_context::key,
                cache_field::none,
                0,
            });
            return;
        }

        fail("unexpected object in entra.cache");
    }

    void object_end() override {
        if (!state.valid ||
            state.stack.empty()) {
            return;
        }

        const auto frame =
            state.stack.back();

        if (frame.context ==
            cache_context::root) {

            if (!seen(frame, cache_field::version) ||
                !seen(frame, cache_field::tenant_id) ||
                !seen(frame, cache_field::audience) ||
                !seen(frame, cache_field::issuer) ||
                !seen(frame, cache_field::jwks_uri) ||
                !seen(frame, cache_field::retrieved_at) ||
                !seen(frame, cache_field::keys)) {

                fail(
                    "entra.cache requires version, tenant_id, audience, "
                    "issuer, jwks_uri, retrieved_at, and keys");
                return;
            }

            state.root_completed = true;
            state.stack.pop_back();
            return;
        }

        if (frame.context ==
            cache_context::key) {

            if (!seen(frame, cache_field::kid) ||
                !seen(frame, cache_field::kty) ||
                !seen(frame, cache_field::n) ||
                !seen(frame, cache_field::e)) {

                fail(
                    "each entra.cache signing key requires kid, kty, n, and e");
                return;
            }

            state.stack.pop_back();
            return;
        }

        fail("unexpected object end in entra.cache");
    }

    void array_begin() override {
        if (!state.valid ||
            state.stack.empty()) {

            fail("unexpected array in entra.cache");
            return;
        }

        auto& parent =
            state.stack.back();

        if (parent.context ==
                cache_context::root &&
            parent.field ==
                cache_field::keys) {

            parent.field =
                cache_field::none;

            state.stack.push_back({
                cache_context::keys,
                cache_field::none,
                0,
            });
            return;
        }

        fail("unexpected array in entra.cache");
    }

    void array_end() override {
        if (!state.valid ||
            state.stack.empty() ||
            state.stack.back().context !=
                cache_context::keys) {

            fail("unexpected array end in entra.cache");
            return;
        }

        if (state.keys.empty()) {
            fail("entra.cache signing key set cannot be empty");
            return;
        }

        state.stack.pop_back();
    }

    void key(
        std::string_view key) override {

        if (!state.valid ||
            state.stack.empty()) {
            return;
        }

        auto& frame =
            state.stack.back();

        cache_field field =
            cache_field::none;

        if (frame.context ==
            cache_context::root) {

            if (key == "version") field = cache_field::version;
            else if (key == "tenant_id") field = cache_field::tenant_id;
            else if (key == "audience") field = cache_field::audience;
            else if (key == "issuer") field = cache_field::issuer;
            else if (key == "jwks_uri") field = cache_field::jwks_uri;
            else if (key == "retrieved_at") field = cache_field::retrieved_at;
            else if (key == "keys") field = cache_field::keys;
        }
        else if (frame.context ==
            cache_context::key) {

            if (key == "kid") field = cache_field::kid;
            else if (key == "kty") field = cache_field::kty;
            else if (key == "alg") field = cache_field::alg;
            else if (key == "n") field = cache_field::n;
            else if (key == "e") field = cache_field::e;
            else if (key == "issuer") field = cache_field::key_issuer;
        }

        if (field ==
            cache_field::none) {

            fail(
                "unknown property in entra.cache: " +
                std::string(key));
            return;
        }

        const auto bit =
            field_bit(field);

        if ((frame.seen & bit) != 0) {
            fail(
                "duplicate property in entra.cache: " +
                std::string(key));
            return;
        }

        frame.seen |= bit;
        frame.field = field;
    }

    void value(
        json_value_view value) override {

        if (!state.valid ||
            state.stack.empty()) {
            return;
        }

        auto& frame =
            state.stack.back();

        if (frame.field ==
            cache_field::none) {

            fail("scalar value is not associated with an entra.cache field");
            return;
        }

        if (frame.context ==
            cache_context::root) {

            switch (frame.field) {
            case cache_field::version:
                if (!value.get(state.version)) {
                    fail("entra.cache version must be an unsigned integer");
                }
                break;

            case cache_field::tenant_id:
                read_non_empty(value, state.tenant_id, "tenant_id");
                break;

            case cache_field::audience:
                read_non_empty(value, state.audience, "audience");
                break;

            case cache_field::issuer:
                read_non_empty(value, state.issuer, "issuer");
                break;

            case cache_field::jwks_uri:
                read_non_empty(value, state.jwks_uri, "jwks_uri");
                break;

            case cache_field::retrieved_at:
                read_non_empty(value, state.retrieved_at, "retrieved_at");
                break;

            default:
                fail("invalid scalar root field in entra.cache");
                break;
            }

            frame.field =
                cache_field::none;
            return;
        }

        if (frame.context ==
            cache_context::key) {

            auto& key =
                state.keys.back();

            switch (frame.field) {
            case cache_field::kid:
                read_non_empty(value, key.kid, "keys[].kid");
                break;

            case cache_field::kty:
                read_non_empty(value, key.kty, "keys[].kty");
                break;

            case cache_field::alg:
                read_non_empty(value, key.alg, "keys[].alg");
                break;

            case cache_field::n:
                read_non_empty(value, key.n, "keys[].n");
                break;

            case cache_field::e:
                read_non_empty(value, key.e, "keys[].e");
                break;

            case cache_field::key_issuer:
                read_non_empty(value, key.issuer, "keys[].issuer");
                break;

            default:
                fail("invalid scalar signing-key field in entra.cache");
                break;
            }

            frame.field =
                cache_field::none;
            return;
        }

        fail("unexpected scalar in entra.cache");
    }

private:
    void read_non_empty(
        json_value_view value,
        std::string& output,
        const char* field) {

        if (!value.get(output) ||
            output.empty()) {

            fail(
                std::string("entra.cache ") +
                field +
                " must be a non-empty string");
        }
    }

    void fail(
        std::string detail) {

        if (!state.valid) {
            return;
        }

        state.valid = false;
        state.detail = std::move(detail);
    }

    cache_state& state;
};

[[nodiscard]] bool read_text(
    const std::filesystem::path& path,
    std::string& output) {

    std::ifstream stream(
        path,
        std::ios::binary);

    if (!stream) {
        return false;
    }

    output.assign(
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{});

    return stream.good() ||
           stream.eof();
}

}

server_status microsoft_entra_authentication::start(
    const external_authentication_configuration& configuration,
    const std::filesystem::path& configuration_directory,
    std::string& detail) noexcept {

    stop();
    detail.clear();

    try {
        const auto path =
            configuration_directory /
            "entra.cache";

        std::string text;

        if (!read_text(
                path,
                text)) {

            detail =
                "Cannot read Microsoft Entra cache: " +
                path.string();

            return server_status::authentication_start_failed;
        }

        cache_state state;
        cache_handler handler{state};

        const auto parsed =
            parse_json(
                text,
                handler);

        if (!parsed.ok()) {
            detail =
                "Invalid Microsoft Entra cache JSON: " +
                std::string(
                    json_error_message(
                        parsed.code));

            return server_status::authentication_start_failed;
        }

        if (!state.valid ||
            !state.root_started ||
            !state.root_completed ||
            !state.stack.empty()) {

            detail =
                state.detail.empty()
                    ? "Invalid Microsoft Entra cache structure"
                    : state.detail;

            return server_status::authentication_start_failed;
        }

        if (state.version != 1) {
            detail =
                "Unsupported Microsoft Entra cache version; expected version 1";

            return server_status::authentication_start_failed;
        }

        if (state.tenant_id !=
            configuration.tenant_id) {

            detail =
                "Microsoft Entra cache tenant_id does not match server.json";

            return server_status::authentication_start_failed;
        }

        if (state.audience !=
            configuration.audience) {

            detail =
                "Microsoft Entra cache audience does not match server.json";

            return server_status::authentication_start_failed;
        }

        for (std::size_t left = 0;
             left < state.keys.size();
             ++left) {

            if (state.keys[left].kty != "RSA") {
                detail =
                    "Microsoft Entra cache contains a non-RSA signing key";

                return server_status::authentication_start_failed;
            }

            if (!state.keys[left].alg.empty() &&
                state.keys[left].alg != "RS256") {

                detail =
                    "Microsoft Entra cache contains an unsupported signing algorithm";

                return server_status::authentication_start_failed;
            }

            for (std::size_t right = left + 1;
                 right < state.keys.size();
                 ++right) {

                if (state.keys[left].kid ==
                    state.keys[right].kid) {

                    detail =
                        "Microsoft Entra cache contains duplicate kid values";

                    return server_status::authentication_start_failed;
                }
            }
        }

        tenant_id_value =
            configuration.tenant_id;

        audience_value =
            configuration.audience;

        issuer_value =
            std::move(state.issuer);

        jwks_uri_value =
            std::move(state.jwks_uri);

        keys =
            std::move(state.keys);

        initialized = true;
        return server_status::success;
    }
    catch (...) {
        stop();
        detail =
            "Microsoft Entra provider initialization failed";

        return server_status::authentication_start_failed;
    }
}

void microsoft_entra_authentication::stop() noexcept {
    tenant_id_value.clear();
    audience_value.clear();
    issuer_value.clear();
    jwks_uri_value.clear();
    keys.clear();
    initialized = false;
}

bool microsoft_entra_authentication::ready() const noexcept {
    return initialized;
}

std::string_view microsoft_entra_authentication::issuer() const noexcept {
    return issuer_value;
}

std::string_view microsoft_entra_authentication::jwks_uri() const noexcept {
    return jwks_uri_value;
}

const std::vector<microsoft_entra_signing_key>&
microsoft_entra_authentication::signing_keys() const noexcept {

    return keys;
}

}
