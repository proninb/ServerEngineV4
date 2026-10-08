cmake_minimum_required(VERSION 3.25)

project(ServerEngineV4
    VERSION 4.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SERVER_ENGINE_ENABLE_IPO
    "Enable interprocedural optimization for Release and RelWithDebInfo"
    ON
)

if(SERVER_ENGINE_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT SERVER_ENGINE_IPO_SUPPORTED
        OUTPUT SERVER_ENGINE_IPO_ERROR LANGUAGES CXX)
    if(SERVER_ENGINE_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    else()
        message(WARNING "IPO unavailable; using ordinary optimization: ${SERVER_ENGINE_IPO_ERROR}")
    endif()
endif()

add_executable(ServerEngineV4
    server_engine/main.cpp
    server_engine/server.cpp
    server_engine/server_policy.cpp
    server_engine/filesystem_path.cpp
    server_engine/read_only_file_mapping.cpp
    server_engine/writable_file_mapping.cpp
    server_engine/fixed_shared_memory.cpp
    server_engine/diagnostics/diagnostic_collection.cpp
    server_engine/diagnostics/diagnostic_formatter.cpp
    server_engine/diagnostics/diagnostic_source_cache.cpp
    server_engine/json/json_parser.cpp

    server_engine/configuration/server_configuration_loader.cpp

    server_engine/license/server_license.cpp
    server_engine/license/server_license_loader.cpp
    server_engine/license/server_lease.cpp
    server_engine/license/server_lease_loader.cpp

    server_engine/server_identity/server_identity.cpp

    server_engine/authentication/authentication.cpp
    server_engine/authentication/microsoft_entra.cpp
    server_engine/authentication/entra_jwt.cpp

    server_engine/communication/request_queue.cpp
    server_engine/communication/communication.cpp
    server_engine/communication/session/client_session.cpp
    server_engine/communication/connection/outbound_channel.cpp
    server_engine/communication/connection/subscription_registration.cpp
    server_engine/communication/connection/communication_connection.cpp
    server_engine/communication/protocol/json_protocol.cpp
    server_engine/communication/tcp/tcp_endpoint.cpp

    server_engine/communication/console/server_console.cpp

    server_engine/project/project.cpp
    server_engine/project/ic/ic_catalog.cpp
    server_engine/project/abi/abi_layout.cpp
    server_engine/project/shm/shm_layout.cpp
    server_engine/project/shm/shm_type_batch.cpp
    server_engine/project/shm/shm_runtime_v2.cpp
    server_engine/project/runtime/runtime_binding.cpp
    server_engine/project/runtime/runtime_query.cpp
    server_engine/project/runtime/runtime_ic.cpp
    server_engine/project/runtime/runtime_ic_codec.cpp
    server_engine/project/runtime/runtime_ic_snapshot.cpp
    server_engine/project/runtime/runtime_ic_reset.cpp
    server_engine/project/runtime/project_runtime.cpp
    server_engine/project/string/string_table.cpp
    server_engine/project/semantic/identity.cpp
    server_engine/project/graph/graph.cpp
    server_engine/project/graph/graph_delta.cpp
    server_engine/project/source/source_map.cpp
    server_engine/project/parser/parser.cpp
    server_engine/project/parser/header_parser_v2.cpp
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
    server_engine/project/frontend/lexical_symbols_v2.cpp
    server_engine/project/frontend/prepared_include_v2.cpp
    server_engine/project/frontend/preprocessor_v2.cpp
    server_engine/project/frontend/lexical_generation.cpp
    server_engine/project/file/file_identity.cpp
    server_engine/project/project_configuration_manifest.cpp
    server_engine/project/project_configuration_manifest_store.cpp
    server_engine/project/project_path.cpp
    server_engine/project/persistence/project_artifact.cpp
    server_engine/project/persistence/compiled_project.cpp
    server_engine/project/persistence/runtime_project.cpp
    server_engine/project/persistence/compiled_project_graph_write.cpp
    server_engine/project/persistence/compiled_project_build.cpp
    server_engine/project/persistence/source_save.cpp
    server_engine/project/persistence/database.cpp
    server_engine/project/persistence/source_save_view.cpp
    server_engine/project/file/file_context.cpp
    server_engine/project/project_load.cpp
    server_engine/project/project_publish.cpp
    server_engine/project/project_build.cpp
    server_engine/project/project_rebuild.cpp
    server_engine/project/project_full_construction.cpp
    server_engine/project/project_configuration_loader.cpp
)

if(WIN32)
    target_sources(ServerEngineV4 PRIVATE
        server_engine/server_identity/microsoft_entra_server_identity_windows.cpp
        server_engine/authentication/crypto/rsa_sha256_verifier_windows.cpp
        server_engine/communication/console/console_input_windows.cpp
        server_engine/project/construction/execution_lanes_windows.cpp
    )

    target_link_libraries(ServerEngineV4 PRIVATE bcrypt crypt32 ncrypt winhttp ws2_32)
else()
    find_package(Threads REQUIRED)

    target_sources(ServerEngineV4 PRIVATE
        server_engine/server_identity/microsoft_entra_server_identity_unsupported.cpp
        server_engine/authentication/crypto/rsa_sha256_verifier_unsupported.cpp
        server_engine/communication/console/console_input_posix.cpp
        server_engine/project/construction/execution_lanes_posix.cpp
    )

    target_link_libraries(ServerEngineV4 PRIVATE Threads::Threads)

    if(NOT APPLE)
        target_link_libraries(ServerEngineV4 PRIVATE rt)
    endif()
endif()

target_include_directories(ServerEngineV4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
)

if(MSVC)
    target_compile_options(ServerEngineV4 PRIVATE /W4 /permissive-)
else()
    target_compile_options(ServerEngineV4 PRIVATE -Wall -Wextra -Wpedantic)
endif()


option(
    SERVER_ENGINE_BUILD_BENCHMARKS
    "Build ServerEngineV4 benchmark executables"
    OFF
)

if(SERVER_ENGINE_BUILD_BENCHMARKS)
    get_target_property(
        SERVER_ENGINE_V4_BENCHMARK_SOURCES
        ServerEngineV4
        SOURCES
    )

    list(
        FILTER SERVER_ENGINE_V4_BENCHMARK_SOURCES
        EXCLUDE REGEX "server_engine/main\\.cpp$"
    )

    # PARSER-V2-PERF-01: opt-in standalone benchmark, no production instrumentation.
    add_executable(
        ServerEngineV4ParserV2Benchmark
        benchmarks/parser_v2_benchmark.cpp
        ${SERVER_ENGINE_V4_BENCHMARK_SOURCES}
    )
    target_include_directories(
        ServerEngineV4ParserV2Benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )
    if(WIN32)
        target_link_libraries(
            ServerEngineV4ParserV2Benchmark PRIVATE
            Psapi bcrypt crypt32 ncrypt winhttp ws2_32
        )
    else()
        target_link_libraries(
            ServerEngineV4ParserV2Benchmark PRIVATE Threads::Threads
        )
        if(NOT APPLE)
            target_link_libraries(ServerEngineV4ParserV2Benchmark PRIVATE rt)
        endif()
    endif()
    if(MSVC)
        target_compile_options(ServerEngineV4ParserV2Benchmark PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ParserV2Benchmark PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(
        ServerEngineV4PublishBenchmark
        benchmarks/publish_benchmark.cpp
        ${SERVER_ENGINE_V4_BENCHMARK_SOURCES}
    )

    # PARSER-V2-PERF-02: real project.json Header-only profile + OLD/V2 oracle.
    # Benchmark target is opt-in and shares production sources without changes.
    add_executable(
        ServerEngineV4ParserV2ProjectBenchmark
        benchmarks/parser_v2_project_benchmark.cpp
        ${SERVER_ENGINE_V4_BENCHMARK_SOURCES}
    )
    target_include_directories(ServerEngineV4ParserV2ProjectBenchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine)
    if(WIN32)
        target_link_libraries(ServerEngineV4ParserV2ProjectBenchmark PRIVATE
            Psapi bcrypt crypt32 ncrypt winhttp ws2_32)
    else()
        target_link_libraries(ServerEngineV4ParserV2ProjectBenchmark PRIVATE
            Threads::Threads)
        if(NOT APPLE)
            target_link_libraries(ServerEngineV4ParserV2ProjectBenchmark PRIVATE rt)
        endif()
    endif()
    if(MSVC)
        target_compile_options(ServerEngineV4ParserV2ProjectBenchmark PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ParserV2ProjectBenchmark PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()


    target_include_directories(
        ServerEngineV4PublishBenchmark
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(WIN32)
        target_link_libraries(
            ServerEngineV4PublishBenchmark
            PRIVATE
            Psapi
            bcrypt
            crypt32
            ncrypt
            winhttp
            ws2_32
        )
    else()
        target_link_libraries(
            ServerEngineV4PublishBenchmark
            PRIVATE
            Threads::Threads
        )

        if(NOT APPLE)
            target_link_libraries(
                ServerEngineV4PublishBenchmark
                PRIVATE
                rt
            )
        endif()
    endif()

    if(MSVC)
        target_compile_options(
            ServerEngineV4PublishBenchmark
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            ServerEngineV4PublishBenchmark
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    add_executable(
        ServerEngineV4ShmLayoutBenchmark
        benchmarks/shm_layout_benchmark.cpp
        server_engine/project/runtime/runtime_layout.cpp
        server_engine/project/shm/shm_materializer.cpp
        server_engine/project/shm/shm_sparse.cpp
        server_engine/project/shm/shm_type_area.cpp
        server_engine/project/shm/shm_hybrid_profile.cpp
        server_engine/project/shm/shm_hybrid_04m.cpp
        ${SERVER_ENGINE_V4_BENCHMARK_SOURCES}
    )

    target_include_directories(
        ServerEngineV4ShmLayoutBenchmark
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    add_executable(
        ServerEngineV4RuntimeBenchmark
        benchmarks/runtime_benchmark.cpp
        server_engine/project/runtime/runtime_layout.cpp
        server_engine/project/runtime/fixed_direct_materializer.cpp
        ${SERVER_ENGINE_V4_BENCHMARK_SOURCES}
    )
# RUNTIME-LOOKUP-PROFILE-V1: benchmark-only target. Production code is unchanged.
    add_executable(
        ServerEngineV4RuntimeLookupBenchmark
        benchmarks/runtime_lookup_benchmark.cpp
        server_engine/project/runtime/runtime_layout.cpp
        server_engine/project/runtime/fixed_direct_materializer.cpp
        ${SERVER_ENGINE_V4_BENCHMARK_SOURCES}
    )

    target_include_directories(
        ServerEngineV4RuntimeBenchmark
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )
    target_include_directories(
        ServerEngineV4RuntimeLookupBenchmark
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(WIN32)
        target_link_libraries(
            ServerEngineV4ShmLayoutBenchmark
            PRIVATE
            Psapi
            bcrypt
            crypt32
            ncrypt
            winhttp
            ws2_32
        )

        target_link_libraries(
            ServerEngineV4RuntimeBenchmark
            PRIVATE
            Psapi
            bcrypt
            crypt32
            ncrypt
            winhttp
            ws2_32
        )
        target_link_libraries(
            ServerEngineV4RuntimeLookupBenchmark
            PRIVATE
            Psapi
            bcrypt
            crypt32
            ncrypt
            winhttp
            ws2_32
        )
    else()
        target_link_libraries(
            ServerEngineV4ShmLayoutBenchmark
            PRIVATE
            Threads::Threads
        )

        if(NOT APPLE)
            target_link_libraries(
                ServerEngineV4ShmLayoutBenchmark
                PRIVATE
                rt
            )
        endif()

        target_link_libraries(
            ServerEngineV4RuntimeBenchmark
            PRIVATE
            Threads::Threads
        )
        target_link_libraries(
            ServerEngineV4RuntimeLookupBenchmark
            PRIVATE
            Threads::Threads
        )

        if(NOT APPLE)
            target_link_libraries(
                ServerEngineV4RuntimeBenchmark
                PRIVATE
                rt
            )
            target_link_libraries(
                ServerEngineV4RuntimeLookupBenchmark
                PRIVATE
                rt
            )
        endif()
    endif()

    if(MSVC)
        target_compile_options(
            ServerEngineV4ShmLayoutBenchmark
            PRIVATE
            /W4
            /permissive-
        )

        target_compile_options(
            ServerEngineV4RuntimeBenchmark
            PRIVATE
            /W4
            /permissive-
        )
        target_compile_options(
            ServerEngineV4RuntimeLookupBenchmark
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            ServerEngineV4ShmLayoutBenchmark
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

        target_compile_options(
            ServerEngineV4RuntimeBenchmark
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        target_compile_options(
            ServerEngineV4RuntimeLookupBenchmark
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

    endif()
endif()

if(MSVC)
    add_executable(
        ServerEngineV4MsvcClassAbiOracle
        EXCLUDE_FROM_ALL
        tests/msvc_class_abi_oracle.cpp
    )

    target_compile_options(
        ServerEngineV4MsvcClassAbiOracle
        PRIVATE
        /W4
        /permissive-
    )
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(ServerEngineV4Crc64Tests tests/crc64_tests.cpp)
    target_include_directories(ServerEngineV4Crc64Tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine)
    if(MSVC)
        target_compile_options(ServerEngineV4Crc64Tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4Crc64Tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME crc64 COMMAND ServerEngineV4Crc64Tests)

    add_executable(ServerEngineV4FixedSharedMemoryTests
        tests/fixed_shared_memory_tests.cpp
        server_engine/fixed_shared_memory.cpp
    )

    target_include_directories(ServerEngineV4FixedSharedMemoryTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(
            ServerEngineV4FixedSharedMemoryTests
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            ServerEngineV4FixedSharedMemoryTests
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )

        if(NOT APPLE)
            target_link_libraries(
                ServerEngineV4FixedSharedMemoryTests
                PRIVATE
                rt
            )
        endif()
    endif()

    add_test(
        NAME fixed_shared_memory
        COMMAND ServerEngineV4FixedSharedMemoryTests
    )

    add_executable(ServerEngineV4ServerConfigurationTests
        tests/server_configuration_tests.cpp
        server_engine/configuration/server_configuration_loader.cpp
        server_engine/json/json_parser.cpp
        server_engine/filesystem_path.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4ServerConfigurationTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(
            ServerEngineV4ServerConfigurationTests
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            ServerEngineV4ServerConfigurationTests
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    add_test(
        NAME server_configuration
        COMMAND ServerEngineV4ServerConfigurationTests
    )

    add_executable(ServerEngineV4ServerLicenseTests
        tests/server_license_tests.cpp
        server_engine/license/server_license.cpp
        server_engine/license/server_license_loader.cpp
        server_engine/json/json_parser.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4ServerLicenseTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4ServerLicenseTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ServerLicenseTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME server_license
        COMMAND ServerEngineV4ServerLicenseTests
    )

    add_executable(ServerEngineV4ServerLeaseTests
        tests/server_lease_tests.cpp
        server_engine/license/server_license.cpp
        server_engine/license/server_lease.cpp
        server_engine/license/server_lease_loader.cpp
        server_engine/json/json_parser.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4ServerLeaseTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4ServerLeaseTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ServerLeaseTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME server_lease COMMAND ServerEngineV4ServerLeaseTests)

    add_executable(ServerEngineV4ServerPolicyTests
        tests/server_policy_tests.cpp
        server_engine/server_policy.cpp
    )

    target_include_directories(ServerEngineV4ServerPolicyTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4ServerPolicyTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ServerPolicyTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME server_policy
        COMMAND ServerEngineV4ServerPolicyTests
    )

    add_executable(ServerEngineV4ServerIdentityTests
        tests/server_identity_tests.cpp
        server_engine/server_identity/server_identity.cpp
    )

    if(WIN32)
        target_sources(ServerEngineV4ServerIdentityTests PRIVATE
            server_engine/server_identity/microsoft_entra_server_identity_windows.cpp
            server_engine/json/json_parser.cpp
        )
        target_link_libraries(ServerEngineV4ServerIdentityTests PRIVATE
            bcrypt
            crypt32
            ncrypt
            winhttp
        )
    else()
        target_sources(ServerEngineV4ServerIdentityTests PRIVATE
            server_engine/server_identity/microsoft_entra_server_identity_unsupported.cpp
        )
    endif()

    target_include_directories(ServerEngineV4ServerIdentityTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4ServerIdentityTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ServerIdentityTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME server_identity COMMAND ServerEngineV4ServerIdentityTests)

    add_executable(ServerEngineV4AuthenticationTests
        tests/authentication_tests.cpp
        server_engine/authentication/authentication.cpp
        server_engine/authentication/microsoft_entra.cpp
        server_engine/authentication/entra_jwt.cpp
        server_engine/json/json_parser.cpp
    )

    if(WIN32)
        target_sources(ServerEngineV4AuthenticationTests PRIVATE
            server_engine/authentication/crypto/rsa_sha256_verifier_windows.cpp
        )
        target_link_libraries(ServerEngineV4AuthenticationTests PRIVATE bcrypt)
    else()
        target_sources(ServerEngineV4AuthenticationTests PRIVATE
            server_engine/authentication/crypto/rsa_sha256_verifier_unsupported.cpp
        )
    endif()

    target_include_directories(ServerEngineV4AuthenticationTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine)

    if(MSVC)
        target_compile_options(ServerEngineV4AuthenticationTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4AuthenticationTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME authentication COMMAND ServerEngineV4AuthenticationTests)

    add_executable(ServerEngineV4ClientSessionTests
        tests/client_session_tests.cpp
        server_engine/communication/session/client_session.cpp
    )

    target_include_directories(ServerEngineV4ClientSessionTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4ClientSessionTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ClientSessionTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME client_session COMMAND ServerEngineV4ClientSessionTests)

    add_executable(ServerEngineV4CommunicationProtocolTests
        tests/communication_protocol_tests.cpp
    )

    target_include_directories(ServerEngineV4CommunicationProtocolTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4CommunicationProtocolTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4CommunicationProtocolTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME communication_protocol COMMAND ServerEngineV4CommunicationProtocolTests)

    add_executable(ServerEngineV4CommunicationConnectionCoreTests
        tests/communication_connection_core_tests.cpp
        server_engine/communication/session/client_session.cpp
        server_engine/communication/connection/outbound_channel.cpp
        server_engine/communication/connection/subscription_registration.cpp
        server_engine/communication/connection/communication_connection.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4CommunicationConnectionCoreTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4CommunicationConnectionCoreTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4CommunicationConnectionCoreTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME communication_connection_core COMMAND ServerEngineV4CommunicationConnectionCoreTests)

    add_executable(ServerEngineV4CommunicationControlTests
        tests/communication_control_tests.cpp
        server_engine/communication/request_queue.cpp
        server_engine/communication/session/client_session.cpp
        server_engine/communication/connection/outbound_channel.cpp
        server_engine/communication/connection/subscription_registration.cpp
        server_engine/communication/connection/communication_connection.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4CommunicationControlTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4CommunicationControlTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4CommunicationControlTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME communication_control COMMAND ServerEngineV4CommunicationControlTests)

    add_executable(ServerEngineV4CommunicationRegressionTests
        tests/communication_regression_tests.cpp
        server_engine/communication/request_queue.cpp
        server_engine/communication/communication.cpp
        server_engine/communication/session/client_session.cpp
        server_engine/communication/connection/outbound_channel.cpp
        server_engine/communication/connection/subscription_registration.cpp
        server_engine/communication/connection/communication_connection.cpp
        server_engine/communication/protocol/json_protocol.cpp
        server_engine/communication/tcp/tcp_endpoint.cpp
        server_engine/communication/console/server_console.cpp
        server_engine/filesystem_path.cpp
        server_engine/json/json_parser.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_formatter.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    if(WIN32)
        target_sources(ServerEngineV4CommunicationRegressionTests PRIVATE
            server_engine/communication/console/console_input_windows.cpp
        )
        target_link_libraries(ServerEngineV4CommunicationRegressionTests PRIVATE
            ws2_32
        )
    else()
        target_sources(ServerEngineV4CommunicationRegressionTests PRIVATE
            server_engine/communication/console/console_input_posix.cpp
        )
        target_link_libraries(ServerEngineV4CommunicationRegressionTests PRIVATE
            Threads::Threads
        )
    endif()

    target_include_directories(ServerEngineV4CommunicationRegressionTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4CommunicationRegressionTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4CommunicationRegressionTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME communication_regressions
        COMMAND ServerEngineV4CommunicationRegressionTests
    )

    add_executable(ServerEngineV4TcpJsonProtocolTests
        tests/tcp_json_protocol_tests.cpp
        server_engine/communication/protocol/json_protocol.cpp
        server_engine/filesystem_path.cpp
        server_engine/json/json_parser.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4TcpJsonProtocolTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4TcpJsonProtocolTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4TcpJsonProtocolTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME tcp_json_protocol COMMAND ServerEngineV4TcpJsonProtocolTests)

    add_executable(ServerEngineV4IcCatalogTests
        tests/ic_catalog_tests.cpp
        server_engine/project/ic/ic_catalog.cpp
        server_engine/filesystem_path.cpp
        server_engine/json/json_parser.cpp
    )

    target_include_directories(ServerEngineV4IcCatalogTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4IcCatalogTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4IcCatalogTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME ic_catalog
        COMMAND ServerEngineV4IcCatalogTests
    )

    add_executable(ServerEngineV4ProjectRuntimeConfigurationTests
        tests/project_runtime_configuration_tests.cpp
        server_engine/project/project_configuration_loader.cpp
        server_engine/filesystem_path.cpp
        server_engine/json/json_parser.cpp
        server_engine/diagnostics/diagnostic_collection.cpp
        server_engine/diagnostics/diagnostic_source_cache.cpp
    )

    target_include_directories(ServerEngineV4ProjectRuntimeConfigurationTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4ProjectRuntimeConfigurationTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4ProjectRuntimeConfigurationTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME project_runtime_configuration
        COMMAND ServerEngineV4ProjectRuntimeConfigurationTests
    )

    add_executable(ServerEngineV4EntraJwtTests
        tests/entra_jwt_tests.cpp
        server_engine/authentication/microsoft_entra.cpp
        server_engine/authentication/entra_jwt.cpp
        server_engine/json/json_parser.cpp
    )

    if(WIN32)
        target_sources(ServerEngineV4EntraJwtTests PRIVATE
            server_engine/authentication/crypto/rsa_sha256_verifier_windows.cpp
        )
        target_link_libraries(ServerEngineV4EntraJwtTests PRIVATE bcrypt)
    else()
        target_sources(ServerEngineV4EntraJwtTests PRIVATE
            server_engine/authentication/crypto/rsa_sha256_verifier_unsupported.cpp
        )
    endif()

    target_include_directories(ServerEngineV4EntraJwtTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4EntraJwtTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4EntraJwtTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME entra_jwt COMMAND ServerEngineV4EntraJwtTests)

    add_executable(ServerEngineV4Tests
        tests/compiled_project_tests.cpp
        server_engine/project/shm/shm_layout.cpp
        server_engine/project/shm/shm_type_batch.cpp
        server_engine/project/shm/shm_runtime_v2.cpp
        server_engine/project/runtime/runtime_binding.cpp
        server_engine/project/runtime/runtime_layout.cpp
        server_engine/project/runtime/runtime_query.cpp
        server_engine/project/runtime/runtime_ic.cpp
        server_engine/project/runtime/runtime_ic_codec.cpp
        server_engine/project/runtime/runtime_ic_snapshot.cpp
        server_engine/project/runtime/runtime_ic_reset.cpp
        server_engine/project/runtime/fixed_direct_materializer.cpp
        server_engine/project/abi/abi_layout.cpp
        server_engine/read_only_file_mapping.cpp
        server_engine/writable_file_mapping.cpp
        server_engine/project/string/string_table.cpp
        server_engine/project/semantic/identity.cpp
        server_engine/project/graph/graph.cpp
        server_engine/project/graph/graph_delta.cpp
        server_engine/project/assign/assign_table.cpp
        server_engine/project/file/file_identity.cpp
        server_engine/project/persistence/project_artifact.cpp
        server_engine/project/persistence/compiled_project.cpp
        server_engine/project/persistence/runtime_project.cpp
        server_engine/project/persistence/compiled_project_graph_write.cpp
        server_engine/filesystem_path.cpp
        server_engine/project/project_path.cpp
        server_engine/project/persistence/source_save_view.cpp
        server_engine/project/file/file_context.cpp
        server_engine/project/source/source_map.cpp
    )

    target_include_directories(ServerEngineV4Tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(WIN32)
        target_sources(ServerEngineV4Tests PRIVATE
            server_engine/project/construction/execution_lanes_windows.cpp
        )
    else()
        target_sources(ServerEngineV4Tests PRIVATE
            server_engine/project/construction/execution_lanes_posix.cpp
        )

        target_link_libraries(ServerEngineV4Tests PRIVATE Threads::Threads)
    endif()

    if(MSVC)
        target_compile_options(ServerEngineV4Tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4Tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME compiled_project
        COMMAND ServerEngineV4Tests
    )

    add_executable(ServerEngineV4SourceSaveTests
        tests/source_save_tests.cpp
        server_engine/filesystem_path.cpp
        server_engine/read_only_file_mapping.cpp
        server_engine/writable_file_mapping.cpp
        server_engine/project/project_path.cpp
        server_engine/project/file/file_identity.cpp
        server_engine/project/persistence/source_save_view.cpp
        server_engine/project/file/file_context.cpp
        server_engine/project/persistence/source_save.cpp
        server_engine/project/graph/graph.cpp
        server_engine/project/graph/graph_delta.cpp
        server_engine/project/source/source_map.cpp
        server_engine/project/string/string_table.cpp
        server_engine/project/semantic/identity.cpp
        server_engine/project/assign/assign_table.cpp
        server_engine/project/persistence/compiled_project.cpp
        server_engine/project/persistence/compiled_project_build.cpp
    )

    target_include_directories(ServerEngineV4SourceSaveTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(WIN32)
        target_sources(ServerEngineV4SourceSaveTests PRIVATE
            server_engine/project/construction/execution_lanes_windows.cpp
        )
    else()
        target_sources(ServerEngineV4SourceSaveTests PRIVATE
            server_engine/project/construction/execution_lanes_posix.cpp
        )

        target_link_libraries(ServerEngineV4SourceSaveTests PRIVATE Threads::Threads)
    endif()

    if(MSVC)
        target_compile_options(ServerEngineV4SourceSaveTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4SourceSaveTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME source_save
        COMMAND ServerEngineV4SourceSaveTests
    )

    add_executable(ServerEngineV4DatabaseTests
        tests/database_tests.cpp
        server_engine/filesystem_path.cpp
        server_engine/read_only_file_mapping.cpp
        server_engine/writable_file_mapping.cpp
        server_engine/project/project_path.cpp
        server_engine/project/frontend/lexical_stream.cpp
        server_engine/project/frontend/lexer.cpp
        server_engine/project/frontend/lexical_generation.cpp
        server_engine/project/frontend/source_discovery.cpp
        server_engine/project/file/file_identity.cpp
        server_engine/project/persistence/source_save_view.cpp
        server_engine/project/file/file_context.cpp
        server_engine/project/persistence/database.cpp
    )

    target_include_directories(ServerEngineV4DatabaseTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(WIN32)
        target_sources(ServerEngineV4DatabaseTests PRIVATE
            server_engine/project/construction/execution_lanes_windows.cpp
        )
    else()
        target_sources(ServerEngineV4DatabaseTests PRIVATE
            server_engine/project/construction/execution_lanes_posix.cpp
        )
        target_link_libraries(ServerEngineV4DatabaseTests PRIVATE Threads::Threads)
    endif()

    if(MSVC)
        target_compile_options(ServerEngineV4DatabaseTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4DatabaseTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME database
        COMMAND ServerEngineV4DatabaseTests
    )


    add_executable(ServerEngineV4SourceMapTests
        tests/source_map_tests.cpp
        server_engine/project/source/source_map.cpp
        server_engine/filesystem_path.cpp
        server_engine/project/project_path.cpp
        server_engine/project/file/file_identity.cpp
        server_engine/project/persistence/source_save_view.cpp
        server_engine/project/file/file_context.cpp
        server_engine/project/string/string_table.cpp
        server_engine/project/semantic/identity.cpp
        server_engine/project/graph/graph.cpp
        server_engine/project/assign/assign_table.cpp
        server_engine/project/persistence/compiled_project.cpp
    )

    target_include_directories(ServerEngineV4SourceMapTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(MSVC)
        target_compile_options(ServerEngineV4SourceMapTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4SourceMapTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME source_map
        COMMAND ServerEngineV4SourceMapTests
    )

    add_executable(ServerEngineV4FrontendTests
        tests/frontend_regression_tests.cpp
        server_engine/filesystem_path.cpp
        server_engine/project/project_path.cpp
        server_engine/project/string/string_table.cpp
        server_engine/project/semantic/identity.cpp
        server_engine/project/graph/graph.cpp
        server_engine/project/graph/graph_delta.cpp
        server_engine/project/source/source_map.cpp
        server_engine/project/assign/assign_table.cpp
        server_engine/project/persistence/compiled_project.cpp
        server_engine/project/parser/parser.cpp
        server_engine/project/parser/header_parser_v2.cpp
        server_engine/project/preprocessor/preprocessor.cpp
        server_engine/project/frontend/frontend_input.cpp
        server_engine/project/frontend/semantic_input.cpp
        server_engine/project/frontend/directive_decoder.cpp
        server_engine/project/frontend/directive_executor.cpp
        server_engine/project/frontend/lexical_stream.cpp
        server_engine/project/frontend/lexer.cpp
        server_engine/project/frontend/lexical_symbols_v2.cpp
        server_engine/project/frontend/prepared_include_v2.cpp
        server_engine/project/frontend/preprocessor_v2.cpp
        server_engine/project/frontend/lexical_generation.cpp
        server_engine/project/file/file_identity.cpp
        server_engine/project/persistence/source_save_view.cpp
        server_engine/project/file/file_context.cpp
    )

    target_include_directories(ServerEngineV4FrontendTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server_engine
    )

    if(WIN32)
        target_sources(ServerEngineV4FrontendTests PRIVATE
            server_engine/project/construction/execution_lanes_windows.cpp)
    else()
        target_sources(ServerEngineV4FrontendTests PRIVATE
            server_engine/project/construction/execution_lanes_posix.cpp)
        target_link_libraries(ServerEngineV4FrontendTests PRIVATE Threads::Threads)
    endif()

    if(MSVC)
        target_compile_options(ServerEngineV4FrontendTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ServerEngineV4FrontendTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME frontend_regressions
        COMMAND ServerEngineV4FrontendTests
    )
endif()

