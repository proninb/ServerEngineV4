/*
 * Streaming Project configuration validator/reference producer.
 *
 * Validation is state-machine based and does not construct a Project
 * configuration tree. Direct child Project locators retain relative/absolute semantics only for
 * recursive composition.
 */
#include "project_configuration_loader.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../filesystem_path.hpp"
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
    preprocessor,
    predefines,
    predefine,
    item_children,
};

enum class item_type : std::uint8_t {
    none,
    group,
    header,
    source,
    assign,
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

class project_configuration_handler final : public json_event_handler {
public:
    project_configuration_handler(
        std::vector<project_configuration_dependency>& dependencies,
        project_preprocessor_configuration& preprocessor)
        : dependencies(dependencies),
          preprocessor(preprocessor) {

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
            if (root_started) {
                fail(
                    "Project configuration must contain exactly one root object");
                return;
            }

            root_started = true;
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
            stack.push_back({schema_context::preprocessor});
            return;
        }

        if (parent.context == schema_context::predefines) {
            try {
                preprocessor.predefines.emplace_back();
            }
            catch (...) {
                fail("cannot allocate Project predefine");
                return;
            }

            stack.push_back({schema_context::predefine});
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
                    "version, name, project, preprocessor");
                return;
            }

            root_completed = true;
            return;

        case schema_context::preprocessor:
            if (ended.index != 1) {
                fail("preprocessor requires predefines");
            }
            return;

        case schema_context::predefine:
            if (ended.index == 0) {
                fail("predefine requires name");
            }
            return;

        case schema_context::item:
            if (ended.stage != item_stage::complete) {
                fail("Project item is incomplete");
            }
            return;

