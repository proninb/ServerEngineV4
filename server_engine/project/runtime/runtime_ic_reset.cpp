#include "runtime_ic_reset.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace cw::server {
namespace {

struct reset_write final {
    runtime_offset offset = 0;
    std::uint8_t size = 0;
    std::array<std::byte, 16> bytes{};
};

struct reset_plan final {
    std::vector<reset_write> writes;
    std::uint64_t bytes = 0;
};


struct binary_reset_write final {
    runtime_offset offset = 0;
    std::uint32_t record = 0;
    std::uint8_t size = 0;
    std::uint8_t reserved[3]{};
};

static_assert(sizeof(binary_reset_write) == 16);

struct binary_reset_plan final {
    std::vector<binary_reset_write> writes;
    std::uint64_t bytes = 0;
};

enum class binary_member_search_result : std::uint8_t {
    none = 0,
    found,
    ambiguous,
    invalid,
};

struct binary_resolved_member final {
    runtime_offset offset = 0;
    type_ref type{};
    bool default_value = false;
};

struct binary_resolve_state final {
    runtime_offset offset = 0;
    type_ref type{};
    bool constant = false;
    bool default_value = false;
};

[[nodiscard]] bool reset_target_excluded(
    const runtime_ic_scalar_target& target,
    reset_ic_options options) noexcept {

    if (has_option(
            options,
            reset_ic_options::constants) &&
        target.constant()) {

        return true;
    }

    if (has_option(
            options,
            reset_ic_options::variables) &&
        !target.constant()) {

        return true;
    }

    return has_option(
               options,
               reset_ic_options::defaults) &&
        target.default_value();
}

[[nodiscard]] bool add_runtime_offset(
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

[[nodiscard]] bool strip_binary_cv(
    const compiled_project_view& project,
    type_ref& type,
    bool& constant) noexcept {

    for (std::size_t step = 0;
         step <= project.derived_type_count();
         ++step) {

        if (type.kind() !=
            type_ref_kind::derived) {

            return true;
        }

        derived_type_record derived;

        if (!project.derived(
                type,
                derived)) {

            return false;
        }

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
            constant = true;
            type = derived.child;
            continue;

        case derived_type_kind::volatile_qualified:
            type = derived.child;
            continue;

        case derived_type_kind::pointer:
        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference:
        case derived_type_kind::bounded_array:
        case derived_type_kind::unbounded_array:
            return true;
        }
    }

    return false;
}

[[nodiscard]] binary_member_search_result
find_binary_member_recursive(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    type_handle type,
    string_id name,
    std::size_t depth,
    binary_resolved_member& output) noexcept {

    output = {};

    if (!type ||
        depth > project.type_count()) {

        return binary_member_search_result::invalid;
    }

    type_entry entry;

    if (!project.type(
            type,
            entry) ||
        !entry.defined()) {

        return binary_member_search_result::invalid;
    }

    if (const auto member =
            project.find_member(
                type,
                name);
        member) {

        member_record record;
        construction_value construction;

        if (!project.member(
                type,
                member,
                record) ||
            !project.construction(
                type,
                member,
                construction)) {

            return binary_member_search_result::invalid;
        }

        const auto global =
            static_cast<std::size_t>(
                entry.members.begin) +
            member.value();

        record_offset member_offset = 0;

        if (!bindings.member_offset(
                global,
                member_offset)) {

            return binary_member_search_result::invalid;
        }

        output.offset =
            static_cast<runtime_offset>(
                member_offset);

        output.type = record.type;
        output.default_value =
            construction == construction_value{};

        return binary_member_search_result::found;
    }

    binary_member_search_result state =
        binary_member_search_result::none;

    binary_resolved_member selected;

    for (std::uint32_t local = 0;
         local < entry.bases.count;
         ++local) {

        const auto global =
            static_cast<std::size_t>(
                entry.bases.begin) +
            local;

        base_record base;
        record_offset base_offset = 0;

        if (!project.base_at(
                global,
                base) ||
            base.virtual_base() ||
            !bindings.base_offset(
                global,
                base_offset)) {

            return binary_member_search_result::invalid;
        }

        const auto base_handle =
            project.find_type(
                base.type);

        if (!base_handle) {
            return binary_member_search_result::invalid;
        }

        binary_resolved_member candidate;

        const auto found =
            find_binary_member_recursive(
                project,
                bindings,
                base_handle,
                name,
                depth + 1,
                candidate);

        if (found ==
                binary_member_search_result::invalid ||
            found ==
                binary_member_search_result::ambiguous) {

            return found;
        }

        if (found !=
            binary_member_search_result::found) {

            continue;
        }

        runtime_offset combined = 0;

        if (!add_runtime_offset(
                static_cast<runtime_offset>(
                    base_offset),
                candidate.offset,
                combined)) {

            return binary_member_search_result::invalid;
        }

        candidate.offset = combined;

        if (state ==
            binary_member_search_result::found) {

            return binary_member_search_result::ambiguous;
        }

        selected = candidate;
        state = binary_member_search_result::found;
    }

    if (state ==
        binary_member_search_result::found) {

        output = selected;
    }

    return state;
}

[[nodiscard]] runtime_ic_reset_result
translate_binary_strings(
    const compiled_project_view& project,
    const runtime_ic_binary_view& image,
    std::vector<string_id>& output) noexcept {

    output.clear();

    if (image.string_count() >=
        (std::numeric_limits<std::size_t>::max)()) {

        return runtime_ic_reset_result::failed;
    }

    try {
        output.resize(
            image.string_count() + 1);
    }
    catch (...) {
        return runtime_ic_reset_result::failed;
    }

    for (std::size_t local = 1;
         local <= image.string_count();
         ++local) {

        if (local >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return runtime_ic_reset_result::invalid_image;
        }

        const auto text =
            image.string(
                static_cast<std::uint32_t>(
                    local));

        if (text.empty()) {
            return runtime_ic_reset_result::invalid_image;
        }

        const auto current =
            project.find_string(
                text);

        if (!current) {
            return runtime_ic_reset_result::not_found;
        }

        output[local] = current;
    }

    return runtime_ic_reset_result::success;
}

[[nodiscard]] bool same_binary_components(
    const runtime_ic_binary_view& image,
    std::uint32_t left_begin,
    std::uint16_t left_count,
    std::uint32_t right_begin,
    std::uint16_t right_count) noexcept {

    if (left_count != right_count) {
        return false;
    }

    for (std::uint32_t index = 0;
         index < left_count;
         ++index) {

        if (image.component(
                left_begin + index) !=
            image.component(
                right_begin + index)) {

            return false;
        }
    }

    return true;
}

[[nodiscard]] std::size_t binary_common_prefix(
    const runtime_ic_binary_view& image,
    std::uint32_t left_begin,
    std::uint16_t left_count,
    std::uint32_t right_begin,
    std::uint16_t right_count) noexcept {

    const auto count =
        left_count < right_count
        ? static_cast<std::size_t>(
            left_count)
        : static_cast<std::size_t>(
            right_count);

    std::size_t index = 0;

    while (index < count &&
           image.component(
               left_begin +
               static_cast<std::uint32_t>(
                   index)) ==
           image.component(
               right_begin +
               static_cast<std::uint32_t>(
                   index))) {

        ++index;
    }

    return index;
}

[[nodiscard]] runtime_ic_reset_result
resolve_binary_object(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::uint64_t runtime_size,
    const runtime_ic_binary_view& image,
    std::span<const string_id> translation,
    std::uint32_t component_begin,
    std::uint16_t component_count,
    binary_resolve_state& output) noexcept {

    output = {};

    if (component_count == 0) {
        return runtime_ic_reset_result::invalid_image;
    }

    auto parent =
        project.identity_root();

    if (!parent) {
        return runtime_ic_reset_result::invalid_runtime;
    }

    object_handle object;

    for (std::uint32_t index = 0;
         index < component_count;
         ++index) {

        const auto local =
            image.component(
                component_begin +
                index);

        if (local == 0 ||
            local >= translation.size()) {

            return runtime_ic_reset_result::invalid_image;
        }

        const auto name =
            translation[local];

        if (!name) {
            return runtime_ic_reset_result::not_found;
        }

        const auto last =
            index + 1 ==
            component_count;

        const auto identity =
            project.find_identity(
                parent,
                name,
                last
                    ? identity_kind::object
                    : identity_kind::namespace_scope);

        if (!identity) {
            return runtime_ic_reset_result::not_found;
        }

        if (last) {
            object =
                project.find_object(
                    identity);

            if (!object) {
                return runtime_ic_reset_result::not_found;
            }
        } else {
            parent = identity;
        }
    }

    object_entry entry;
    runtime_offset offset = 0;

    if (!project.object(
            object,
            entry) ||
        !bindings.object_offset(
            object,
            offset) ||
        offset >= runtime_size) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    output.offset = offset;
    output.type = entry.type;
    output.constant = false;
    output.default_value =
        !entry.non_default_initializer();

    return runtime_ic_reset_result::success;
}

[[nodiscard]] runtime_ic_reset_result
resolve_binary_member_step(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::uint64_t runtime_size,
    string_id name,
    binary_resolve_state& state) noexcept {

    if (!strip_binary_cv(
            project,
            state.type,
            state.constant)) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    if (state.type.kind() !=
        type_ref_kind::named) {

        return runtime_ic_reset_result::unsupported_type;
    }

    type_handle owner;

    if (!project.named(
            state.type,
            owner)) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    binary_resolved_member member;

    const auto found =
        find_binary_member_recursive(
            project,
            bindings,
            owner,
            name,
            0,
            member);

    switch (found) {
    case binary_member_search_result::none:
        return runtime_ic_reset_result::not_found;

    case binary_member_search_result::ambiguous:
        return runtime_ic_reset_result::invalid_input;

    case binary_member_search_result::invalid:
        return runtime_ic_reset_result::invalid_runtime;

    case binary_member_search_result::found:
        break;
    }

    runtime_offset location = 0;

    if (!add_runtime_offset(
            state.offset,
            member.offset,
            location) ||
        location >= runtime_size) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    state.offset = location;
    state.type = member.type;
    state.default_value =
        member.default_value;

    return runtime_ic_reset_result::success;
}

[[nodiscard]] runtime_ic_reset_result
finish_binary_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::uint64_t runtime_size,
    const runtime_ic_binary_record_view& record,
    binary_resolve_state state,
    reset_ic_options options,
    runtime_ic_scalar_target& output,
    bool& excluded) noexcept {

    output = {};
    excluded = false;

    if (!strip_binary_cv(
            project,
            state.type,
            state.constant)) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    if (state.type.kind() ==
            type_ref_kind::derived ||
        state.type.kind() ==
            type_ref_kind::named) {

        return runtime_ic_reset_result::unsupported_type;
    }

    if (state.type.kind() !=
        type_ref_kind::intrinsic) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    const auto intrinsic =
        static_cast<intrinsic_type>(
            state.type.payload());

    std::uint8_t size = 0;

    if (!bindings.intrinsic_size(
            intrinsic,
            size) ||
        size == 0 ||
        size >
            runtime_ic_scalar_value{}.
                bytes.size() ||
        state.offset > runtime_size ||
        size > runtime_size - state.offset) {

        return runtime_ic_reset_result::invalid_runtime;
    }

    output.offset = state.offset;
    output.type = intrinsic;
    output.size = size;

    if (state.constant) {
        output.flags |=
            runtime_ic_target_const;
    }

    if (state.default_value) {
        output.flags |=
            runtime_ic_target_default;
    }

    excluded =
        reset_target_excluded(
            output,
            options);

    if (excluded) {
        return runtime_ic_reset_result::success;
    }

    if (record.value.empty() ||
        record.value.size() >
            runtime_ic_scalar_value{}.
                bytes.size()) {

        return runtime_ic_reset_result::invalid_image;
    }

    if (record.type != intrinsic ||
        record.value.size() != size) {

        return runtime_ic_reset_result::type_mismatch;
    }

    return runtime_ic_reset_result::success;
}

