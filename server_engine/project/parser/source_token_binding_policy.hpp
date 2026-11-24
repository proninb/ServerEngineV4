#pragma once
namespace cw::server::detail {
#ifdef CW_SOURCE_TOKEN_BINDING_AB
inline bool source_token_binding_reuse = true;
#endif
inline constexpr bool source_token_binding_native =
#ifdef CW_SOURCE_TOKEN_BINDING_V2
    true;
#else
    false;
#endif
inline bool token_binding_enabled() noexcept {
#ifdef CW_SOURCE_TOKEN_BINDING_AB
    return source_token_binding_reuse;
#else
    return source_token_binding_native;
#endif
}
}
