/*
 * Server configuration loader.
 *
 * JSON syntax is handled by the generic server_engine/json subsystem.
 * This file owns only the server.json schema and conversion to
 * server_configuration.
 */
#include "server_configuration_loader.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../filesystem_path.hpp"
#include "../shared_memory_name.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"
#include "../json/json_parser.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

enum class schema_context : std::uint8_t {
    root,
    settings,
    abi,
    shm,
    files,
    communication,
    endpoints,
    endpoint,
    project,
    logging,
    telemetry,
    telemetry_subsystems,
};

enum class schema_field : std::uint8_t {
    none = 0,
    version,
    settings,
    abi,
    target,
    pack,
    shm,
    mode,
    fixed_base_address,
    files,
    manifest,
    source_save,
    database,
    compiled,
    communication,
    project,
    logging,
    telemetry,
    endpoints,
    name,
    transport,
    protocol,
    address,
    port,
    path,
    startup,
    level,
    console,
    file,
    subsystems,
};

enum class schema_failure : std::uint8_t {
    none = 0,
    unknown_property,
    duplicate_property,
    wrong_type,
    missing_required_field,
    invalid_value,
    duplicate_endpoint_name,
    unsupported_version,
    invalid_structure,
};

struct schema_error {
    schema_failure code = schema_failure::none;
    std::size_t offset = 0;
    std::size_t length = 0;
    std::string detail;
};

struct schema_frame {
    schema_context context = schema_context::root;
    schema_field field = schema_field::none;
    std::uint64_t seen = 0;
};

[[nodiscard]] constexpr std::uint64_t field_bit(
    schema_field field) noexcept {

    const auto value =
        static_cast<std::uint8_t>(field);

    return value == 0
        ? 0
        : (std::uint64_t{1} << value);
}

[[nodiscard]] bool valid_log_level(
    std::string_view value) noexcept {

    return value == "trace" ||
           value == "debug" ||
           value == "info" ||
           value == "warning" ||
           value == "error" ||
           value == "critical";
}

[[nodiscard]] constexpr std::string_view schema_context_name(
    schema_context context) noexcept {

    switch (context) {
    case schema_context::root: return "root";
    case schema_context::settings: return "settings";
    case schema_context::abi: return "settings.abi";
    case schema_context::shm: return "settings.shm";
    case schema_context::files: return "settings.files";
    case schema_context::communication: return "communication";
    case schema_context::endpoints: return "communication.endpoints";
    case schema_context::endpoint: return "communication.endpoints[]";
    case schema_context::project: return "project";
    case schema_context::logging: return "logging";
    case schema_context::telemetry: return "telemetry";
    case schema_context::telemetry_subsystems: return "telemetry.subsystems";
    }

    return "server configuration";
}