[[nodiscard]] runtime_ic_reset_result
append_binary_write(
    std::size_t record_index,
    const runtime_ic_scalar_target& target,
    binary_reset_plan& plan) noexcept {

    if (record_index >
            (std::numeric_limits<std::uint32_t>::max)() ||
        plan.bytes >
            (std::numeric_limits<std::uint64_t>::max)() -
                target.size) {

        return runtime_ic_reset_result::failed;
    }

    try {
        plan.writes.push_back(
            {
                target.offset,
                static_cast<std::uint32_t>(
                    record_index),
                target.size,
                {},
            });
    }
    catch (...) {
        return runtime_ic_reset_result::failed;
    }

    plan.bytes += target.size;

    return runtime_ic_reset_result::success;
}

void apply_binary_plan(
    std::span<std::byte> runtime,
    const runtime_ic_binary_view& image,
    const binary_reset_plan& plan) noexcept {

    for (const auto& write :
         plan.writes) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                write.record,
                record) ||
            record.value.size() !=
                write.size) {

            // The bound IC view is immutable for the lifetime of RESET.
            // Validation already proved every record before this phase.
            return;
        }

        std::memcpy(
            runtime.data() +
                static_cast<std::size_t>(
                    write.offset),
            record.value.data(),
            write.size);
    }
}

