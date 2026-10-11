#pragma once
// V3-23: record-local direct string_id => member index, one producer-local scratch.
// No tree/hash lookup on the hot path; no per-field node allocations.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace v323_index {
class direct_id_index final {
    struct slot final { std::uint32_t epoch=0; std::uint32_t index_plus_one=0; };
    std::vector<slot> slots_;
    std::uint32_t epoch_=0;
public:
    static constexpr std::size_t not_found=std::numeric_limits<std::size_t>::max();
    // Each record has an independent namespace. A 32-bit wrap clears stamps.
    void begin_record() noexcept {
        if (++epoch_==0) {
            for (auto& entry:slots_)entry.epoch=0;
            epoch_=1;
        }
    }
    [[nodiscard]] std::size_t find(std::uint32_t id) const noexcept {
        if (!id || id>=slots_.size())return not_found;
        const auto& value=slots_[id];
        return value.epoch==epoch_ && value.index_plus_one
            ? static_cast<std::size_t>(value.index_plus_one-1) : not_found;
    }
    [[nodiscard]] bool insert(std::uint32_t id,std::size_t index) {
        if (!id || index>=std::numeric_limits<std::uint32_t>::max())return false;
        // Grow only when needed. Very sparse enormous IDs are not expected from
        // string_table; fail closed instead of requesting potentially 32 GiB.
        constexpr std::size_t maximum_dense_id=32u*1024u*1024u;
        if (id>=maximum_dense_id)return false;
        if (id>=slots_.size()) {
            const auto needed=static_cast<std::size_t>(id)+1;
            std::size_t capacity=slots_.empty()?256:slots_.size();
            while(capacity<needed)capacity=std::min(capacity*2,maximum_dense_id);
            slots_.resize(capacity);
        }
        auto& value=slots_[id];
        if(value.epoch==epoch_ && value.index_plus_one)return false;
        value={epoch_,static_cast<std::uint32_t>(index+1)};
        return true;
    }
    [[nodiscard]] std::size_t capacity() const noexcept {return slots_.size();}
    // Test-only hook for UINT32 wrap regression; has no use in production.
#ifdef CW_V323_INDEX_TEST
    void test_set_epoch(std::uint32_t e) noexcept {epoch_=e;}
#endif
};
}
