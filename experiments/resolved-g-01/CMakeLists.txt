cmake_minimum_required(VERSION 3.25)
project(ResolvedG01 LANGUAGES CXX)
get_filename_component(ENGINE_ROOT "../.." ABSOLUTE)
set(SERVER_ENGINE_BUILD_BENCHMARKS ON CACHE BOOL "Build benchmark target" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build unrelated root tests" FORCE)
add_subdirectory("${ENGINE_ROOT}" engine EXCLUDE_FROM_ALL)
target_compile_definitions(ServerEngineV4PublishBenchmark PRIVATE CW_RESOLVED_G_01=1)
set_target_properties(ServerEngineV4PublishBenchmark PROPERTIES OUTPUT_NAME ResolvedGReplay01)
add_custom_target(ResolvedGReplay01 DEPENDS ServerEngineV4PublishBenchmark)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
enable_testing()
add_test(NAME resolved_g01_real_operations COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/validate.py" "$<TARGET_FILE:ServerEngineV4PublishBenchmark>"
    "${CMAKE_CURRENT_BINARY_DIR}/validation")