void publish_binary_stats(
    const binary_reset_plan& plan,
    runtime_ic_reset_stats* stats) noexcept {

    if (stats == nullptr) {
        return;
    }

    stats->records =
        static_cast<std::uint32_t>(
            plan.writes.size());

    stats->bytes = plan.bytes;
}


[[nodiscard]] runtime_ic_reset_result map_result(
    runtime_ic_result result) noexcept {

    switch (result) {
    case runtime_ic_result::success:
        return runtime_ic_reset_result::success;

    case runtime_ic_result::invalid_input:
        return runtime_ic_reset_result::invalid_input;

    case runtime_ic_result::not_found:
        return runtime_ic_reset_result::not_found;

    case runtime_ic_result::unsupported_type:
        return runtime_ic_reset_result::unsupported_type;

    case runtime_ic_result::type_mismatch:
        return runtime_ic_reset_result::type_mismatch;

    case runtime_ic_result::invalid_runtime:
        return runtime_ic_reset_result::invalid_runtime;
    }

    return runtime_ic_reset_result::invalid_runtime;
}

[[nodiscard]] runtime_ic_reset_result append_write(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    runtime_ic_path_view path,
    const runtime_ic_scalar_value& value,
    reset_plan& plan,
    reset_ic_options options) noexcept {

    runtime_ic_scalar_target target;

    const auto resolved =
        resolve_runtime_ic_scalar(
            project,
            bindings,
            static_cast<std::uint64_t>(
                runtime.size()),
            path,
            target);

    if (resolved != runtime_ic_result::success) {
        return map_result(resolved);
    }

    if (reset_target_excluded(
            target,
            options)) {

        return runtime_ic_reset_result::success;
    }

    if (value.type == intrinsic_type::none ||
        value.size == 0 ||
        value.size > value.bytes.size()) {

        return runtime_ic_reset_result::invalid_input;
    }

    if (target.type != value.type ||
        target.size != value.size) {

        return runtime_ic_reset_result::
            type_mismatch;
    }

    if (target.offset >
            static_cast<runtime_offset>(
                runtime.size()) ||
        target.size >
            static_cast<runtime_offset>(
                runtime.size()) -
                target.offset) {

        return runtime_ic_reset_result::
            invalid_runtime;
    }

    if (plan.bytes >
        (std::numeric_limits<std::uint64_t>::max)() -
            target.size) {

        return runtime_ic_reset_result::failed;
    }

    reset_write write;
    write.offset = target.offset;
    write.size = target.size;

    std::memcpy(
        write.bytes.data(),
        value.bytes.data(),
        value.size);

    try {
        plan.writes.push_back(
            write);
    }
    catch (...) {
        return runtime_ic_reset_result::failed;
    }

    plan.bytes += target.size;
    return runtime_ic_reset_result::success;
}

