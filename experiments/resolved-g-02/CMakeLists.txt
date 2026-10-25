cmake_minimum_required(VERSION 3.25)
project(ResolvedG02 LANGUAGES CXX)
get_filename_component(ENGINE_ROOT "../.." ABSOLUTE)
set(SERVER_ENGINE_BUILD_BENCHMARKS ON CACHE BOOL "Build benchmark target" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build unrelated root tests" FORCE)
add_subdirectory("${ENGINE_ROOT}" engine EXCLUDE_FROM_ALL)
target_compile_definitions(ServerEngineV4PublishBenchmark PRIVATE CW_RESOLVED_G_01=1 CW_PATH_INTERN_02=1)
set_target_properties(ServerEngineV4PublishBenchmark PROPERTIES OUTPUT_NAME ResolvedGPath02)
add_custom_target(ResolvedGPath02 DEPENDS ServerEngineV4PublishBenchmark)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
enable_testing()
add_test(NAME resolved_g02_path_parity COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/validate.py" "$<TARGET_FILE:ServerEngineV4PublishBenchmark>"
    "${CMAKE_CURRENT_BINARY_DIR}/validation")
