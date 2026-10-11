#pragma once
#include <cstdint>
namespace v341_diag {
[[nodiscard]] inline constexpr bool fields(std::uint64_t sampled_types,
  std::uint64_t finished_fields,std::uint64_t appended,
  std::uint64_t registered,std::uint64_t lookups,
  std::uint64_t construction_writes,std::uint64_t references,
  std::uint64_t reference_max) noexcept {
  return sampled_types==finished_fields && appended==finished_fields &&
    registered==finished_fields && lookups>=finished_fields &&
    construction_writes<=finished_fields && references<=finished_fields &&
    references<=reference_max;
}
}