void apply_plan(
    std::span<std::byte> runtime,
    const reset_plan& plan) noexcept {

    for (const auto& write :
         plan.writes) {

        std::memcpy(
            runtime.data() +
                static_cast<std::size_t>(
                    write.offset),
            write.bytes.data(),
            write.size);
    }
}

void publish_stats(
    const reset_plan& plan,
    runtime_ic_reset_stats* stats) noexcept {

    if (stats == nullptr) {
        return;
    }

    stats->records =
        static_cast<std::uint32_t>(
            plan.writes.size());

    stats->bytes = plan.bytes;
}

[[nodiscard]] bool append_component(
    std::vector<std::string_view>& output,
    std::string_view component) noexcept {

    if (component.empty()) {
        return false;
    }

    try {
        output.push_back(
            component);
    }
    catch (...) {
        return false;
    }

    return true;
}

[[nodiscard]] bool parse_text_path(
    std::string_view text,
    std::vector<std::string_view>& object,
    std::vector<std::string_view>& members) noexcept {

    object.clear();
    members.clear();

    if (!text.starts_with("::") ||
        text.size() <= 2) {

        return false;
    }

    text.remove_prefix(2);

    const auto member_separator =
        text.find('.');

    const auto object_text =
        member_separator ==
            std::string_view::npos
        ? text
        : text.substr(
            0,
            member_separator);

    if (object_text.empty()) {
        return false;
    }

    std::size_t begin = 0;

    for (;;) {
        const auto separator =
            object_text.find(
                "::",
                begin);

        const auto end =
            separator ==
                std::string_view::npos
            ? object_text.size()
            : separator;

        if (!append_component(
                object,
                object_text.substr(
                    begin,
                    end - begin))) {

            return false;
        }

        if (separator ==
            std::string_view::npos) {

            break;
        }

        begin = separator + 2;

        if (begin >= object_text.size()) {
            return false;
        }
    }

    if (member_separator ==
        std::string_view::npos) {

        return true;
    }

    const auto member_text =
        text.substr(
            member_separator + 1);

    if (member_text.empty()) {
        return false;
    }

    begin = 0;

    for (;;) {
        const auto separator =
            member_text.find(
                '.',
                begin);

        const auto end =
            separator ==
                std::string_view::npos
            ? member_text.size()
            : separator;

        const auto component =
            member_text.substr(
                begin,
                end - begin);

        if (component.empty() ||
            component.find("::") !=
                std::string_view::npos ||
            !append_component(
                members,
                component)) {

            return false;
        }

        if (separator ==
            std::string_view::npos) {

            break;
        }

        begin = separator + 1;

        if (begin >= member_text.size()) {
            return false;
        }
    }

    return true;
}

