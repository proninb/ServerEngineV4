#pragma once

#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>

namespace cw::server::detail {

#ifdef CW_HEADER_STATE_LAZY_AB
// Benchmark/test-only switch. Set before parsing starts, never by workers.
inline bool header_record_state_eager = false;
#endif

// Empty reads allocate nothing. Mutations retain the standard container's
// lookup, iteration, rehash and erase behavior. A singular empty iterator must
// not survive the first mutation (the Header caller never does this).
template<class Container>
class header_record_index final {
    std::optional<Container> data;

    Container& writable() {
        if (!data) data.emplace();
        return *data;
    }

public:
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using key_type = typename Container::key_type;
    using mapped_type = typename Container::mapped_type;

    header_record_index() {
#ifdef CW_HEADER_STATE_LAZY_AB
        if (header_record_state_eager) data.emplace();
#endif
    }

    [[nodiscard]] bool materialized() const noexcept { return data.has_value(); }
    [[nodiscard]] bool empty() const noexcept { return !data || data->empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return data ? data->size() : 0; }
    iterator begin() noexcept { return data ? data->begin() : iterator{}; }
    iterator end() noexcept { return data ? data->end() : iterator{}; }
    const_iterator begin() const noexcept { return data ? data->begin() : const_iterator{}; }
    const_iterator end() const noexcept { return data ? data->end() : const_iterator{}; }
    iterator find(const key_type& key) { return data ? data->find(key) : iterator{}; }
    const_iterator find(const key_type& key) const { return data ? data->find(key) : const_iterator{}; }
    std::pair<iterator, iterator> equal_range(const key_type& key) {
        return data ? data->equal_range(key) : std::pair<iterator, iterator>{};
    }
    void reserve(std::size_t count) { writable().reserve(count); }
    template<class Key> mapped_type& operator[](Key&& key) {
        return writable()[std::forward<Key>(key)];
    }
    template<class... Args> auto emplace(Args&&... args) {
        return writable().emplace(std::forward<Args>(args)...);
    }
    template<class... Args> auto try_emplace(Args&&... args) {
        return writable().try_emplace(std::forward<Args>(args)...);
    }
    template<class... Args> auto insert_or_assign(Args&&... args) {
        return writable().insert_or_assign(std::forward<Args>(args)...);
    }
    iterator erase(iterator position) {
        assert(data.has_value());
        return data->erase(position);
    }
};

} // namespace cw::server::detail
