cmake_minimum_required(VERSION 3.25)
project(PublishGraphWriter08 LANGUAGES CXX)
get_filename_component(ENGINE_ROOT "../.." ABSOLUTE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(SERVER_ENGINE_BUILD_BENCHMARKS ON CACHE BOOL "Core sources" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Own tests" FORCE)
add_subdirectory("${ENGINE_ROOT}" engine EXCLUDE_FROM_ALL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS generate.py writer.hpp runtime_probe.hpp publish_source.hpp publish_stats.hpp resident_probe.hpp
 "${ENGINE_ROOT}/server_engine/project/parser/parser.cpp" "${ENGINE_ROOT}/benchmarks/publish_benchmark.cpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-07/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-07/ownership.hpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-06/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-04/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-03/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-02/main.cpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-02/aliases.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" "${ENGINE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
get_target_property(sources ServerEngineV4PublishBenchmark SOURCES)
set(core)
foreach(source IN LISTS sources)
 if(NOT IS_ABSOLUTE "${source}")
  set(source "${ENGINE_ROOT}/${source}")
 endif()
 if(NOT source MATCHES "publish_benchmark[.]cpp$")
  list(APPEND core "${source}")
 endif()
endforeach()
add_library(PLS08Core STATIC ${core})
add_executable(PublishGraphWriter08 "${CMAKE_CURRENT_BINARY_DIR}/generated/main.cpp")
add_executable(PublishFinalState08 "${CMAKE_CURRENT_BINARY_DIR}/generated/publish_main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/generated/publish_parser.cpp")
get_directory_property(definitions DIRECTORY "${ENGINE_ROOT}" COMPILE_DEFINITIONS)
foreach(target PLS08Core PublishGraphWriter08 PublishFinalState08)
 target_compile_features(${target} PRIVATE cxx_std_20)
 target_include_directories(${target} PRIVATE "${ENGINE_ROOT}/server_engine")
 target_compile_definitions(${target} PRIVATE ${definitions})
 foreach(property COMPILE_OPTIONS INTERPROCEDURAL_OPTIMIZATION_RELEASE)
  get_target_property(value ServerEngineV4PublishBenchmark ${property})
  if(value AND NOT value MATCHES "NOTFOUND$")
   set_property(TARGET ${target} PROPERTY ${property} "${value}")
  endif()
 endforeach()
endforeach()
target_include_directories(PublishFinalState08 PRIVATE "${ENGINE_ROOT}/server_engine/project/parser" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(PublishFinalState08 PRIVATE PLS08Core psapi)
target_compile_definitions(PublishGraphWriter08 PRIVATE PLS03_PROBE=1)
target_link_libraries(PublishGraphWriter08 PRIVATE PLS08Core psapi)
foreach(property LINK_LIBRARIES LINK_OPTIONS)
 get_target_property(value ServerEngineV4PublishBenchmark ${property})
 if(value AND NOT value MATCHES "NOTFOUND$")
  set_property(TARGET PublishGraphWriter08 APPEND PROPERTY ${property} "${value}")
  set_property(TARGET PublishFinalState08 APPEND PROPERTY ${property} "${value}")
 endif()
endforeach()
enable_testing()
add_test(NAME PLS08_existing_semantics COMMAND "${Python3_EXECUTABLE}" "${ENGINE_ROOT}/experiments/parallel-local-source-02/test.py"
 "$<TARGET_FILE:PublishGraphWriter08>" "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(PLS08_existing_semantics PROPERTIES ENVIRONMENT "PLS03_MODE=1;PLS04_EARLY=1;PLS06_DENSE=1;PLS07_OWN=1;PLS07_VERIFY=1")
add_test(NAME PLS08_independent_runtime COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_writer.py"
 "$<TARGET_FILE:PublishGraphWriter08>" "${CMAKE_CURRENT_BINARY_DIR}/writer-fixtures")
add_test(NAME PLS08_gap_boundaries COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_gaps.py"
 "$<TARGET_FILE:PublishGraphWriter08>" "${CMAKE_CURRENT_BINARY_DIR}/gap-fixtures")
add_test(NAME PLS08_native_resident COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_resident.py"
 "$<TARGET_FILE:PublishFinalState08>" "${CMAKE_CURRENT_BINARY_DIR}/writer-fixtures")
set_tests_properties(PLS08_native_resident PROPERTIES DEPENDS PLS08_independent_runtime)