struct text_reset_context final {
    const compiled_project_view& project;
    const runtime_binding_index& bindings;
    std::span<std::byte> runtime;
    reset_plan& plan;
    reset_ic_options options =
        reset_ic_options::none;
    std::uint32_t records = 0;

    std::vector<std::string_view> object;
    std::vector<std::string_view> members;

    runtime_ic_reset_result result =
        runtime_ic_reset_result::success;
};

[[nodiscard]] bool text_record(
    void* raw,
    std::string_view path_text,
    std::string_view value_text) noexcept {

    auto& context =
        *static_cast<text_reset_context*>(
            raw);

    ++context.records;

    if (!parse_text_path(
            path_text,
            context.object,
            context.members)) {

        context.result =
            runtime_ic_reset_result::
                invalid_image;

        return false;
    }

    runtime_ic_scalar_target target;

    const auto resolved =
        resolve_runtime_ic_scalar(
            context.project,
            context.bindings,
            static_cast<std::uint64_t>(
                context.runtime.size()),
            {
                context.object,
                context.members,
            },
            target);

    if (resolved != runtime_ic_result::success) {
        context.result =
            map_result(resolved);
        return false;
    }

    if (reset_target_excluded(
            target,
            context.options)) {

        return true;
    }

    runtime_ic_scalar_value value;

    const auto parsed =
        parse_runtime_ic_text_scalar(
            target.type,
            target.size,
            value_text,
            value);

    if (parsed != runtime_ic_codec_result::success) {
        context.result =
            parsed ==
                runtime_ic_codec_result::
                    unsupported_value
            ? runtime_ic_reset_result::
                unsupported_type
            : runtime_ic_reset_result::
                invalid_image;

        return false;
    }

    context.result =
        append_write(
            context.project,
            context.bindings,
            context.runtime,
            {
                context.object,
                context.members,
            },
            value,
            context.plan,
            context.options);

    return context.result ==
        runtime_ic_reset_result::success;
}

}

