#include "runtime_ic_snapshot.hpp"
#include "runtime_ic_codec.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace cw::server {
namespace {

struct pending_record final {
    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;
    std::uint16_t member_count = 0;
    std::uint32_t member_begin = 0;
    runtime_ic_scalar_value value;
};

struct snapshot_build_state final {
    const compiled_project_view& project;
    const runtime_binding_index& bindings;
    std::span<const std::byte> runtime;

    std::vector<std::string_view> components;
    std::vector<pending_record> records;
    std::vector<std::string_view> object_path;
    std::vector<std::string_view> reverse_object_path;
    std::vector<std::string_view> member_path;

    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;

    runtime_ic_snapshot_stats stats;
};

[[nodiscard]] bool add_offset(
    runtime_offset left,
    runtime_offset right,
    runtime_offset& output) noexcept {

    if (left >
        (std::numeric_limits<runtime_offset>::max)() -
            right) {

        return false;
    }

    output = left + right;
    return true;
}

[[nodiscard]] runtime_ic_snapshot_result
make_object_path(
    snapshot_build_state& state,
    object_handle object) noexcept {

    state.object_path.clear();
    state.reverse_object_path.clear();

    const auto root =
        state.project.identity_root();

    auto identity =
        state.project.identity(
            object);

    if (!root ||
        !identity) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    for (std::size_t depth = 0;
         depth <= state.project.identity_count();
         ++depth) {

        if (identity == root) {
            break;
        }

        const auto name =
            state.project.identity_name(
                identity);

        const auto text =
            state.project.string(
                name);

        if (!name ||
            text.empty()) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        try {
            state.reverse_object_path.push_back(
                text);
        }
        catch (...) {
            return runtime_ic_snapshot_result::failed;
        }

        identity =
            state.project.identity_parent(
                identity);

        if (!identity) {
            return runtime_ic_snapshot_result::
                invalid_runtime;
        }
    }

    if (identity != root) {
        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    if (state.reverse_object_path.empty()) {
        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    if (state.reverse_object_path.size() >
        (std::numeric_limits<std::uint16_t>::max)()) {

        return runtime_ic_snapshot_result::overflow;
    }

    try {
        state.object_path.assign(
            state.reverse_object_path.rbegin(),
            state.reverse_object_path.rend());
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    return runtime_ic_snapshot_result::success;
}

[[nodiscard]] runtime_ic_snapshot_result
emit_scalar(
    snapshot_build_state& state,
    runtime_offset offset,
    intrinsic_type type,
    bool inherited_path) noexcept {

    std::uint8_t size = 0;

    if (type == intrinsic_type::none) {
        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    if (type == intrinsic_type::void_type ||
        type == intrinsic_type::nullptr_type ||
        !state.bindings.intrinsic_size(
            type,
            size) ||
        size == 0 ||
        size >
            runtime_ic_scalar_value{}.
                bytes.size()) {

        ++state.stats.skipped_structural;
        return runtime_ic_snapshot_result::success;
    }

    if (offset >
            static_cast<runtime_offset>(
                state.runtime.size()) ||
        size >
            static_cast<runtime_offset>(
                state.runtime.size()) -
                offset) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    if (inherited_path) {
        runtime_ic_scalar_target target;

        const auto resolved =
            resolve_runtime_ic_scalar(
                state.project,
                state.bindings,
                static_cast<std::uint64_t>(
                    state.runtime.size()),
                {
                    state.object_path,
                    state.member_path,
                },
                target);

        if (resolved != runtime_ic_result::success ||
            target.offset != offset ||
            target.type != type ||
            target.size != size) {

            ++state.stats.skipped_unaddressable;
            return runtime_ic_snapshot_result::success;
        }
    }

    if (state.member_path.size() >
            (std::numeric_limits<std::uint16_t>::max)() ||
        state.components.size() >
            (std::numeric_limits<std::uint32_t>::max)() ||
        state.records.size() >=
            (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_snapshot_result::overflow;
    }

    const auto member_begin =
        static_cast<std::uint32_t>(
            state.components.size());

    try {
        state.components.insert(
            state.components.end(),
            state.member_path.begin(),
            state.member_path.end());
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    pending_record record;
    record.object_begin =
        state.object_begin;
    record.object_count =
        state.object_count;
    record.member_begin =
        member_begin;
    record.member_count =
        static_cast<std::uint16_t>(
            state.member_path.size());

    record.value.type = type;
    record.value.size = size;

    std::memcpy(
        record.value.bytes.data(),
        state.runtime.data() +
            static_cast<std::size_t>(
                offset),
        size);

    try {
        state.records.push_back(
            record);
    }
    catch (...) {
        state.components.resize(
            member_begin);
        return runtime_ic_snapshot_result::failed;
    }

    ++state.stats.scalars;
    return runtime_ic_snapshot_result::success;
}

[[nodiscard]] runtime_ic_snapshot_result
walk_type(
    snapshot_build_state& state,
    type_ref type,
    runtime_offset offset,
    bool inherited_path,
    std::size_t depth) noexcept;

[[nodiscard]] runtime_ic_snapshot_result
walk_record(
    snapshot_build_state& state,
    type_handle type,
    runtime_offset offset,
    bool inherited_path,
    std::size_t depth) noexcept {

    if (!type ||
        depth >
            state.project.type_count() +
            state.project.derived_type_count() + 1) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    type_entry entry;

    if (!state.project.type(
            type,
            entry) ||
        !entry.defined()) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    for (std::uint32_t local = 0;
         local < entry.bases.count;
         ++local) {

        const auto global =
            static_cast<std::size_t>(
                entry.bases.begin) +
            local;

        base_record base;

        if (!state.project.base_at(
                global,
                base)) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        if (base.virtual_base()) {
            ++state.stats.skipped_structural;
            continue;
        }

        record_offset base_offset = 0;

        if (!state.bindings.base_offset(
                global,
                base_offset)) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        runtime_offset location = 0;

        if (!add_offset(
                offset,
                static_cast<runtime_offset>(
                    base_offset),
                location)) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        const auto result =
            walk_record(
                state,
                base.type,
                location,
                true,
                depth + 1);

        if (result !=
            runtime_ic_snapshot_result::success) {

            return result;
        }
    }

    for (std::uint32_t local = 0;
         local < entry.members.count;
         ++local) {

        const auto global =
            static_cast<std::size_t>(
                entry.members.begin) +
            local;

        member_record member;

        if (!state.project.member(
                type,
                local,
                member)) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        const auto name =
            state.project.string(
                member.name);

        if (name.empty()) {
            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        record_offset member_offset = 0;

        if (!state.bindings.member_offset(
                global,
                member_offset)) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        runtime_offset location = 0;

        if (!add_offset(
                offset,
                static_cast<runtime_offset>(
                    member_offset),
                location)) {

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        try {
            state.member_path.push_back(
                name);
        }
        catch (...) {
            return runtime_ic_snapshot_result::failed;
        }

        const auto result =
            walk_type(
                state,
                member.type,
                location,
                inherited_path,
                depth + 1);

        state.member_path.pop_back();

        if (result !=
            runtime_ic_snapshot_result::success) {

            return result;
        }
    }

    return runtime_ic_snapshot_result::success;
}

runtime_ic_snapshot_result walk_type(
    snapshot_build_state& state,
    type_ref type,
    runtime_offset offset,
    bool inherited_path,
    std::size_t depth) noexcept {

    if (!type ||
        depth >
            state.project.type_count() +
            state.project.derived_type_count() + 1) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    for (std::size_t derived_depth = 0;
         derived_depth <=
            state.project.derived_type_count();
         ++derived_depth) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return emit_scalar(
                state,
                offset,
                static_cast<intrinsic_type>(
                    type.payload()),
                inherited_path);

        case type_ref_kind::named: {
            type_handle named;

            if (!state.project.named(
                    type,
                    named)) {

                return runtime_ic_snapshot_result::
                    invalid_runtime;
            }

            return walk_record(
                state,
                named,
                offset,
                inherited_path,
                depth + 1);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!state.project.derived(
                    type,
                    derived)) {

                return runtime_ic_snapshot_result::
                    invalid_runtime;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                type = derived.child;
                continue;

            case derived_type_kind::pointer:
            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
            case derived_type_kind::bounded_array:
            case derived_type_kind::unbounded_array:
                ++state.stats.skipped_structural;
                return runtime_ic_snapshot_result::success;
            }

            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        case type_ref_kind::invalid:
            return runtime_ic_snapshot_result::
                invalid_runtime;
        }
    }

    return runtime_ic_snapshot_result::
        invalid_runtime;
}

[[nodiscard]] runtime_ic_snapshot_result
walk_object(
    snapshot_build_state& state,
    object_handle object) noexcept {

    object_entry entry;

    if (!state.project.object(
            object,
            entry)) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    runtime_offset offset = 0;

    if (!state.bindings.object_offset(
            object,
            offset) ||
        offset >=
            static_cast<runtime_offset>(
                state.runtime.size())) {

        return runtime_ic_snapshot_result::
            invalid_runtime;
    }

    const auto path_result =
        make_object_path(
            state,
            object);

    if (path_result !=
        runtime_ic_snapshot_result::success) {

        return path_result;
    }

    const auto object_component_begin =
        state.components.size();

    if (object_component_begin >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_snapshot_result::overflow;
    }

    try {
        state.components.insert(
            state.components.end(),
            state.object_path.begin(),
            state.object_path.end());
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    state.object_begin =
        static_cast<std::uint32_t>(
            object_component_begin);

    state.object_count =
        static_cast<std::uint16_t>(
            state.object_path.size());

    const auto record_begin =
        state.records.size();

    const auto result =
        walk_type(
            state,
            entry.type,
            offset,
            false,
            0);

    if (result !=
        runtime_ic_snapshot_result::success) {

        return result;
    }

    if (state.records.size() ==
        record_begin) {

        state.components.resize(
            object_component_begin);
    }

    ++state.stats.objects;
    return runtime_ic_snapshot_result::success;
}

}

void runtime_ic_snapshot::reset() noexcept {
    components.clear();
    values.clear();
    statistics = {};
}

runtime_ic_snapshot_result
snapshot_runtime_ic_project(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    runtime_ic_snapshot& output) noexcept {

    output.reset();

    if (!project.valid() ||
        runtime.empty()) {

        return runtime_ic_snapshot_result::
            invalid_input;
    }

    snapshot_build_state state{
        project,
        bindings,
        runtime,
    };

    try {
        state.components.reserve(
            project.object_count() * 2 +
            project.member_count());

        state.records.reserve(
            project.object_count() +
            project.member_count());

        state.object_path.reserve(8);
        state.reverse_object_path.reserve(8);
        state.member_path.reserve(8);
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    for (std::size_t index = 0;
         index < project.object_count();
         ++index) {

        const auto object =
            project.object_at(
                index);

        if (!object) {
            return runtime_ic_snapshot_result::
                invalid_runtime;
        }

        const auto result =
            walk_object(
                state,
                object);

        if (result !=
            runtime_ic_snapshot_result::success) {

            return result;
        }
    }

    try {
        output.components =
            std::move(
                state.components);

        output.values.reserve(
            state.records.size());

        for (const auto& record :
             state.records) {

            const auto object_end =
                static_cast<std::uint64_t>(
                    record.object_begin) +
                record.object_count;

            const auto member_end =
                static_cast<std::uint64_t>(
                    record.member_begin) +
                record.member_count;

            if (object_end >
                    output.components.size() ||
                member_end >
                    output.components.size()) {

                output.reset();
                return runtime_ic_snapshot_result::
                    invalid_runtime;
            }

            output.values.push_back(
                {
                    {
                        {
                            output.components.data() +
                                record.object_begin,
                            record.object_count,
                        },
                        {
                            output.components.data() +
                                record.member_begin,
                            record.member_count,
                        },
                    },
                    record.value,
                });
        }
    }
    catch (...) {
        output.reset();
        return runtime_ic_snapshot_result::failed;
    }

    output.statistics =
        state.stats;

    return runtime_ic_snapshot_result::success;
}


namespace {

struct native_snapshot_state final {
    const compiled_project_view& project;
    const runtime_binding_index& bindings;
    std::span<const std::byte> runtime;

    std::vector<string_id> components;
    std::vector<runtime_ic_native_record_source> records;
    std::vector<string_id> object_path;
    std::vector<string_id> reverse_object_path;
    std::vector<string_id> member_path;

    std::vector<std::string_view> object_text;
    std::vector<std::string_view> reverse_object_text;
    std::vector<std::string_view> member_text;

    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;
    runtime_ic_snapshot_stats stats;
};

[[nodiscard]] runtime_ic_snapshot_result
native_object_path(
    native_snapshot_state& state,
    object_handle object) noexcept {

    state.object_path.clear();
    state.reverse_object_path.clear();
    state.object_text.clear();
    state.reverse_object_text.clear();

    const auto root = state.project.identity_root();
    auto identity = state.project.identity(object);

    if (!root || !identity) {
        return runtime_ic_snapshot_result::invalid_runtime;
    }

    for (std::size_t depth = 0;
         depth <= state.project.identity_count();
         ++depth) {

        if (identity == root) {
            break;
        }

        const auto name = state.project.identity_name(identity);
        const auto text = state.project.string(name);

        if (!name || text.empty()) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        try {
            state.reverse_object_path.push_back(name);
            state.reverse_object_text.push_back(text);
        }
        catch (...) {
            return runtime_ic_snapshot_result::failed;
        }

        identity = state.project.identity_parent(identity);

        if (!identity) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }
    }

    if (identity != root ||
        state.reverse_object_path.empty()) {

        return runtime_ic_snapshot_result::invalid_runtime;
    }

    if (state.reverse_object_path.size() >
        (std::numeric_limits<std::uint16_t>::max)()) {

        return runtime_ic_snapshot_result::overflow;
    }

    try {
        state.object_path.assign(
            state.reverse_object_path.rbegin(),
            state.reverse_object_path.rend());

        state.object_text.assign(
            state.reverse_object_text.rbegin(),
            state.reverse_object_text.rend());
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    return runtime_ic_snapshot_result::success;
}

[[nodiscard]] runtime_ic_snapshot_result
native_emit_scalar(
    native_snapshot_state& state,
    runtime_offset offset,
    intrinsic_type type,
    bool inherited_path) noexcept {

    std::uint8_t size = 0;

    if (type == intrinsic_type::none) {
        return runtime_ic_snapshot_result::invalid_runtime;
    }

    if (type == intrinsic_type::void_type ||
        type == intrinsic_type::nullptr_type ||
        !state.bindings.intrinsic_size(type, size) ||
        size == 0 ||
        size > runtime_ic_scalar_value{}.bytes.size()) {

        ++state.stats.skipped_structural;
        return runtime_ic_snapshot_result::success;
    }

    if (offset > static_cast<runtime_offset>(state.runtime.size()) ||
        size > static_cast<runtime_offset>(state.runtime.size()) - offset) {

        return runtime_ic_snapshot_result::invalid_runtime;
    }

    if (inherited_path) {
        runtime_ic_scalar_target target;

        const auto resolved =
            resolve_runtime_ic_scalar(
                state.project,
                state.bindings,
                static_cast<std::uint64_t>(state.runtime.size()),
                {state.object_text, state.member_text},
                target);

        if (resolved != runtime_ic_result::success ||
            target.offset != offset ||
            target.type != type ||
            target.size != size) {

            ++state.stats.skipped_unaddressable;
            return runtime_ic_snapshot_result::success;
        }
    }

    if (state.member_path.size() >
            (std::numeric_limits<std::uint16_t>::max)() ||
        state.components.size() >
            (std::numeric_limits<std::uint32_t>::max)() ||
        state.records.size() >=
            (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_snapshot_result::overflow;
    }

    const auto member_begin =
        static_cast<std::uint32_t>(state.components.size());

    try {
        state.components.insert(
            state.components.end(),
            state.member_path.begin(),
            state.member_path.end());
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    runtime_ic_native_record_source record;
    record.object_begin = state.object_begin;
    record.object_count = state.object_count;
    record.member_begin = member_begin;
    record.member_count =
        static_cast<std::uint16_t>(state.member_path.size());

    record.value.type = type;
    record.value.size = size;

    std::memcpy(
        record.value.bytes.data(),
        state.runtime.data() + static_cast<std::size_t>(offset),
        size);

    try {
        state.records.push_back(record);
    }
    catch (...) {
        state.components.resize(member_begin);
        return runtime_ic_snapshot_result::failed;
    }

    ++state.stats.scalars;
    return runtime_ic_snapshot_result::success;
}

[[nodiscard]] runtime_ic_snapshot_result
native_walk_type(
    native_snapshot_state& state,
    type_ref type,
    runtime_offset offset,
    bool inherited_path,
    std::size_t depth) noexcept;

[[nodiscard]] runtime_ic_snapshot_result
native_walk_record(
    native_snapshot_state& state,
    type_handle type,
    runtime_offset offset,
    bool inherited_path,
    std::size_t depth) noexcept {

    if (!type ||
        depth > state.project.type_count() +
            state.project.derived_type_count() + 1) {

        return runtime_ic_snapshot_result::invalid_runtime;
    }

    type_entry entry;

    if (!state.project.type(type, entry) ||
        !entry.defined()) {

        return runtime_ic_snapshot_result::invalid_runtime;
    }

    for (std::uint32_t local = 0;
         local < entry.bases.count;
         ++local) {

        const auto global =
            static_cast<std::size_t>(entry.bases.begin) + local;

        base_record base;

        if (!state.project.base_at(global, base)) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        if (base.virtual_base()) {
            ++state.stats.skipped_structural;
            continue;
        }

        record_offset base_offset = 0;

        if (!state.bindings.base_offset(global, base_offset)) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        runtime_offset location = 0;

        if (!add_offset(
                offset,
                static_cast<runtime_offset>(base_offset),
                location)) {

            return runtime_ic_snapshot_result::invalid_runtime;
        }

        const auto result =
            native_walk_record(
                state,
                base.type,
                location,
                true,
                depth + 1);

        if (result != runtime_ic_snapshot_result::success) {
            return result;
        }
    }

    for (std::uint32_t local = 0;
         local < entry.members.count;
         ++local) {

        const auto global =
            static_cast<std::size_t>(entry.members.begin) + local;

        member_record member;

        if (!state.project.member(type, local, member)) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        const auto text =
            state.project.string(member.name);

        if (!member.name || text.empty()) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        record_offset member_offset = 0;

        if (!state.bindings.member_offset(global, member_offset)) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        runtime_offset location = 0;

        if (!add_offset(
                offset,
                static_cast<runtime_offset>(member_offset),
                location)) {

            return runtime_ic_snapshot_result::invalid_runtime;
        }

        try {
            state.member_path.push_back(member.name);
            state.member_text.push_back(text);
        }
        catch (...) {
            if (state.member_path.size() >
                state.member_text.size()) {

                state.member_path.pop_back();
            }

            return runtime_ic_snapshot_result::failed;
        }

        const auto result =
            native_walk_type(
                state,
                member.type,
                location,
                inherited_path,
                depth + 1);

        state.member_path.pop_back();
        state.member_text.pop_back();

        if (result != runtime_ic_snapshot_result::success) {
            return result;
        }
    }

    return runtime_ic_snapshot_result::success;
}

runtime_ic_snapshot_result native_walk_type(
    native_snapshot_state& state,
    type_ref type,
    runtime_offset offset,
    bool inherited_path,
    std::size_t depth) noexcept {

    if (!type ||
        depth > state.project.type_count() +
            state.project.derived_type_count() + 1) {

        return runtime_ic_snapshot_result::invalid_runtime;
    }

    for (std::size_t derived_depth = 0;
         derived_depth <= state.project.derived_type_count();
         ++derived_depth) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return native_emit_scalar(
                state,
                offset,
                static_cast<intrinsic_type>(type.payload()),
                inherited_path);

        case type_ref_kind::named: {
            type_handle named;

            if (!state.project.named(
                    type,
                    named)) {

                return runtime_ic_snapshot_result::invalid_runtime;
            }

            return native_walk_record(
                state,
                named,
                offset,
                inherited_path,
                depth + 1);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!state.project.derived(type, derived)) {
                return runtime_ic_snapshot_result::invalid_runtime;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                type = derived.child;
                continue;

            case derived_type_kind::pointer:
            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
            case derived_type_kind::bounded_array:
            case derived_type_kind::unbounded_array:
                ++state.stats.skipped_structural;
                return runtime_ic_snapshot_result::success;
            }

            return runtime_ic_snapshot_result::invalid_runtime;
        }

        case type_ref_kind::invalid:
            return runtime_ic_snapshot_result::invalid_runtime;
        }
    }

    return runtime_ic_snapshot_result::invalid_runtime;
}

[[nodiscard]] runtime_ic_snapshot_result
native_walk_object(
    native_snapshot_state& state,
    object_handle object) noexcept {

    object_entry entry;

    if (!state.project.object(object, entry)) {
        return runtime_ic_snapshot_result::invalid_runtime;
    }

    runtime_offset offset = 0;

    if (!state.bindings.object_offset(object, offset) ||
        offset >= static_cast<runtime_offset>(state.runtime.size())) {

        return runtime_ic_snapshot_result::invalid_runtime;
    }

    const auto path_result =
        native_object_path(state, object);

    if (path_result != runtime_ic_snapshot_result::success) {
        return path_result;
    }

    const auto object_begin = state.components.size();

    if (object_begin >
        (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_snapshot_result::overflow;
    }

    try {
        state.components.insert(
            state.components.end(),
            state.object_path.begin(),
            state.object_path.end());
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    state.object_begin =
        static_cast<std::uint32_t>(object_begin);

    state.object_count =
        static_cast<std::uint16_t>(state.object_path.size());

    const auto record_begin = state.records.size();

    const auto result =
        native_walk_type(
            state,
            entry.type,
            offset,
            false,
            0);

    if (result != runtime_ic_snapshot_result::success) {
        return result;
    }

    if (state.records.size() == record_begin) {
        state.components.resize(object_begin);
    }

    ++state.stats.objects;
    return runtime_ic_snapshot_result::success;
}

}

runtime_ic_snapshot_result
snapshot_runtime_ic_project_native(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    runtime_ic_native_snapshot& output) noexcept {

    output.reset();

    if (!project.valid() || runtime.empty()) {
        return runtime_ic_snapshot_result::invalid_input;
    }

    native_snapshot_state state{
        project,
        bindings,
        runtime,
    };

    try {
        state.components.reserve(
            project.object_count() * 2 +
            project.member_count());

        state.records.reserve(
            project.object_count() +
            project.member_count());

        state.object_path.reserve(8);
        state.reverse_object_path.reserve(8);
        state.member_path.reserve(8);

        state.object_text.reserve(8);
        state.reverse_object_text.reserve(8);
        state.member_text.reserve(8);
    }
    catch (...) {
        return runtime_ic_snapshot_result::failed;
    }

    for (std::size_t index = 0;
         index < project.object_count();
         ++index) {

        const auto object = project.object_at(index);

        if (!object) {
            return runtime_ic_snapshot_result::invalid_runtime;
        }

        const auto result =
            native_walk_object(state, object);

        if (result != runtime_ic_snapshot_result::success) {
            return result;
        }
    }

    output.components = std::move(state.components);
    output.values = std::move(state.records);
    output.statistics = state.stats;

    return runtime_ic_snapshot_result::success;
}


runtime_ic_snapshot_result
snapshot_runtime_ic_binary(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    std::vector<std::byte>& output,
    runtime_ic_snapshot_stats* stats) noexcept {

    output.clear();

    if (stats != nullptr) {
        *stats = {};
    }

    runtime_ic_native_snapshot snapshot;

    const auto snapped =
        snapshot_runtime_ic_project_native(
            project,
            bindings,
            runtime,
            snapshot);

    if (snapped !=
        runtime_ic_snapshot_result::success) {

        return snapped;
    }

    runtime_ic_binary_plan plan;

    const auto prepared =
        prepare_runtime_ic_binary_native(
            project,
            snapshot.source(),
            plan);

    switch (prepared) {
    case runtime_ic_codec_result::success:
        break;

    case runtime_ic_codec_result::overflow:
        return runtime_ic_snapshot_result::overflow;

    case runtime_ic_codec_result::invalid_input:
    case runtime_ic_codec_result::invalid_image:
    case runtime_ic_codec_result::unsupported_value:
        return runtime_ic_snapshot_result::invalid_runtime;

    case runtime_ic_codec_result::failed:
        return runtime_ic_snapshot_result::failed;
    }

    try {
        output.assign(
            plan.size(),
            std::byte{0});
    }
    catch (...) {
        output.clear();
        return runtime_ic_snapshot_result::failed;
    }

    const auto encoded =
        encode_runtime_ic_binary_native(
            project,
            snapshot.source(),
            plan,
            output);

    switch (encoded) {
    case runtime_ic_codec_result::success:
        break;

    case runtime_ic_codec_result::overflow:
        output.clear();
        return runtime_ic_snapshot_result::overflow;

    case runtime_ic_codec_result::invalid_input:
    case runtime_ic_codec_result::invalid_image:
    case runtime_ic_codec_result::unsupported_value:
        output.clear();
        return runtime_ic_snapshot_result::invalid_runtime;

    case runtime_ic_codec_result::failed:
        output.clear();
        return runtime_ic_snapshot_result::failed;
    }

    if (stats != nullptr) {
        *stats = snapshot.stats();
    }

    return runtime_ic_snapshot_result::success;
}

}
