/*
 * Streaming Project configuration validator/reference producer.
 *
 * Validation is state-machine based and does not construct a Project
 * configuration tree. Direct child Project paths are retained only for recursive
 * composition.
 */
#include "project_configuration_loader.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"
#include "../json/json_parser.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

inline constexpr std::uint32_t current_project_configuration_version = 1;

enum class schema_context : std::uint8_t {
    root,
    project_items,
    item,
    configuration,
    abi,
    item_children,
};

enum class item_type : std::uint8_t {
    none,
    group,
    header,
    source,
    project,
};

enum class item_stage : std::uint8_t {
    expect_name,
    expect_type,
    expect_payload,
    complete,
};

struct schema_error {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::string detail;
};

struct frame {
    schema_context context = schema_context::root;
    std::uint8_t index = 0;
    item_stage stage = item_stage::expect_name;
    item_type type = item_type::none;
};

[[nodiscard]] bool valid_pack(std::uint32_t value) noexcept {
    return value == 1 ||
           value == 2 ||
           value == 4 ||
           value == 8 ||
           value == 16;
}

class project_configuration_handler final : public json_event_handler {
public:
    explicit project_configuration_handler(
        std::vector<std::filesystem::path>& project_references)
        : project_references(project_references) {

        stack.reserve(16);
    }

    void location(
        std::size_t offset,
        std::size_t length) override {

        current_offset = offset;
        current_length = length;
    }

    void object_begin() override {
        if (failed()) {
            return;
        }

        if (stack.empty()) {
            stack.push_back({schema_context::root});
            return;
        }

        auto& parent = stack.back();

        if (parent.context == schema_context::project_items ||
            parent.context == schema_context::item_children) {

            stack.push_back({schema_context::item});
            return;
        }

        if (parent.context == schema_context::root &&
            parent.index == 3) {

            parent.index = 4;
            stack.push_back({schema_context::configuration});
            return;
        }

        if (parent.context == schema_context::configuration &&
            parent.index == 0) {

            parent.index = 1;
            stack.push_back({schema_context::abi});
            return;
        }

        fail("unexpected object in ordered Project configuration");
    }

    void object_end() override {
        if (failed() || stack.empty()) {
            return;
        }

        const auto ended =
            stack.back();

        stack.pop_back();

        switch (ended.context) {
        case schema_context::root:
            if (ended.index != 4) {
                fail(
                    "Project root requires fields in order: "
                    "version, name, project, configuration");
            }
            return;

        case schema_context::configuration:
            if (ended.index != 1) {
                fail("configuration requires abi");
            }
            return;

        case schema_context::abi:
            if (ended.index != 2) {
                fail("configuration.abi requires target then pack");
            }
            return;

        case schema_context::item:
            if (ended.stage != item_stage::complete) {
                fail("Project item is incomplete");
            }
            return;

        case schema_context::project_items:
        case schema_context::item_children:
            fail("internal Project schema object mismatch");
            return;
        }
    }

    void array_begin() override {
        if (failed() || stack.empty()) {
            return;
        }

        auto& parent = stack.back();

        if (parent.context == schema_context::root &&
            parent.index == 2) {

            parent.index = 3;
            stack.push_back({schema_context::project_items});
            return;
        }

        if (parent.context == schema_context::item &&
            parent.stage == item_stage::expect_payload &&
            parent.type == item_type::group) {

            parent.stage = item_stage::complete;
            stack.push_back({schema_context::item_children});
            return;
        }

        fail("unexpected array in ordered Project configuration");
    }

    void array_end() override {
        if (failed() || stack.empty()) {
            return;
        }

        const auto context =
            stack.back().context;

        if (context != schema_context::project_items &&
            context != schema_context::item_children) {

            fail("unexpected array end in Project configuration");
            return;
        }

        stack.pop_back();
    }

    void key(std::string_view key) override {
        if (failed() || stack.empty()) {
            return;
        }

        auto& current =
            stack.back();

        switch (current.context) {
        case schema_context::root:
            validate_root_key(current, key);
            return;

        case schema_context::configuration:
            if (current.index != 0 || key != "abi") {
                fail("configuration fields must be ordered: abi");
            }
            return;

        case schema_context::abi:
            if (current.index == 0) {
                if (key != "target") {
                    fail(
                        "configuration.abi fields must be ordered: "
                        "target, pack");
                }
                return;
            }

            if (current.index == 1) {
                if (key != "pack") {
                    fail(
                        "configuration.abi fields must be ordered: "
                        "target, pack");
                }
                return;
            }

            fail("configuration.abi contains extra field");
            return;

        case schema_context::item:
            validate_item_key(current, key);
            return;

        case schema_context::project_items:
        case schema_context::item_children:
            fail("array elements must be Project item objects");
            return;
        }
    }