runtime_ic_reset_result reset_runtime_ic_records(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    std::span<const runtime_ic_record_source> records,
    runtime_ic_reset_stats* stats,
    reset_ic_options options) noexcept {

    if (stats != nullptr) {
        *stats = {};
    }

    if (!project.valid() ||
        runtime.empty() ||
        !valid_reset_ic_options(options) ||
        records.empty() ||
        records.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_reset_result::
            invalid_input;
    }

    reset_plan plan;

    try {
        plan.writes.reserve(
            records.size());
    }
    catch (...) {
        return runtime_ic_reset_result::failed;
    }

    for (const auto& record :
         records) {

        const auto result =
            append_write(
                project,
                bindings,
                runtime,
                record.path,
                record.value,
                plan,
                options);

        if (result !=
            runtime_ic_reset_result::success) {

            return result;
        }
    }

    apply_plan(
        runtime,
        plan);

    publish_stats(
        plan,
        stats);

    return runtime_ic_reset_result::success;
}

runtime_ic_reset_result reset_runtime_ic_binary(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    const runtime_ic_binary_view& image,
    runtime_ic_reset_stats* stats,
    reset_ic_options options) noexcept {

    if (stats != nullptr) {
        *stats = {};
    }

    if (!project.valid() ||
        runtime.empty() ||
        !valid_reset_ic_options(options) ||
        !image.valid() ||
        image.record_count() == 0 ||
        image.record_count() >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return runtime_ic_reset_result::
            invalid_input;
    }

    std::vector<string_id> translation;

    const auto translated =
        translate_binary_strings(
            project,
            image,
            translation);

    if (translated !=
        runtime_ic_reset_result::success) {

        return translated;
    }

    binary_reset_plan plan;

    std::vector<binary_resolve_state>
        prefix_states;

    try {
        plan.writes.reserve(
            image.record_count());

        prefix_states.reserve(8);
    }
    catch (...) {
        return runtime_ic_reset_result::failed;
    }

    runtime_ic_binary_record_view
        previous_record;

    bool have_previous = false;

    binary_resolve_state object_state;

    for (std::size_t index = 0;
         index < image.record_count();
         ++index) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                index,
                record)) {

            return runtime_ic_reset_result::
                invalid_image;
        }

        const auto same_object =
            have_previous &&
            same_binary_components(
                image,
                previous_record.object_begin,
                previous_record.object_count,
                record.object_begin,
                record.object_count);

        if (!same_object) {
            const auto resolved =
                resolve_binary_object(
                    project,
                    bindings,
                    static_cast<std::uint64_t>(
                        runtime.size()),
                    image,
                    translation,
                    record.object_begin,
                    record.object_count,
                    object_state);

            if (resolved !=
                runtime_ic_reset_result::success) {

                return resolved;
            }

            prefix_states.clear();

            try {
                prefix_states.push_back(
                    object_state);
            }
            catch (...) {
                return runtime_ic_reset_result::
                    failed;
            }
        }

        const auto common =
            same_object
            ? binary_common_prefix(
                  image,
                  previous_record.member_begin,
                  previous_record.member_count,
                  record.member_begin,
                  record.member_count)
            : std::size_t{0};

        if (prefix_states.size() <
            common + 1) {

            return runtime_ic_reset_result::
                invalid_runtime;
        }

        prefix_states.resize(
            common + 1);

        for (std::size_t member_index = common;
             member_index <
                 record.member_count;
             ++member_index) {

            const auto local =
                image.component(
                    record.member_begin +
                    static_cast<std::uint32_t>(
                        member_index));

            if (local == 0 ||
                local >= translation.size()) {

                return runtime_ic_reset_result::
                    invalid_image;
            }

            const auto name =
                translation[local];

            if (!name) {
                return runtime_ic_reset_result::
                    not_found;
            }

            auto state =
                prefix_states.back();

            const auto resolved =
                resolve_binary_member_step(
                    project,
                    bindings,
                    static_cast<std::uint64_t>(
                        runtime.size()),
                    name,
                    state);

            if (resolved !=
                runtime_ic_reset_result::success) {

                return resolved;
            }

            try {
                prefix_states.push_back(
                    state);
            }
            catch (...) {
                return runtime_ic_reset_result::
                    failed;
            }
        }

        runtime_ic_scalar_target target;
        bool excluded = false;

        const auto finished =
            finish_binary_scalar(
                project,
                bindings,
                static_cast<std::uint64_t>(
                    runtime.size()),
                record,
                prefix_states.back(),
                options,
                target,
                excluded);

        if (finished !=
            runtime_ic_reset_result::success) {

            return finished;
        }

        if (!excluded) {
            const auto appended =
                append_binary_write(
                    index,
                    target,
                    plan);

            if (appended !=
                runtime_ic_reset_result::success) {

                return appended;
            }
        }

        previous_record = record;
        have_previous = true;
    }

    apply_binary_plan(
        runtime,
        image,
        plan);

    publish_binary_stats(
        plan,
        stats);

    return runtime_ic_reset_result::success;
}

