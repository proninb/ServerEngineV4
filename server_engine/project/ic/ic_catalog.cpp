#include "ic_catalog.hpp"

#include "../../filesystem_path.hpp"
#include "../../json/json_parser.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace cw::server {
namespace {

inline constexpr std::uint32_t current_ic_catalog_version = 1;

enum class schema_context : std::uint8_t {
    root,
    items,
    node,
    children,
};

enum class node_stage : std::uint8_t {
    name,
    type,
    payload,
    size,
    path,
    complete,
};

struct frame final {
    schema_context context = schema_context::root;
    std::uint8_t index = 0;
    node_stage stage = node_stage::name;
    ic_catalog_node* node = nullptr;
    std::vector<ic_catalog_node>* list = nullptr;
};

class ic_catalog_handler final : public json_event_handler {
public:
    explicit ic_catalog_handler(ic_catalog& output)
        : output(output) {

        stack.reserve(16);
    }

    void location(std::size_t, std::size_t) override {
    }

    void object_begin() override {
        if (!valid_value) {
            return;
        }

        if (stack.empty()) {
            if (root_started) {
                valid_value = false;
                return;
            }

            root_started = true;
            stack.push_back({schema_context::root});
            return;
        }

        auto& parent = stack.back();

        if ((parent.context == schema_context::items ||
             parent.context == schema_context::children) &&
            parent.list != nullptr) {

            try {
                parent.list->emplace_back();
            }
            catch (...) {
                valid_value = false;
                return;
            }

            stack.push_back({
                schema_context::node,
                0,
                node_stage::name,
                &parent.list->back(),
                nullptr,
            });
            return;
        }

        valid_value = false;
    }

    void object_end() override {
        if (!valid_value || stack.empty()) {
            valid_value = false;
            return;
        }

        const auto ended = stack.back();
        stack.pop_back();

        if (ended.context == schema_context::root) {
            if (ended.index != 2 || !stack.empty()) {
                valid_value = false;
                return;
            }

            root_completed = true;
            return;
        }

        if (ended.context == schema_context::node) {
            if (ended.node == nullptr ||
                ended.stage != node_stage::complete) {

                valid_value = false;
            }
            return;
        }

        valid_value = false;
    }

    void array_begin() override {
        if (!valid_value || stack.empty()) {
            valid_value = false;
            return;
        }

        auto& parent = stack.back();

        if (parent.context == schema_context::root &&
            parent.index == 1 &&
            key_value == "items") {

            parent.index = 2;
            stack.push_back({
                schema_context::items,
                0,
                node_stage::name,
                nullptr,
                &output.items,
            });
            return;
        }

        if (parent.context == schema_context::node &&
            parent.node != nullptr &&
            parent.node->kind == ic_catalog_node_kind::group &&
            parent.stage == node_stage::payload &&
            key_value == "children") {

            parent.stage = node_stage::complete;
            stack.push_back({
                schema_context::children,
                0,
                node_stage::name,
                nullptr,
                &parent.node->children,
            });
            return;
        }

        valid_value = false;
    }

    void array_end() override {
        if (!valid_value || stack.empty()) {
            valid_value = false;
            return;
        }

        const auto context = stack.back().context;

        if (context != schema_context::items &&
            context != schema_context::children) {

            valid_value = false;
            return;
        }

        stack.pop_back();
    }

    void key(std::string_view value) override {
        if (!valid_value || stack.empty()) {
            valid_value = false;
            return;
        }

        key_value.assign(value.data(), value.size());
        auto& current = stack.back();

        if (current.context == schema_context::root) {
            if ((current.index == 0 && value != "version") ||
                (current.index == 1 && value != "items") ||
                current.index > 1) {

                valid_value = false;
            }
            return;
        }

        if (current.context != schema_context::node ||
            current.node == nullptr) {

            valid_value = false;
            return;
        }

        switch (current.stage) {
        case node_stage::name:
            valid_value = value == "name";
            return;
        case node_stage::type:
            valid_value = value == "type";
            return;
        case node_stage::payload:
            valid_value =
                current.node->kind == ic_catalog_node_kind::group
                    ? value == "children"
                    : value == "description";
            return;
        case node_stage::size:
            valid_value =
                current.node->kind == ic_catalog_node_kind::ic &&
                value == "size";
            return;
        case node_stage::path:
            valid_value =
                current.node->kind == ic_catalog_node_kind::ic &&
                value == "path";
            return;
        case node_stage::complete:
            valid_value = false;
            return;
        }
    }