class server_configuration_handler final
    : public json_event_handler {
public:
    explicit server_configuration_handler(
        server_configuration& configuration)
        : configuration(configuration) {

        stack.reserve(8);
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
                    schema_failure::invalid_structure,
                    "server configuration must contain exactly one root object");
                return;
            }

            root_started = true;

            stack.push_back({
                schema_context::root,
                schema_field::none,
                0,
            });
            return;
        }

        const auto parent =
            stack.back();

        if (parent.context ==
                schema_context::root &&
            parent.field ==
                schema_field::settings) {

            stack.back().field =
                schema_field::none;

            stack.push_back({
                schema_context::settings,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::settings &&
            parent.field ==
                schema_field::abi) {

            stack.back().field =
                schema_field::none;

            stack.push_back({
                schema_context::abi,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::settings &&
            parent.field ==
                schema_field::shm) {

            stack.back().field =
                schema_field::none;

            stack.push_back({
                schema_context::shm,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::settings &&
            parent.field ==
                schema_field::files) {

            stack.back().field =
                schema_field::none;

            stack.push_back({
                schema_context::files,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::root &&
            parent.field ==
                schema_field::communication) {

            stack.back().field =
                schema_field::none;

            stack.push_back({
                schema_context::communication,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::root &&
            parent.field ==
                schema_field::project) {

            stack.back().field =
                schema_field::none;

            configuration.project.emplace();

            stack.push_back({
                schema_context::project,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::root &&
            parent.field ==
                schema_field::logging) {

            stack.back().field =
                schema_field::none;

            stack.push_back({
                schema_context::logging,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::root &&
            parent.field ==
                schema_field::telemetry) {

            stack.back().field =
                schema_field::none;

            configuration.telemetry.emplace();

            stack.push_back({
                schema_context::telemetry,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
            schema_context::endpoints) {

            configuration.communication.endpoints.emplace_back();

            stack.push_back({
                schema_context::endpoint,
                schema_field::none,
                0,
            });
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "unexpected JSON object for server configuration field");
    }

    void object_end() override {
        if (failed() ||
            stack.empty()) {
            return;
        }

        auto frame =
            stack.back();

        switch (frame.context) {
        case schema_context::root:
            if (!seen(frame, schema_field::version) ||
                !seen(frame, schema_field::settings) ||
                !seen(frame, schema_field::communication)) {

                fail(
                    schema_failure::missing_required_field,
                    "server configuration requires version, settings, and communication");
                return;
            }

            root_completed = true;
            break;

        case schema_context::settings:
            if (!seen(frame, schema_field::abi) ||
                !seen(frame, schema_field::shm) ||
                !seen(frame, schema_field::files)) {

                fail(
                    schema_failure::missing_required_field,
                    "settings requires abi, shm, and files");
                return;
            }
            break;

        case schema_context::abi:
            if (!seen(frame, schema_field::target) ||
                !seen(frame, schema_field::pack)) {

                fail(
                    schema_failure::missing_required_field,
                    "settings.abi requires target and pack");
                return;
            }
            break;

        case schema_context::shm:
            if (!seen(frame, schema_field::mode) ||
                !seen(frame, schema_field::name)) {

                fail(
                    schema_failure::missing_required_field,
                    "settings.shm requires mode and name");
                return;
            }

            if (configuration.settings.shm.mode ==
                    shm_runtime_mode::fixed_direct) {

                if (!seen(
                        frame,
                        schema_field::fixed_base_address)) {

                    fail(
                        schema_failure::missing_required_field,
                        "settings.shm.fixed_direct requires fixed_base_address");
                    return;
                }
            }
            else if (seen(
                         frame,
                         schema_field::fixed_base_address)) {

                fail(
                    schema_failure::invalid_value,
                    "settings.shm.fixed_base_address is valid only for fixed_direct");
                return;
            }
            break;

        case schema_context::files:
            if (!validate_files(frame)) {
                return;
            }
            break;

        case schema_context::communication:
            if (!seen(frame, schema_field::endpoints)) {
                fail(
                    schema_failure::missing_required_field,
                    "communication requires endpoints");
                return;
            }
            break;

        case schema_context::endpoint:
            if (!validate_endpoint(frame)) {
                return;
            }
            break;

        case schema_context::project:
            if (!seen(frame, schema_field::path)) {
                fail(
                    schema_failure::missing_required_field,
                    "project requires path");
                return;
            }
            break;

        case schema_context::logging:
        case schema_context::telemetry:
            break;

        case schema_context::endpoints:
        case schema_context::telemetry_subsystems:
            fail(
                schema_failure::invalid_structure,
                "internal schema context mismatch");
            return;
        }

        stack.pop_back();
    }

    void array_begin() override {
        if (failed()) {
            return;
        }

        if (stack.empty()) {
            fail(
                schema_failure::invalid_structure,
                "server configuration root must be an object");
            return;
        }

        auto& parent =
            stack.back();

        if (parent.context ==
                schema_context::communication &&
            parent.field ==
                schema_field::endpoints) {

            parent.field =
                schema_field::none;

            stack.push_back({
                schema_context::endpoints,
                schema_field::none,
                0,
            });
            return;
        }

        if (parent.context ==
                schema_context::telemetry &&
            parent.field ==
                schema_field::subsystems) {

            parent.field =
                schema_field::none;

            stack.push_back({
                schema_context::telemetry_subsystems,
                schema_field::none,
                0,
            });
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "unexpected JSON array for server configuration field");
    }

    void array_end() override {
        if (failed() ||
            stack.empty()) {
            return;
        }

        const auto context =
            stack.back().context;

        if (context ==
            schema_context::endpoints) {

            if (configuration.communication.endpoints.empty()) {
                fail(
                    schema_failure::invalid_value,
                    "communication.endpoints cannot be empty");
                return;
            }

            stack.pop_back();
            return;
        }

        if (context ==
            schema_context::telemetry_subsystems) {

            stack.pop_back();
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "unexpected JSON array end");
    }

    void key(
        std::string_view key) override {

        if (failed() ||
            stack.empty()) {
            return;
        }

        auto& frame =
            stack.back();

        const auto field =
            resolve_field(
                frame.context,
                key);

        if (field ==
            schema_field::none) {

            fail(
                schema_failure::unknown_property,
                "unknown property in " +
                    std::string(schema_context_name(frame.context)) +
                    ": " +
                    std::string(key));
            return;
        }

        const auto bit =
            field_bit(field);

        if ((frame.seen & bit) != 0) {
            fail(
                schema_failure::duplicate_property,
                "duplicate property in " +
                    std::string(schema_context_name(frame.context)) +
                    ": " +
                    std::string(key));
            return;
        }

        frame.seen |= bit;
        frame.field = field;
    }

    void value(
        json_value_view value) override {

        if (failed()) {
            return;
        }

        if (stack.empty()) {
            fail(
                schema_failure::invalid_structure,
                "server configuration root must be an object");
            return;
        }

        auto& frame =
            stack.back();

        if (frame.context ==
            schema_context::telemetry_subsystems) {

            std::string subsystem;

            if (!value.get(subsystem) ||
                subsystem.empty()) {

                fail(
                    schema_failure::wrong_type,
                    "telemetry.subsystems[] must be a non-empty string");
                return;
            }

            configuration.telemetry->subsystems.push_back(
                std::move(subsystem));
            return;
        }

        const auto field =
            frame.field;

        if (field ==
            schema_field::none) {

            fail(
                schema_failure::invalid_structure,
                "scalar value is not associated with a configuration field");
            return;
        }

        switch (frame.context) {
        case schema_context::root:
            read_root(field, value);
            break;

        case schema_context::abi:
            read_abi(field, value);
            break;

        case schema_context::shm:
            read_shm(field, value);
            break;

        case schema_context::files:
            read_files(field, value);
            break;

        case schema_context::endpoint:
            read_endpoint(field, value);
            break;

        case schema_context::project:
            read_project(field, value);
            break;

        case schema_context::logging:
            read_logging(field, value);
            break;

        case schema_context::telemetry:
            read_telemetry(field, value);
            break;

        case schema_context::settings:
        case schema_context::communication:
        case schema_context::endpoints:
        case schema_context::telemetry_subsystems:
            fail(
                schema_failure::invalid_structure,
                "unexpected scalar value in server configuration");
            break;
        }

        frame.field =
            schema_field::none;
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
        return error_value.code !=
            schema_failure::none;
    }

    void fail(
        schema_failure code,
        std::string detail) {

        if (failed()) {
            return;
        }

        error_value = {
            code,
            current_offset,
            current_length,
            std::move(detail),
        };
    }

    [[nodiscard]] static bool seen(
        const schema_frame& frame,
        schema_field field) noexcept {

        return (frame.seen &
                field_bit(field)) != 0;
    }

    [[nodiscard]] static schema_field resolve_field(
        schema_context context,
        std::string_view key) noexcept {

        switch (context) {
        case schema_context::root:
            if (key == "version") return schema_field::version;
            if (key == "settings") return schema_field::settings;
            if (key == "communication") return schema_field::communication;
            if (key == "project") return schema_field::project;
            if (key == "logging") return schema_field::logging;
            if (key == "telemetry") return schema_field::telemetry;
            break;

        case schema_context::settings:
            if (key == "abi") return schema_field::abi;
            if (key == "shm") return schema_field::shm;
            if (key == "files") return schema_field::files;
            break;

        case schema_context::abi:
            if (key == "target") return schema_field::target;
            if (key == "pack") return schema_field::pack;
            break;

        case schema_context::shm:
            if (key == "mode") return schema_field::mode;
            if (key == "name") return schema_field::name;
            if (key == "fixed_base_address") {
                return schema_field::fixed_base_address;
            }
            break;

        case schema_context::files:
            if (key == "manifest") return schema_field::manifest;
            if (key == "source_save") return schema_field::source_save;
            if (key == "database") return schema_field::database;
            if (key == "compiled") return schema_field::compiled;
            break;

        case schema_context::communication:
            if (key == "endpoints") return schema_field::endpoints;
            break;

        case schema_context::endpoint:
            if (key == "name") return schema_field::name;
            if (key == "transport") return schema_field::transport;
            if (key == "protocol") return schema_field::protocol;
            if (key == "address") return schema_field::address;
            if (key == "port") return schema_field::port;
            break;

        case schema_context::project:
            if (key == "path") return schema_field::path;
            if (key == "startup") return schema_field::startup;
            break;

        case schema_context::logging:
            if (key == "level") return schema_field::level;
            if (key == "console") return schema_field::console;
            if (key == "file") return schema_field::file;
            break;

        case schema_context::telemetry:
            if (key == "console") return schema_field::console;
            if (key == "subsystems") return schema_field::subsystems;
            break;

        case schema_context::endpoints:
        case schema_context::telemetry_subsystems:
            break;
        }

        return schema_field::none;
    }

    void read_root(
        schema_field field,
        json_value_view value) {

        if (field != schema_field::version) {
            fail(
                schema_failure::wrong_type,
                "server configuration field requires an object");
            return;
        }

        std::uint32_t version = 0;

        if (!value.get(version)) {
            fail(
                schema_failure::wrong_type,
                "version must be an unsigned integer");
            return;
        }

        if (version != 6) {
            fail(
                schema_failure::unsupported_version,
                "unsupported server configuration version; expected version 6");
            return;
        }

        configuration.version = version;
    }

    void read_abi(
        schema_field field,
        json_value_view value) {

        if (field == schema_field::target) {
            std::string target;

            if (!value.get(target)) {
                fail(
                    schema_failure::wrong_type,
                    "settings.abi.target must be a string");
                return;
            }

            if (target == "windows-x64") {
                configuration.settings.abi.target =
                    abi_target::windows_x64;
                return;
            }

            if (target == "posix-x64") {
                configuration.settings.abi.target =
                    abi_target::posix_x64;
                return;
            }

            fail(
                schema_failure::invalid_value,
                "settings.abi.target must be windows-x64 or posix-x64");
            return;
        }

        if (field == schema_field::pack) {
            std::uint32_t pack = 0;

            if (!value.get(pack) ||
                (pack != 1 &&
                 pack != 2 &&
                 pack != 4 &&
                 pack != 8 &&
                 pack != 16)) {

                fail(
                    schema_failure::invalid_value,
                    "settings.abi.pack must be 1, 2, 4, 8, or 16");
                return;
            }

            configuration.settings.abi.pack = pack;
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "invalid abi scalar field");
    }

    void read_shm(
        schema_field field,
        json_value_view value) {

        if (field == schema_field::mode) {
            std::string mode;

            if (!value.get(mode)) {
                fail(
                    schema_failure::wrong_type,
                    "settings.shm.mode must be a string");
                return;
            }

            if (mode == "fixed_direct") {
                configuration.settings.shm.mode =
                    shm_runtime_mode::fixed_direct;
                return;
            }

            if (mode == "relocatable_transfer") {
                configuration.settings.shm.mode =
                    shm_runtime_mode::relocatable_transfer;
                return;
            }

            fail(
                schema_failure::invalid_value,
                "settings.shm.mode must be fixed_direct or relocatable_transfer");
            return;
        }

        if (field ==
            schema_field::name) {

            std::string name;

            if (!value.get(name)) {
                fail(
                    schema_failure::wrong_type,
                    "settings.shm.name must be a string");
                return;
            }

            if (!valid_shared_memory_name(
                    name)) {

                fail(
                    schema_failure::invalid_value,
                    "settings.shm.name must contain 1-128 characters from A-Z, a-z, 0-9, '.', '_', or '-'");
                return;
            }

            configuration.settings.shm.name =
                std::move(name);

            return;
        }

        if (field ==
            schema_field::fixed_base_address) {

            std::string_view text;

            if (!value.get(text)) {
                fail(
                    schema_failure::wrong_type,
                    "settings.shm.fixed_base_address must be a hexadecimal string");
                return;
            }

            if (text.size() <= 2 ||
                text[0] != '0' ||
                (text[1] != 'x' &&
                 text[1] != 'X')) {

                fail(
                    schema_failure::invalid_value,
                    "settings.shm.fixed_base_address must use 0x hexadecimal notation");
                return;
            }

            std::uint64_t address = 0;

            const auto first =
                text.data() + 2;

            const auto last =
                text.data() +
                text.size();

            const auto parsed =
                std::from_chars(
                    first,
                    last,
                    address,
                    16);

            if (parsed.ec != std::errc{} ||
                parsed.ptr != last ||
                address == 0) {

                fail(
                    schema_failure::invalid_value,
                    "settings.shm.fixed_base_address must be a non-zero x64 address");
                return;
            }

            configuration.settings.shm.fixed_base_address =
                address;
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "invalid shm scalar field");
    }

    void read_files(
        schema_field field,
        json_value_view value) {

        std::string name;

        if (!value.get(name) ||
            name.empty()) {

            fail(
                schema_failure::wrong_type,
                "settings.files entries must be non-empty strings");
            return;
        }

        const char* field_name = nullptr;
        std::filesystem::path* output = nullptr;

        switch (field) {
        case schema_field::manifest:
            field_name = "manifest";
            output = &configuration.settings.files.manifest;
            break;

        case schema_field::source_save:
            field_name = "source_save";
            output = &configuration.settings.files.source_save;
            break;

        case schema_field::database:
            field_name = "database";
            output = &configuration.settings.files.database;
            break;

        case schema_field::compiled:
            field_name = "compiled";
            output = &configuration.settings.files.compiled;
            break;

        default:
            fail(
                schema_failure::invalid_structure,
                "invalid settings.files scalar field");
            return;
        }

        if (name == "." ||
            name == ".." ||
            name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) {

            fail(
                schema_failure::invalid_value,
                std::string("settings.files.") +
                    field_name +
                    " must be a single relative file name");
            return;
        }

        std::filesystem::path native_path;

        const auto converted =
            filesystem_path_from_utf8(
                name,
                native_path);

        if (converted !=
            filesystem_path_result::success) {

            fail(
                schema_failure::invalid_value,
                converted ==
                        filesystem_path_result::invalid_utf8
                    ? std::string("settings.files.") +
                        field_name +
                        " must be valid UTF-8"
                    : std::string("cannot convert settings.files.") +
                        field_name +
                        " to native filesystem path");
            return;
        }

        if (native_path.empty() ||
            native_path.is_absolute() ||
            native_path.has_root_name() ||
            native_path.has_root_directory() ||
            native_path.has_parent_path() ||
            native_path.filename() != native_path) {

            fail(
                schema_failure::invalid_value,
                std::string("settings.files.") +
                    field_name +
                    " must be a single relative file name");
            return;
        }

        *output =
            std::move(native_path);
    }

    void read_endpoint(
        schema_field field,
        json_value_view value) {

        auto& endpoint =
            configuration.communication.endpoints.back();

        switch (field) {
        case schema_field::name:
            if (!value.get(endpoint.name) ||
                endpoint.name.empty()) {
                fail(
                    schema_failure::wrong_type,
                    "endpoint.name must be a non-empty string");
            }
            return;

        case schema_field::transport: {
            std::string transport;

            if (!value.get(transport)) {
                fail(
                    schema_failure::wrong_type,
                    "endpoint.transport must be a string");
                return;
            }

            if (transport == "console") {
                endpoint.transport =
                    transport_kind::console;
                return;
            }

            if (transport == "tcp") {
                endpoint.transport =
                    transport_kind::tcp;
                return;
            }

            fail(
                schema_failure::invalid_value,
                "endpoint.transport must be console or tcp");
            return;
        }

        case schema_field::protocol:
            if (!value.get(endpoint.protocol)) {
                fail(
                    schema_failure::wrong_type,
                    "endpoint.protocol must be a string");
            }
            return;

        case schema_field::address:
            if (!value.get(endpoint.address)) {
                fail(
                    schema_failure::wrong_type,
                    "endpoint.address must be a string");
            }
            return;

        case schema_field::port:
            if (!value.get(endpoint.port) ||
                endpoint.port == 0) {
                fail(
                    schema_failure::invalid_value,
                    "endpoint.port must be integer 1..65535");
            }
            return;

        default:
            fail(
                schema_failure::invalid_structure,
                "invalid endpoint scalar field");
            return;
        }
    }

    void read_project(
        schema_field field,
        json_value_view value) {

        if (field ==
            schema_field::path) {

            std::string path;

            if (!value.get(path) ||
                path.empty()) {

                fail(
                    schema_failure::wrong_type,
                    "project.path must be a non-empty string");
                return;
            }

            std::filesystem::path native_path;

            const auto converted =
                filesystem_path_from_utf8(
                    path,
                    native_path);

            if (converted != filesystem_path_result::success) {
                fail(
                    schema_failure::invalid_value,
                    converted == filesystem_path_result::invalid_utf8
                        ? "project.path must be valid UTF-8"
                        : "cannot convert project.path to native filesystem path");
                return;
            }

            configuration.project->path =
                std::move(native_path);
            return;
        }

        if (field ==
            schema_field::startup) {

            std::string startup;

            if (!value.get(startup)) {
                fail(
                    schema_failure::wrong_type,
                    "project.startup must be a string");
                return;
            }

            if (startup == "load") {
                configuration.project->startup =
                    project_startup_mode::load;
                return;
            }

            if (startup == "publish") {
                configuration.project->startup =
                    project_startup_mode::publish;
                return;
            }

            if (startup == "rebuild") {
                configuration.project->startup =
                    project_startup_mode::rebuild;
                return;
            }

            fail(
                schema_failure::invalid_value,
                "project.startup must be load, publish, or rebuild");
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "invalid project scalar field");
    }

    void read_logging(
        schema_field field,
        json_value_view value) {

        switch (field) {
        case schema_field::level: {
            std::string level;

            if (!value.get(level)) {
                fail(
                    schema_failure::wrong_type,
                    "logging.level must be a string");
                return;
            }

            if (!valid_log_level(level)) {
                fail(
                    schema_failure::invalid_value,
                    "logging.level must be trace, debug, info, warning, error, or critical");
                return;
            }

            configuration.logging.level =
                std::move(level);
            return;
        }

        case schema_field::console:
            if (!value.get(
                    configuration.logging.console)) {

                fail(
                    schema_failure::wrong_type,
                    "logging.console must be boolean");
            }
            return;

        case schema_field::file: {
            std::string path;

            if (!value.get(path) ||
                path.empty()) {

                fail(
                    schema_failure::wrong_type,
                    "logging.file must be a non-empty string");
                return;
            }

            std::filesystem::path native_path;

            const auto converted =
                filesystem_path_from_utf8(
                    path,
                    native_path);

            if (converted != filesystem_path_result::success) {
                fail(
                    schema_failure::invalid_value,
                    converted == filesystem_path_result::invalid_utf8
                        ? "logging.file must be valid UTF-8"
                        : "cannot convert logging.file to native filesystem path");
                return;
            }

            configuration.logging.file =
                std::move(native_path);
            return;
        }

        default:
            fail(
                schema_failure::invalid_structure,
                "invalid logging scalar field");
            return;
        }
    }

    void read_telemetry(
        schema_field field,
        json_value_view value) {

        if (field ==
            schema_field::console) {

            if (!value.get(
                    configuration.telemetry->console)) {

                fail(
                    schema_failure::wrong_type,
                    "telemetry.console must be boolean");
            }
            return;
        }

        fail(
            schema_failure::invalid_structure,
            "telemetry.subsystems must be an array");
    }

    [[nodiscard]] bool validate_files(
        const schema_frame& frame) {

        if (!seen(frame, schema_field::manifest) ||
            !seen(frame, schema_field::source_save) ||
            !seen(frame, schema_field::database) ||
            !seen(frame, schema_field::compiled)) {

            fail(
                schema_failure::missing_required_field,
                "settings.files requires manifest, source_save, database, and compiled");
            return false;
        }

        const std::array<const std::filesystem::path*, 4> files{
            &configuration.settings.files.manifest,
            &configuration.settings.files.source_save,
            &configuration.settings.files.database,
            &configuration.settings.files.compiled,
        };

        constexpr std::array<std::string_view, 4> names{
            "manifest",
            "source_save",
            "database",
            "compiled",
        };

        std::array<filesystem_path_key, 4> keys;

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            if (make_filesystem_path_key(
                    *files[index],
                    keys[index]) !=
                filesystem_path_result::success) {

                fail(
                    schema_failure::invalid_value,
                    "cannot derive filesystem-equivalence key for settings.files." +
                        std::string(names[index]));
                return false;
            }
        }

        for (std::size_t left = 0;
             left < keys.size();
             ++left) {

            for (std::size_t right = left + 1;
                 right < keys.size();
                 ++right) {

                if (keys[left] ==
                    keys[right]) {

                    fail(
                        schema_failure::invalid_value,
                        "settings.files entries must use distinct file names under platform filesystem semantics");
                    return false;
                }
            }
        }

        return true;
    }

    [[nodiscard]] bool validate_endpoint(
        const schema_frame& frame) {

        if (!seen(frame, schema_field::name) ||
            !seen(frame, schema_field::transport)) {

            fail(
                schema_failure::missing_required_field,
                "endpoint requires name and transport");
            return false;
        }

        auto& endpoint =
            configuration.communication.endpoints.back();

        for (std::size_t i = 0;
             i + 1 <
                configuration.communication.endpoints.size();
             ++i) {

            if (configuration.communication.endpoints[i].name ==
                endpoint.name) {

                fail(
                    schema_failure::duplicate_endpoint_name,
                    "duplicate endpoint name: " +
                        endpoint.name);
                return false;
            }
        }

        if (endpoint.transport ==
            transport_kind::console) {

            if (!endpoint.protocol.empty() ||
                !endpoint.address.empty() ||
                endpoint.port != 0) {

                fail(
                    schema_failure::invalid_value,
                    "console endpoint must not define protocol, address, or port");
                return false;
            }

            return true;
        }

        if (endpoint.protocol != "json" ||
            endpoint.address.empty() ||
            endpoint.port == 0) {

            fail(
                schema_failure::invalid_value,
                "tcp endpoint requires protocol=json, non-empty address, and port");
            return false;
        }

        return true;
    }

    server_configuration& configuration;
    std::vector<schema_frame> stack;

    std::size_t current_offset = 0;
    std::size_t current_length = 0;
    bool root_started = false;
    bool root_completed = false;
    schema_error error_value;
};

[[nodiscard]] bool read_text_file(
    const std::filesystem::path& path,
    std::string& output) {

    std::ifstream stream(
        path,
        std::ios::binary);

    if (!stream) {
        return false;
    }

    output.assign(
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{});

    return stream.good() ||
           stream.eof();
}

} // namespace

server_status load_server_configuration(
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    server_configuration& configuration) {

    std::string text;

    if (!read_text_file(
            path,
            text)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::configuration_read_failed,
                operation)
                .file(path)
                .detail("Cannot read server configuration file")
                .build());

        return server_status::io_error;
    }

    const auto file_id =
        diagnostics.add_source(
            path,
            text);

    server_configuration candidate;
    server_configuration_handler handler{
        candidate,
    };

    const auto parsed =
        parse_json(
            text,
            handler);

    if (!parsed.ok()) {
        const auto& descriptor =
            parsed.code ==
                    json_error_code::internal_failure
                ? diagnostics::server_initialization_failed
                : diagnostics::configuration_invalid_json;

        diagnostics.emit(
            diagnostic(
                descriptor,
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

        return parsed.code ==
                json_error_code::internal_failure
            ? server_status::invalid_configuration
            : server_status::invalid_configuration;
    }

    if (!handler.valid()) {
        const auto& error =
            handler.error();

        const auto& descriptor =
            error.code ==
                    schema_failure::unsupported_version
                ? diagnostics::configuration_unsupported_version
                : diagnostics::configuration_invalid;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .location(
                    diagnostics.locate(
                        file_id,
                        error.offset,
                        error.length))
                .detail(
                    error.detail)
                .build());

        return server_status::invalid_configuration;
    }

    configuration =
        std::move(candidate);

    return server_status::success;
}

}