runtime_ic_reset_result reset_runtime_ic_text(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    std::string_view text,
    runtime_ic_reset_stats* stats,
    std::size_t* error_line,
    reset_ic_options options) noexcept {

    if (stats != nullptr) {
        *stats = {};
    }

    if (error_line != nullptr) {
        *error_line = 0;
    }

    if (!project.valid() ||
        runtime.empty() ||
        !valid_reset_ic_options(options) ||
        text.empty()) {

        return runtime_ic_reset_result::
            invalid_input;
    }

    reset_plan plan;

    text_reset_context context{
        project,
        bindings,
        runtime,
        plan,
        options,
    };

    try {
        context.object.reserve(8);
        context.members.reserve(8);
    }
    catch (...) {
        return runtime_ic_reset_result::failed;
    }

    const auto parsed =
        parse_runtime_ic_text(
            text,
            &context,
            text_record,
            error_line);

    if (parsed != runtime_ic_codec_result::success) {
        if (context.result !=
            runtime_ic_reset_result::success) {

            return context.result;
        }

        return runtime_ic_reset_result::
            invalid_image;
    }

    if (context.records == 0) {
        return runtime_ic_reset_result::
            invalid_input;
    }

    apply_plan(
        runtime,
        plan);

    publish_stats(
        plan,
        stats);

    return runtime_ic_reset_result::success;
}

}
