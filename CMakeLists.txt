cmake_minimum_required(VERSION 3.25)

project(ServerEngineV4
    VERSION 4.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ServerEngineV4
    server_engine/main.cpp
    server_engine/server.cpp
    server_engine/filesystem_path.cpp
    server_engine/read_only_file_mapping.cpp
    server_engine/diagnostics/diagnostic_collection.cpp
    server_engine/diagnostics/diagnostic_formatter.cpp
    server_engine/diagnostics/diagnostic_source_cache.cpp
    server_engine/json/json_parser.cpp

    server_engine/configuration/server_configuration_loader.cpp

    server_engine/communication/command_queue.cpp
    server_engine/communication/communication.cpp

    server_engine/communication/console/server_console.cpp

    server_engine/project/project.cpp
    server_engine/project/abi/abi_layout.cpp
    server_engine/project/string/string_table.cpp
    server_engine/project/semantic/identity.cpp
    server_engine/project/graph/graph.cpp
    server_engine/project/parser/parser.cpp
    server_engine/project/preprocessor/preprocessor.cpp
    server_engine/project/frontend/frontend_input.cpp
    server_engine/project/frontend/semantic_input.cpp
    server_engine/project/frontend/directive_decoder.cpp
    server_engine/project/frontend/directive_executor.cpp
    server_engine/project/frontend/source_discovery.cpp
    server_engine/project/assign/assign_table.cpp
    server_engine/project/assign/assign_input.cpp
    server_engine/project/frontend/lexical_stream.cpp
    server_engine/project/frontend/lexer.cpp
    server_engine/project/frontend/lexical_generation.cpp
    server_engine/project/file/file_identity.cpp
    server_engine/project/project_configuration_manifest.cpp
    server_engine/project/project_configuration_manifest_store.cpp
    server_engine/project/project_path.cpp
    server_engine/project/persistence/project_artifact.cpp
    server_engine/project/persistence/compiled_project.cpp
    server_engine/project/persistence/source_save.cpp
    server_engine/project/persistence/database.cpp
    server_engine/project/file/file_context.cpp
    server_engine/project/project_load.cpp
    server_engine/project/project_build.cpp
    server_engine/project/project_rebuild.cpp
    server_engine/project/project_configuration_loader.cpp
)

if(WIN32)
    target_sources(ServerEngineV4 PRIVATE
        server_engine/communication/console/console_input_windows.cpp
        server_engine/project/construction/execution_lanes_windows.cpp
    )
else()
    find_package(Threads REQUIRED)

    target_sources(ServerEngineV4 PRIVATE
        server_engine/communication/console/console_input_posix.cpp
        server_engine/project/construction/execution_lanes_posix.cpp
    )

    target_link_libraries(ServerEngineV4 PRIVATE Threads::Threads)
endif()

target_include_directories(ServerEngineV4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
)

if(MSVC)
    target_compile_options(ServerEngineV4 PRIVATE /W4 /permissive-)
else()
    target_compile_options(ServerEngineV4 PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(ServerEngineV4Tests
        tests/compiled_project_tests.cpp
        server_engine/project/string/string_table.cpp
        server_engine/project/semantic/identity.cpp
        server_engine/project/graph/graph.cpp
        server_engine/project/assign/assign_table.cpp
        server_engine/project/file/file_identity.cpp
        server_engine/project/persistence/project_artifact.cpp
        server_engine/project/persistence/compiled_project.cpp
    )

    target_include_directories(ServerEngineV4Tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4Tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4Tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME compiled_project
        COMMAND ServerEngineV4Tests
    )
endif()