    void value(json_value_view value) override {
        if (!valid_value || stack.empty()) {
            valid_value = false;
            return;
        }

        auto& current = stack.back();

        if (current.context == schema_context::root) {
            if (current.index != 0 || key_value != "version") {
                valid_value = false;
                return;
            }

            std::uint32_t version = 0;

            if (!value.get(version) ||
                version != current_ic_catalog_version) {

                valid_value = false;
                return;
            }

            current.index = 1;
            return;
        }

        if (current.context != schema_context::node ||
            current.node == nullptr) {

            valid_value = false;
            return;
        }

        auto& node = *current.node;

        switch (current.stage) {
        case node_stage::name:
            if (!value.get(node.name) || node.name.empty()) {
                valid_value = false;
                return;
            }
            current.stage = node_stage::type;
            return;

        case node_stage::type: {
            std::string type;
            if (!value.get(type)) {
                valid_value = false;
                return;
            }

            if (type == "group") {
                node.kind = ic_catalog_node_kind::group;
            } else if (type == "ic") {
                node.kind = ic_catalog_node_kind::ic;
            } else {
                valid_value = false;
                return;
            }

            current.stage = node_stage::payload;
            return;
        }

        case node_stage::payload:
            if (node.kind != ic_catalog_node_kind::ic ||
                !value.get(node.description)) {

                valid_value = false;
                return;
            }
            current.stage = node_stage::size;
            return;

        case node_stage::size:
            if (node.kind != ic_catalog_node_kind::ic ||
                !value.get(node.size)) {

                valid_value = false;
                return;
            }
            current.stage = node_stage::path;
            return;

        case node_stage::path: {
            if (node.kind != ic_catalog_node_kind::ic) {
                valid_value = false;
                return;
            }

            std::string text;
            if (!value.get(text) || text.empty() ||
                filesystem_path_from_utf8(text, node.path) !=
                    filesystem_path_result::success) {

                valid_value = false;
                return;
            }

            if (!node.path.is_absolute() &&
                (node.path.has_root_name() ||
                 node.path.has_root_directory())) {

                valid_value = false;
                return;
            }

            node.path = node.path.lexically_normal();
            current.stage = node_stage::complete;
            return;
        }

        case node_stage::complete:
            valid_value = false;
            return;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_value &&
            root_started &&
            root_completed &&
            stack.empty();
    }

private:
    ic_catalog& output;
    std::vector<frame> stack;
    std::string key_value;
    bool valid_value = true;
    bool root_started = false;
    bool root_completed = false;
};

[[nodiscard]] bool validate_nodes(
    const std::vector<ic_catalog_node>& nodes) noexcept {

    for (std::size_t left = 0; left < nodes.size(); ++left) {
        const auto& node = nodes[left];

        if (node.name.empty()) {
            return false;
        }

        for (std::size_t right = left + 1;
             right < nodes.size();
             ++right) {

            if (nodes[right].name == node.name) {
                return false;
            }
        }

        if (node.kind == ic_catalog_node_kind::group) {
            if (!node.description.empty() ||
                node.size != 0 ||
                !node.path.empty() ||
                !validate_nodes(node.children)) {

                return false;
            }
        } else if (!node.children.empty() || node.path.empty()) {
            return false;
        }
    }

    return true;
}

void append_json_string(
    std::string_view value,
    std::string& output) {

    static constexpr char hex[] = "0123456789ABCDEF";
    output.push_back('"');

    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20u) {
                output += "\\u00";
                output.push_back(hex[(byte >> 4u) & 0x0Fu]);
                output.push_back(hex[byte & 0x0Fu]);
            } else {
                output.push_back(ch);
            }
            break;
        }
    }

    output.push_back('"');
}

[[nodiscard]] bool append_node(
    const ic_catalog_node& node,
    std::string& output) {

    output += "{\"name\":";
    append_json_string(node.name, output);

    if (node.kind == ic_catalog_node_kind::group) {
        output += ",\"type\":\"group\",\"children\":[";
        for (std::size_t index = 0; index < node.children.size(); ++index) {
            if (index != 0) {
                output.push_back(',');
            }
            if (!append_node(node.children[index], output)) {
                return false;
            }
        }
        output += "]}";
        return true;
    }

    std::string path;
    if (filesystem_path_to_utf8(node.path, path) !=
        filesystem_path_result::success) {
        return false;
    }

    output += ",\"type\":\"ic\",\"description\":";
    append_json_string(node.description, output);
    output += ",\"size\":";

    char buffer[32]{};
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), node.size);
    if (converted.ec != std::errc{}) {
        return false;
    }
    output.append(buffer, converted.ptr);

    output += ",\"path\":";
    append_json_string(path, output);
    output.push_back('}');
    return true;
}

[[nodiscard]] std::vector<ic_catalog_node>* find_group_list(
    ic_catalog& catalog,
    const std::vector<std::string>& group) noexcept {

    auto* list = &catalog.items;
    for (const auto& component : group) {
        auto found = std::find_if(
            list->begin(), list->end(),
            [&](const ic_catalog_node& node) {
                return node.name == component;
            });

        if (found == list->end() ||
            found->kind != ic_catalog_node_kind::group) {
            return nullptr;
        }

        list = &found->children;
    }

    return list;
}

[[nodiscard]] const std::vector<ic_catalog_node>* find_group_list(
    const ic_catalog& catalog,
    const std::vector<std::string>& group) noexcept {

    const auto* list = &catalog.items;
    for (const auto& component : group) {
        const auto found = std::find_if(
            list->begin(), list->end(),
            [&](const ic_catalog_node& node) {
                return node.name == component;
            });

        if (found == list->end() ||
            found->kind != ic_catalog_node_kind::group) {
            return nullptr;
        }

        list = &found->children;
    }

    return list;
}

