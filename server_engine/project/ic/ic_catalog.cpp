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
    entry,
};

enum class entry_stage : std::uint8_t {
    description,
    size,
    path,
    complete,
};

struct frame final {
    schema_context context = schema_context::root;
    std::uint8_t index = 0;
    entry_stage stage = entry_stage::description;
    ic_catalog_entry* entry = nullptr;
};

[[nodiscard]] std::filesystem::path resolve_ic_path(
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path) {

    return path.is_absolute()
        ? path.lexically_normal()
        : (catalog_path.parent_path() / path).lexically_normal();
}

[[nodiscard]] bool normalize_ic_path(
    const std::filesystem::path& path,
    std::filesystem::path& output) noexcept {

    output.clear();

    if (path.empty()) {
        return false;
    }

    try {
        output = path.lexically_normal();

        if (!output.is_absolute() &&
            (output.has_root_name() ||
             output.has_root_directory())) {

            output.clear();
            return false;
        }

        return true;
    }
    catch (...) {
        output.clear();
        return false;
    }
}

class ic_catalog_handler final : public json_event_handler {
public:
    explicit ic_catalog_handler(ic_catalog& output)
        : output(output) {
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

        if (stack.back().context != schema_context::items) {
            valid_value = false;
            return;
        }

        try {
            output.items.emplace_back();
        }
        catch (...) {
            valid_value = false;
            return;
        }

        stack.push_back({
            schema_context::entry,
            0,
            entry_stage::description,
            &output.items.back(),
        });
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

        if (ended.context == schema_context::entry) {
            if (ended.entry == nullptr ||
                ended.stage != entry_stage::complete) {

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

        auto& current = stack.back();

        if (current.context == schema_context::root &&
            current.index == 1 &&
            key_value == "items") {

            current.index = 2;
            stack.push_back({schema_context::items});
            return;
        }

        valid_value = false;
    }

    void array_end() override {
        if (!valid_value ||
            stack.empty() ||
            stack.back().context != schema_context::items) {

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

        if (current.context != schema_context::entry ||
            current.entry == nullptr) {

            valid_value = false;
            return;
        }

        switch (current.stage) {
        case entry_stage::description:
            valid_value = value == "description";
            return;
        case entry_stage::size:
            valid_value = value == "size";
            return;
        case entry_stage::path:
            valid_value = value == "path";
            return;
        case entry_stage::complete:
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
            if (current.index != 0 ||
                key_value != "version") {

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

        if (current.context != schema_context::entry ||
            current.entry == nullptr) {

            valid_value = false;
            return;
        }

        auto& entry = *current.entry;

        switch (current.stage) {
        case entry_stage::description:
            if (!value.get(entry.description)) {
                valid_value = false;
                return;
            }

            current.stage = entry_stage::size;
            return;

        case entry_stage::size:
            if (!value.get(entry.size)) {
                valid_value = false;
                return;
            }

            current.stage = entry_stage::path;
            return;

        case entry_stage::path: {
            std::string path_text;

            if (!value.get(path_text) ||
                path_text.empty() ||
                filesystem_path_from_utf8(
                    path_text,
                    entry.path) != filesystem_path_result::success) {

                valid_value = false;
                return;
            }

            if (!entry.path.is_absolute() &&
                (entry.path.has_root_name() ||
                 entry.path.has_root_directory())) {

                valid_value = false;
                return;
            }

            entry.path = entry.path.lexically_normal();
            current.stage = entry_stage::complete;
            return;
        }

        case entry_stage::complete:
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

[[nodiscard]] bool validate_catalog(
    const std::filesystem::path& catalog_path,
    const ic_catalog& catalog) noexcept {

    if (catalog_path.empty()) {
        return false;
    }

    try {
        for (std::size_t left = 0;
             left < catalog.items.size();
             ++left) {

            const auto& entry = catalog.items[left];

            if (entry.path.empty()) {
                return false;
            }

            const auto left_path =
                resolve_ic_path(
                    catalog_path,
                    entry.path);

            filesystem_path_key left_key;

            if (make_filesystem_path_key(
                    left_path,
                    left_key) != filesystem_path_result::success) {

                return false;
            }

            for (std::size_t right = left + 1;
                 right < catalog.items.size();
                 ++right) {

                const auto& other = catalog.items[right];

                const auto right_path =
                    resolve_ic_path(
                        catalog_path,
                        other.path);

                filesystem_path_key right_key;

                if (make_filesystem_path_key(
                        right_path,
                        right_key) != filesystem_path_result::success) {

                    return false;
                }

                if (left_key == right_key) {
                    return false;
                }
            }
        }

        return true;
    }
    catch (...) {
        return false;
    }
}

void append_json_string(
    std::string_view value,
    std::string& output) {

    static constexpr char hex[] =
        "0123456789ABCDEF";

    output.push_back('"');

    for (const auto ch : value) {
        const auto byte =
            static_cast<unsigned char>(ch);

        switch (ch) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (byte < 0x20u) {
                output += "\\u00";
                output.push_back(
                    hex[(byte >> 4u) & 0x0Fu]);
                output.push_back(
                    hex[byte & 0x0Fu]);
            } else {
                output.push_back(ch);
            }
            break;
        }
    }

    output.push_back('"');
}

[[nodiscard]] bool append_entry(
    const ic_catalog_entry& entry,
    std::string& output) {

    std::string path_text;

    if (filesystem_path_to_utf8(
            entry.path,
            path_text) != filesystem_path_result::success) {

        return false;
    }

    output += "{\"description\":";
    append_json_string(
        entry.description,
        output);

    output += ",\"size\":";

    char buffer[32]{};

    const auto converted =
        std::to_chars(
            buffer,
            buffer + sizeof(buffer),
            entry.size);

    if (converted.ec != std::errc{}) {
        return false;
    }

    output.append(
        buffer,
        converted.ptr);

    output += ",\"path\":";
    append_json_string(
        path_text,
        output);

    output.push_back('}');
    return true;
}

}

ic_catalog_result load_ic_catalog(
    const std::filesystem::path& path,
    ic_catalog& output) noexcept {

    output = {};

    try {
        std::ifstream input(
            path,
            std::ios::binary);

        if (!input) {
            std::error_code error;

            const auto exists =
                std::filesystem::exists(
                    path,
                    error);

            return !error && !exists
                ? ic_catalog_result::not_found
                : ic_catalog_result::io_error;
        }

        std::string bytes{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};

        if (!input.good() &&
            !input.eof()) {

            return ic_catalog_result::io_error;
        }

        ic_catalog candidate;
        ic_catalog_handler handler{candidate};

        const auto parsed =
            parse_json(
                std::string_view{bytes},
                handler);

        if (!parsed.ok() ||
            !handler.valid() ||
            !validate_catalog(
                path,
                candidate)) {

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
        if (!validate_catalog(
                path,
                catalog)) {

            return ic_catalog_result::invalid;
        }

        std::string bytes;
        bytes.reserve(1024);
        bytes += "{\"version\":1,\"items\":[";

        for (std::size_t index = 0;
             index < catalog.items.size();
             ++index) {

            if (index != 0) {
                bytes.push_back(',');
            }

            if (!append_entry(
                    catalog.items[index],
                    bytes)) {

                return ic_catalog_result::failed;
            }
        }

        bytes += "]}\n";

        auto temporary_path = path;
        temporary_path += ".new";

        std::error_code error;

        std::filesystem::remove(
            temporary_path,
            error);

        if (error) {
            return ic_catalog_result::io_error;
        }

        std::ofstream output(
            temporary_path,
            std::ios::binary |
                std::ios::trunc);

        if (!output) {
            return ic_catalog_result::io_error;
        }

        output.write(
            bytes.data(),
            static_cast<std::streamsize>(
                bytes.size()));

        output.flush();

        if (!output) {
            output.close();

            error.clear();
            std::filesystem::remove(
                temporary_path,
                error);

            return ic_catalog_result::io_error;
        }

        output.close();

        if (!output) {
            error.clear();
            std::filesystem::remove(
                temporary_path,
                error);

            return ic_catalog_result::io_error;
        }

        if (replace_file(
                temporary_path,
                path) !=
            filesystem_replace_result::success) {

            error.clear();
            std::filesystem::remove(
                temporary_path,
                error);

            return ic_catalog_result::io_error;
        }

        return ic_catalog_result::success;
    }
    catch (...) {
        return ic_catalog_result::failed;
    }
}

const ic_catalog_entry* find_ic_catalog_entry(
    const ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path) noexcept {

    if (catalog_path.empty() ||
        path.empty()) {

        return nullptr;
    }

    try {
        std::filesystem::path normalized;

        if (!normalize_ic_path(
                path,
                normalized)) {

            return nullptr;
        }

        filesystem_path_key target_key;

        if (make_filesystem_path_key(
                resolve_ic_path(
                    catalog_path,
                    normalized),
                target_key) != filesystem_path_result::success) {

            return nullptr;
        }

        for (const auto& entry : catalog.items) {
            filesystem_path_key entry_key;

            if (make_filesystem_path_key(
                    resolve_ic_path(
                        catalog_path,
                        entry.path),
                    entry_key) != filesystem_path_result::success) {

                return nullptr;
            }

            if (entry_key == target_key) {
                return &entry;
            }
        }

        return nullptr;
    }
    catch (...) {
        return nullptr;
    }
}

ic_catalog_entry* find_ic_catalog_entry(
    ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path) noexcept {

    return const_cast<ic_catalog_entry*>(
        find_ic_catalog_entry(
            static_cast<const ic_catalog&>(
                catalog),
            catalog_path,
            path));
}

bool upsert_ic_catalog_entry(
    ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path,
    std::uint64_t size) noexcept {

    if (catalog_path.empty() ||
        path.empty()) {

        return false;
    }

    try {
        std::filesystem::path normalized;

        if (!normalize_ic_path(
                path,
                normalized)) {

            return false;
        }

        filesystem_path_key target_key;

        if (make_filesystem_path_key(
                resolve_ic_path(
                    catalog_path,
                    normalized),
                target_key) != filesystem_path_result::success) {

            return false;
        }

        for (auto& entry : catalog.items) {
            filesystem_path_key entry_key;

            if (make_filesystem_path_key(
                    resolve_ic_path(
                        catalog_path,
                        entry.path),
                    entry_key) != filesystem_path_result::success) {

                return false;
            }

            if (entry_key == target_key) {
                entry.size = size;
                return true;
            }
        }

        ic_catalog_entry entry;
        entry.path = normalized;
        entry.size = size;

        catalog.items.push_back(
            std::move(entry));

        return true;
    }
    catch (...) {
        return false;
    }
}

bool erase_ic_catalog_entry(
    ic_catalog& catalog,
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& path,
    std::filesystem::path& removed_path) noexcept {

    removed_path.clear();

    if (catalog_path.empty() ||
        path.empty()) {

        return false;
    }

    try {
        std::filesystem::path normalized;

        if (!normalize_ic_path(
                path,
                normalized)) {

            return false;
        }

        filesystem_path_key target_key;

        if (make_filesystem_path_key(
                resolve_ic_path(
                    catalog_path,
                    normalized),
                target_key) != filesystem_path_result::success) {

            return false;
        }

        for (auto current = catalog.items.begin();
             current != catalog.items.end();
             ++current) {

            filesystem_path_key current_key;

            if (make_filesystem_path_key(
                    resolve_ic_path(
                        catalog_path,
                        current->path),
                    current_key) != filesystem_path_result::success) {

                return false;
            }

            if (current_key != target_key) {
                continue;
            }

            removed_path = current->path;
            catalog.items.erase(current);
            return true;
        }

        return false;
    }
    catch (...) {
        removed_path.clear();
        return false;
    }
}

}
