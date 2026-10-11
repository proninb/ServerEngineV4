#pragma once
// V3-37R: count owned declarations using the scanner's existing DFS ranges.
// No extra owner index, no persistent array, and no allocation.
#include <cstddef>

namespace v337r_ranges {
template <class Syntax>
std::size_t next_after_foreign(const Syntax& syntax, std::size_t di,
                               std::size_t end) noexcept {
    const auto owner = syntax.declarations[di].owner;
    if (owner < syntax.records.size()) {
        const auto& nested = syntax.records[owner];
        const auto start = static_cast<std::size_t>(nested.declaration_first);
        const auto nested_end = start + nested.declaration_count;
        // A foreign row may be skipped only inside its actual subtree.
        if (start <= di && di < nested_end && nested_end <= end)
            return nested_end;
    }
    return di + 1;
}

template <class Syntax>
std::size_t owned_declaration_count(const Syntax& syntax,
                                    std::size_t ri) noexcept {
    const auto& rec = syntax.records[ri];
    const auto end = static_cast<std::size_t>(rec.declaration_first) +
                     rec.declaration_count;
    std::size_t count = 0;
    for (std::size_t di = rec.declaration_first; di < end;) {
        if (syntax.declarations[di].owner == ri) {
            ++count;
            ++di;
        } else {
            di = next_after_foreign(syntax, di, end);
        }
    }
    return count;
}
} // namespace v337r_ranges