        case schema_context::project_items:
        case schema_context::predefines:
        case schema_context::item_children:
            fail("internal Project schema object mismatch");
            return;
        }
    }

    void array_begin() override {
        if (failed()) {
            return;
        }

        if (stack.empty()) {
            fail(
                "Project configuration root must be an object");
            return;
        }

        auto& parent = stack.back();

        if (parent.context == schema_context::root &&
            parent.index == 2) {

            parent.index = 3;
            stack.push_back({schema_context::project_items});
            return;
        }

        if (parent.context == schema_context::preprocessor &&
            parent.index == 0) {

            parent.index = 1;
            stack.push_back({schema_context::predefines});
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
            context != schema_context::predefines &&
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

        case schema_context::preprocessor:
            if (current.index != 0 || key != "predefines") {
                fail("preprocessor fields must be ordered: predefines");
            }
            return;

        case schema_context::predefine:
            if (current.index == 0) {
                if (key != "name") {
                    fail("predefine fields must begin with name");
                }
                return;
            }

            if (current.index == 1) {
                if (key != "replacement") {
                    fail("predefine optional replacement must follow name");
                }
                return;
            }

            fail("predefine contains extra field");
            return;

        case schema_context::item:
            validate_item_key(current, key);
            return;

        case schema_context::project_items:
        case schema_context::item_children:
            fail("array elements must be Project item objects");
            return;

        case schema_context::predefines:
            fail("predefines array elements must be objects");
            return;
        }
    }

    void value(json_value_view value) override {
        if (failed()) {
            return;
        }

        if (stack.empty()) {
            fail(
                "Project configuration root must be an object");
            return;
        }

        auto& current =
            stack.back();

        switch (current.context) {
        case schema_context::root:
            read_root_value(current, value);
            return;

        case schema_context::predefine:
            read_predefine_value(current, value);
            return;

        case schema_context::item:
            read_item_value(current, value);
            return;

        case schema_context::preprocessor:
        case schema_context::predefines:
        case schema_context::project_items:
        case schema_context::item_children:
            fail("unexpected scalar in Project configuration");
            return;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return !failed() &&
               root_started &&
               root_completed &&
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
            "preprocessor",
        };

        if (current.index >= keys.size()) {
            fail("Project root contains extra field");
            return;
        }

        if (key != keys[current.index]) {
            fail(
                "Project root fields must be ordered: "
                "version, name, project, preprocessor");
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
                    "header/source/assign/project item requires path after type");
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

    [[nodiscard]] static bool valid_identifier(
        std::string_view value) noexcept {

        if (value.empty()) {
            return false;
        }

        const auto first =
            static_cast<unsigned char>(value.front());

        if (!((first >= 'A' && first <= 'Z') ||
              (first >= 'a' && first <= 'z') ||
              first == '_')) {

            return false;
        }

        for (std::size_t index = 1;
             index < value.size();
             ++index) {

            const auto ch =
                static_cast<unsigned char>(value[index]);

            if (!((ch >= 'A' && ch <= 'Z') ||
                  (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') ||
                  ch == '_')) {

                return false;
            }
        }

        return true;
    }

    void read_predefine_value(
        frame& current,
        json_value_view value) {

        if (preprocessor.predefines.empty()) {
            fail("internal predefine state mismatch");
            return;
        }

        auto& predefine =
            preprocessor.predefines.back();

        if (current.index == 0) {
            if (!value.get(predefine.name) ||
                !valid_identifier(predefine.name)) {

                fail(
                    "predefine.name must be a valid identifier");
                return;
            }

            ++current.index;
            return;
        }

        if (current.index == 1) {
            if (!value.get(predefine.replacement) ||
                !valid_identifier(predefine.replacement)) {

                fail(
                    "predefine.replacement must be a valid identifier");
                return;
            }

            ++current.index;
            return;
        }

        fail("predefine contains extra scalar");
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
            } else if (type == "assign") {
                current.type = item_type::assign;
            } else if (type == "project") {
                current.type = item_type::project;
            } else {
                fail(
                    "Project item type must be "
                    "group, header, source, assign, or project");
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

                std::filesystem::path item_path;

                const auto converted =
                    filesystem_path_from_utf8(
                        path,
                        item_path);

                if (converted != filesystem_path_result::success) {
                    fail(
                        converted == filesystem_path_result::invalid_utf8
                            ? "Project item path must be valid UTF-8"
                            : "Cannot convert Project item path to native filesystem path");
                    return;
                }

                project_configuration_path_type path_type =
                    project_configuration_path_type::relative;

                if (item_path.is_absolute()) {
                    path_type =
                        project_configuration_path_type::absolute;
                } else if (
                    item_path.has_root_name() ||
                    item_path.has_root_directory()) {

                    fail(
                        "Project item path must be relative or fully absolute; "
                        "drive-relative/rooted forms are not supported");
                    return;
                }

                file_kind kind = file_kind::project;

                switch (current.type) {
                case item_type::header:
                    kind = file_kind::header;
                    break;
                case item_type::source:
                    kind = file_kind::source;
                    break;
                case item_type::assign:
                    kind = file_kind::assign;
                    break;
                case item_type::project:
                    kind = file_kind::project;
                    break;
                case item_type::none:
                case item_type::group:
                    fail("internal Project item kind mismatch");
                    return;
                }

                dependencies.push_back({
                    kind,
                    path_type,
                    item_path.lexically_normal(),
                });
            }

            current.stage =
                item_stage::complete;
            return;

        case item_stage::complete:
            fail("Project item contains extra scalar");
            return;
        }
    }

    std::vector<project_configuration_dependency>& dependencies;
    project_preprocessor_configuration& preprocessor;
    std::vector<frame> stack;

    std::size_t current_offset = 0;
    std::size_t current_length = 0;
    bool root_started = false;
    bool root_completed = false;
    schema_error error_value;
};

} // namespace

server_status read_project_configuration(
    std::string& bytes,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::vector<project_configuration_dependency>& dependencies,
    project_preprocessor_configuration& preprocessor) {

    dependencies.clear();
    preprocessor.predefines.clear();

    project_configuration_handler handler{
        dependencies,
        preprocessor};

    const auto parsed =
        parse_json(
            std::string_view{bytes},
            handler);

    if (!parsed.ok()) {
        dependencies.clear();
        preprocessor.predefines.clear();

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
        dependencies.clear();
        preprocessor.predefines.clear();

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