    void value(json_value_view value) override {
        if (failed() || stack.empty()) {
            return;
        }

        auto& current =
            stack.back();

        switch (current.context) {
        case schema_context::root:
            read_root_value(current, value);
            return;

        case schema_context::abi:
            read_abi_value(current, value);
            return;

        case schema_context::item:
            read_item_value(current, value);
            return;

        case schema_context::configuration:
        case schema_context::project_items:
        case schema_context::item_children:
            fail("unexpected scalar in Project configuration");
            return;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return !failed() &&
               stack.empty();
    }

    [[nodiscard]] const schema_error& error() const noexcept {
        return error_value;
    }

private:
    [[nodiscard]] bool failed() const noexcept {
        return !error_value.detail.empty();
    }

    void fail(std::string detail) {
        if (failed()) {
            return;
        }

        error_value = {
            current_offset,
            current_length,
            std::move(detail),
        };
    }

    void validate_root_key(
        const frame& current,
        std::string_view key) {

        static constexpr std::array<std::string_view, 4> keys{
            "version",
            "name",
            "project",
            "configuration",
        };

        if (current.index >= keys.size()) {
            fail("Project root contains extra field");
            return;
        }

        if (key != keys[current.index]) {
            fail(
                "Project root fields must be ordered: "
                "version, name, project, configuration");
        }
    }

    void validate_item_key(
        frame& current,
        std::string_view key) {

        switch (current.stage) {
        case item_stage::expect_name:
            if (key != "name") {
                fail("Project item fields must begin with name");
            }
            return;

        case item_stage::expect_type:
            if (key != "type") {
                fail("Project item field type must follow name");
            }
            return;

        case item_stage::expect_payload:
            if (current.type == item_type::group) {
                if (key != "children") {
                    fail("group item requires children after type");
                }
                return;
            }

            if (key != "path") {
                fail(
                    "header/source/project item requires path after type");
            }
            return;

        case item_stage::complete:
            fail("Project item contains extra field");
            return;
        }
    }

    void read_root_value(
        frame& current,
        json_value_view value) {

        if (current.index == 0) {
            std::uint32_t version = 0;

            if (!value.get(version)) {
                fail("version must be an unsigned integer");
                return;
            }

            if (version != current_project_configuration_version) {
                fail("unsupported Project configuration version");
                return;
            }

            ++current.index;
            return;
        }

        if (current.index == 1) {
            std::string name;

            if (!value.get(name) ||
                name.empty()) {

                fail("name must be a non-empty string");
                return;
            }

            ++current.index;
            return;
        }

        fail("Project root object/array field expected");
    }

    void read_abi_value(
        frame& current,
        json_value_view value) {

        if (current.index == 0) {
            std::string target;

            if (!value.get(target)) {
                fail("configuration.abi.target must be a string");
                return;
            }

            if (target != "windows-x64" &&
                target != "posix-x64") {

                fail(
                    "configuration.abi.target must be "
                    "windows-x64 or posix-x64");
                return;
            }

            ++current.index;
            return;
        }

        if (current.index == 1) {
            std::uint32_t pack = 0;

            if (!value.get(pack) ||
                !valid_pack(pack)) {

                fail(
                    "configuration.abi.pack must be "
                    "1, 2, 4, 8, or 16");
                return;
            }

            ++current.index;
            return;
        }

        fail("configuration.abi contains extra scalar");
    }

    void read_item_value(
        frame& current,
        json_value_view value) {

        switch (current.stage) {
        case item_stage::expect_name: {
            std::string name;

            if (!value.get(name) ||
                name.empty()) {

                fail("Project item name must be a non-empty string");
                return;
            }

            current.stage =
                item_stage::expect_type;
            return;
        }

        case item_stage::expect_type: {
            std::string type;

            if (!value.get(type)) {
                fail("Project item type must be a string");
                return;
            }

            if (type == "group") {
                current.type = item_type::group;
            } else if (type == "header") {
                current.type = item_type::header;
            } else if (type == "source") {
                current.type = item_type::source;
            } else if (type == "project") {
                current.type = item_type::project;
            } else {
                fail(
                    "Project item type must be "
                    "group, header, source, or project");
                return;
            }

            current.stage =
                item_stage::expect_payload;
            return;
        }

        case item_stage::expect_payload:
            if (current.type == item_type::group) {
                fail("group children must be an array");
                return;
            }

            {
                std::string path;

                if (!value.get(path) ||
                    path.empty()) {

                    fail("Project item path must be a non-empty string");
                    return;
                }

                if (current.type ==
                    item_type::project) {

                    project_references.emplace_back(
                        std::move(path));
                }
            }

            current.stage =
                item_stage::complete;
            return;

        case item_stage::complete:
            fail("Project item contains extra scalar");
            return;
        }
    }

    std::vector<std::filesystem::path>& project_references;
    std::vector<frame> stack;

    std::size_t current_offset = 0;
    std::size_t current_length = 0;
    schema_error error_value;
};

} // namespace

server_status read_project_configuration(
    std::string& bytes,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::vector<std::filesystem::path>& project_references) {

    project_references.clear();

    project_configuration_handler handler{
        project_references};

    const auto parsed =
        parse_json(
            std::string_view{bytes},
            handler);

    if (!parsed.ok()) {
        project_references.clear();

        const auto file_id =
            diagnostics.add_source(
                path,
                std::move(bytes));

        diagnostics.emit(
            diagnostic(
                diagnostics::project_invalid_json,
                operation)
                .location(
                    diagnostics.locate(
                        file_id,
                        parsed.offset,
                        parsed.length))
                .detail(
                    json_error_message(
                        parsed.code))
                .build());

        return server_status::project_configuration_invalid;
    }

    if (!handler.valid()) {
        project_references.clear();

        const auto& error =
            handler.error();

        const auto file_id =
            diagnostics.add_source(
                path,
                std::move(bytes));

        diagnostics.emit(
            diagnostic(
                diagnostics::project_invalid_configuration,
                operation)
                .location(
                    diagnostics.locate(
                        file_id,
                        error.offset,
                        error.length))
                .detail(error.detail)
                .build());

        return server_status::project_configuration_invalid;
    }

    return server_status::success;
}

}
