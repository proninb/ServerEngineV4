cmake_minimum_required(VERSION 3.25)
project(PublishDifferentialTrace01 LANGUAGES CXX)
get_filename_component(ENGINE_ROOT "../.." ABSOLUTE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" "${ENGINE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS generate.py ../evaluator-parallel-01/main.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${ENGINE_ROOT}/server_engine/project/string/string_table.cpp"
  "${ENGINE_ROOT}/server_engine/project/semantic/identity.cpp"
  "${ENGINE_ROOT}/server_engine/project/graph/graph.cpp"
  "${ENGINE_ROOT}/server_engine/project/frontend/preprocessor_v2.cpp"
  "${ENGINE_ROOT}/server_engine/project/parser/header_parser_v2.cpp"
  "${ENGINE_ROOT}/server_engine/project/parser/parser.cpp"
  "${ENGINE_ROOT}/server_engine/project/source/source_map.cpp")
file(GLOB objects "${ENGINE_ROOT}/build/semantic-bind-03/SemanticBind03Engine.dir/Release/*.obj")
if(NOT objects)
  message(FATAL_ERROR "Build SemanticBind03Engine first")
endif()
set(replaced string_table identity graph preprocessor_v2 header_parser_v2 source_map)
foreach(name IN LISTS replaced)
  list(FILTER objects EXCLUDE REGEX "/${name}\\.obj$")
  list(APPEND generated "${CMAKE_CURRENT_BINARY_DIR}/generated/${name}.cpp")
endforeach()
set_source_files_properties(${objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_executable(PublishDifferentialTrace01 main.cpp ${generated} ${objects})
target_include_directories(PublishDifferentialTrace01 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${ENGINE_ROOT}" "${ENGINE_ROOT}/server_engine")
target_compile_features(PublishDifferentialTrace01 PRIVATE cxx_std_20)
target_compile_options(PublishDifferentialTrace01 PRIVATE /W4 /permissive- /O2)
target_link_libraries(PublishDifferentialTrace01 PRIVATE Psapi bcrypt crypt32 ncrypt winhttp ws2_32)
set_property(TARGET PublishDifferentialTrace01 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
enable_testing()
add_test(NAME publish_differential_trace_01_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_coverage.py" "$<TARGET_FILE:PublishDifferentialTrace01>" "${CMAKE_CURRENT_BINARY_DIR}/coverage")
