#pragma once
#include <optional>
#include <utility>
#include <cassert>

namespace sl01 {
// A record-local adapter, not a new lookup/index implementation. Empty reads
// never construct the standard container; the first write constructs it in place.
// Iterators obtained while disengaged must not survive the first mutation.
template<class Container> class lazy_container {
    std::optional<Container> data;
    Container& writable() { if (!data) data.emplace(); return *data; }
public:
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using key_type = typename Container::key_type;
    using mapped_type = typename Container::mapped_type;

    bool materialized() const noexcept { return data.has_value(); }
    bool empty() const { return !data || data->empty(); }
    std::size_t size() const { return data ? data->size() : 0; }
    iterator begin() { return data ? data->begin() : iterator{}; }
    iterator end() { return data ? data->end() : iterator{}; }
    const_iterator begin() const { return data ? data->begin() : const_iterator{}; }
    const_iterator end() const { return data ? data->end() : const_iterator{}; }
    iterator find(const key_type& key) { return data ? data->find(key) : iterator{}; }
    const_iterator find(const key_type& key) const { return data ? data->find(key) : const_iterator{}; }
    std::pair<iterator, iterator> equal_range(const key_type& key) {
        return data ? data->equal_range(key) : std::pair<iterator, iterator>{};
    }
    template<class Key> mapped_type& operator[](Key&& key) {
        return writable()[std::forward<Key>(key)];
    }
    template<class... Args> auto emplace(Args&&... args) {
        return writable().emplace(std::forward<Args>(args)...);
    }
    template<class... Args> auto try_emplace(Args&&... args) {
        return writable().try_emplace(std::forward<Args>(args)...);
    }
    iterator erase(iterator position) {
        assert(data.has_value());
        return data->erase(position);
    }
};
}
