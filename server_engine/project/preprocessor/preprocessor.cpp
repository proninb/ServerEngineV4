#include "preprocessor.hpp"

#include "../string/string_table.hpp"

#include <limits>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t next_capacity(
    std::size_t required) noexcept {

    if (required == 0) {
        return 0;
    }

    constexpr std::size_t minimum_capacity = 8;

    const auto maximum =
        (std::numeric_limits<std::size_t>::max)();

    if (required > maximum / 2) {
        return 0;
    }

    const auto minimum =
        required * 2;

    std::size_t capacity =
        minimum_capacity;

    while (capacity < minimum) {
        if (capacity > maximum / 2) {
            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

}

std::uint32_t preprocessor::hash_name(
    string_id name) noexcept {

    auto value =
        name.value();

    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;

    return value;
}

std::size_t preprocessor::position(
    string_id name) const noexcept {

    if (definitions.empty()) {
        return 0;
    }

    const auto mask =
        definitions.size() - 1;

    auto current =
        static_cast<std::size_t>(
            hash_name(name)) &
        mask;

    while (definitions[current].name &&
           definitions[current].name != name) {

        current =
            (current + 1) &
            mask;
    }

    return current;
}

void preprocessor::insert_slot(
    std::vector<define_slot>& target,
    define_slot slot) noexcept {

    const auto mask =
        target.size() - 1;

    auto current =
        static_cast<std::size_t>(
            hash_name(slot.name)) &
        mask;

    while (target[current].name) {
        current =
            (current + 1) &
            mask;
    }

    target[current] =
        slot;
}

server_status preprocessor::ensure_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            definition_count) {

        return server_status::io_error;
    }

    const auto required =
        definition_count + additional;

    if (!definitions.empty() &&
        required <=
            definitions.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_capacity(required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<define_slot> candidate(
            capacity);

        for (const auto& slot : definitions) {
            if (slot.name) {
                insert_slot(
                    candidate,
                    slot);
            }
        }

        definitions =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status preprocessor::define_impl(
    string_id name,
    string_id replacement) noexcept {

    if (!strings.contains(name) ||
        (replacement &&
         !strings.contains(replacement))) {

        return server_status::
            project_configuration_invalid;
    }

    if (!definitions.empty()) {
        const auto existing_position =
            position(name);

        const auto& existing =
            definitions[existing_position];

        if (existing.name) {
            return existing.replacement ==
                    replacement
                ? server_status::success
                : server_status::
                    project_configuration_invalid;
        }
    }

    const auto prepared =
        ensure_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto target =
        position(name);

    definitions[target] = {
        name,
        replacement,
    };

    ++definition_count;

    return server_status::success;
}

server_status preprocessor::define(
    string_id name) noexcept {

    return define_impl(
        name,
        {});
}

server_status preprocessor::define(
    string_id name,
    string_id replacement) noexcept {

    if (!replacement) {
        return server_status::
            project_configuration_invalid;
    }

    return define_impl(
        name,
        replacement);
}

server_status preprocessor::undefine(
    string_id name) noexcept {

    if (!strings.contains(name)) {
        return server_status::
            project_configuration_invalid;
    }

    if (definitions.empty()) {
        return server_status::success;
    }

    const auto found =
        position(name);

    if (!definitions[found].name) {
        return server_status::success;
    }

    definitions[found] = {};
    --definition_count;

    const auto mask =
        definitions.size() - 1;

    auto current =
        (found + 1) &
        mask;

    while (definitions[current].name) {
        const auto slot =
            definitions[current];

        definitions[current] = {};

        insert_slot(
            definitions,
            slot);

        current =
            (current + 1) &
            mask;
    }

    return server_status::success;
}

bool preprocessor::defined(
    string_id name) const noexcept {

    if (!strings.contains(name) ||
        definitions.empty()) {

        return false;
    }

    return definitions[
        position(name)]
        .name == name;
}

preprocessor::transition_kind
preprocessor::transition(
    string_id name,
    string_id& next) const noexcept {

    next = {};

    if (definitions.empty()) {
        return transition_kind::identifier;
    }

    const auto& slot =
        definitions[position(name)];

    if (slot.name != name) {
        return transition_kind::identifier;
    }

    if (!slot.replacement) {
        return transition_kind::empty;
    }

    next =
        slot.replacement;

    return transition_kind::next;
}

server_status preprocessor::expand(
    string_id name,
    preprocessor_expansion& output) const noexcept {

    output = {};

    if (!strings.contains(name)) {
        return server_status::
            project_configuration_invalid;
    }

    const auto terminal =
        [&](transition_kind kind,
            string_id identifier) noexcept {
            output.kind =
                kind == transition_kind::empty
                ? preprocessor_expansion_kind::empty
                : preprocessor_expansion_kind::identifier;

            output.identifier =
                output.kind ==
                    preprocessor_expansion_kind::identifier
                ? identifier
                : string_id{};
        };

    string_id tortoise =
        name;

    string_id hare =
        name;

    for (;;) {
        string_id next;

        auto state =
            transition(
                tortoise,
                next);

        if (state != transition_kind::next) {
            terminal(
                state,
                tortoise);
            return server_status::success;
        }

        tortoise =
            next;

        state =
            transition(
                hare,
                next);

        if (state != transition_kind::next) {
            terminal(
                state,
                hare);
            return server_status::success;
        }

        hare =
            next;

        state =
            transition(
                hare,
                next);

        if (state != transition_kind::next) {
            terminal(
                state,
                hare);
            return server_status::success;
        }

        hare =
            next;

        if (tortoise == hare) {
            break;
        }
    }

    auto entry =
        name;

    auto meeting =
        tortoise;

    while (entry != meeting) {
        string_id next_entry;
        string_id next_meeting;

        if (transition(
                entry,
                next_entry) !=
                transition_kind::next ||
            transition(
                meeting,
                next_meeting) !=
                transition_kind::next) {

            return server_status::
                project_configuration_invalid;
        }

        entry =
            next_entry;

        meeting =
            next_meeting;
    }

    output.kind =
        preprocessor_expansion_kind::identifier;

    output.identifier =
        entry;

    return server_status::success;
}

}