[[nodiscard]] std::vector<ic_catalog_node>* ensure_group_list(
    ic_catalog& catalog,
    const std::vector<std::string>& group) {

    auto* list = &catalog.items;

    for (const auto& component : group) {
        if (component.empty()) {
            return nullptr;
        }

        auto found = std::find_if(
            list->begin(), list->end(),
            [&](const ic_catalog_node& node) {
                return node.name == component;
            });

        if (found == list->end()) {
            ic_catalog_node node;
            node.kind = ic_catalog_node_kind::group;
            node.name = component;
            list->push_back(std::move(node));
            found = std::prev(list->end());
        } else if (found->kind != ic_catalog_node_kind::group) {
            return nullptr;
        }

        list = &found->children;
    }

    return list;
}

}

ic_catalog_result load_ic_catalog(
    const std::filesystem::path& path,
    ic_catalog& output) noexcept {

    output = {};

    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::error_code error;
            const auto exists = std::filesystem::exists(path, error);
            return !error && !exists
                ? ic_catalog_result::not_found
                : ic_catalog_result::io_error;
        }

        std::string bytes{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};

        if (!input.good() && !input.eof()) {
            return ic_catalog_result::io_error;
        }

        ic_catalog candidate;
        ic_catalog_handler handler{candidate};
        const auto parsed = parse_json(std::string_view{bytes}, handler);

        if (!parsed.ok() || !handler.valid() ||
            !validate_nodes(candidate.items)) {
            return ic_catalog_result::invalid;
        }

        output = std::move(candidate);
        return ic_catalog_result::success;
    }
    catch (...) {
        output = {};
        return ic_catalog_result::failed;
    }
}

ic_catalog_result save_ic_catalog(
    const std::filesystem::path& path,
    const ic_catalog& catalog) noexcept {

    try {
        if (path.empty() || !validate_nodes(catalog.items)) {
            return ic_catalog_result::invalid;
        }

        std::string bytes;
        bytes.reserve(1024);
        bytes += "{\"version\":1,\"items\":[";

        for (std::size_t index = 0; index < catalog.items.size(); ++index) {
            if (index != 0) {
                bytes.push_back(',');
            }
            if (!append_node(catalog.items[index], bytes)) {
                return ic_catalog_result::failed;
            }
        }

        bytes += "]}\n";

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return ic_catalog_result::io_error;
        }

        output.write(
            bytes.data(),
            static_cast<std::streamsize>(bytes.size()));
        output.flush();

        return output
            ? ic_catalog_result::success
            : ic_catalog_result::io_error;
    }
    catch (...) {
        return ic_catalog_result::failed;
    }
}

const ic_catalog_node* find_ic_catalog_node(
    const ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name) noexcept {

    if (name.empty()) {
        return nullptr;
    }

    const auto* list = find_group_list(catalog, group);
    if (list == nullptr) {
        return nullptr;
    }

    const auto found = std::find_if(
        list->begin(), list->end(),
        [&](const ic_catalog_node& node) {
            return node.kind == ic_catalog_node_kind::ic &&
                node.name == name;
        });

    return found == list->end() ? nullptr : &*found;
}

ic_catalog_node* find_ic_catalog_node(
    ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name) noexcept {

    return const_cast<ic_catalog_node*>(
        find_ic_catalog_node(
            static_cast<const ic_catalog&>(catalog),
            group,
            name));
}

bool upsert_ic_catalog_node(
    ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name,
    const std::filesystem::path& path,
    std::uint64_t size) noexcept {

    if (name.empty() || path.empty()) {
        return false;
    }

    try {
        auto* list = ensure_group_list(catalog, group);
        if (list == nullptr) {
            return false;
        }

        auto found = std::find_if(
            list->begin(), list->end(),
            [&](const ic_catalog_node& node) {
                return node.name == name;
            });

        if (found != list->end()) {
            if (found->kind != ic_catalog_node_kind::ic) {
                return false;
            }

            found->path = path.lexically_normal();
            found->size = size;
            return true;
        }

        ic_catalog_node node;
        node.kind = ic_catalog_node_kind::ic;
        node.name.assign(name.data(), name.size());
        node.path = path.lexically_normal();
        node.size = size;
        list->push_back(std::move(node));
        return true;
    }
    catch (...) {
        return false;
    }
}

bool erase_ic_catalog_node(
    ic_catalog& catalog,
    const std::vector<std::string>& group,
    std::string_view name,
    std::filesystem::path& removed_path) noexcept {

    removed_path.clear();
    auto* list = find_group_list(catalog, group);

    if (list == nullptr || name.empty()) {
        return false;
    }

    const auto found = std::find_if(
        list->begin(), list->end(),
        [&](const ic_catalog_node& node) {
            return node.kind == ic_catalog_node_kind::ic &&
                node.name == name;
        });

    if (found == list->end()) {
        return false;
    }

    try {
        removed_path = found->path;
        list->erase(found);
        return true;
    }
    catch (...) {
        removed_path.clear();
        return false;
    }
}

}
