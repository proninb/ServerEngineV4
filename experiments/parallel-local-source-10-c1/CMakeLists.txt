cmake_minimum_required(VERSION 3.25)
project(PLS10C1 LANGUAGES CXX)
get_filename_component(ENGINE_ROOT "../.." ABSOLUTE)
add_executable(DirectCommitFeasibility10C1
 feasibility.cpp
 "${ENGINE_ROOT}/server_engine/project/source/source_map.cpp"
 "${ENGINE_ROOT}/server_engine/filesystem_path.cpp"
 "${ENGINE_ROOT}/server_engine/project/project_path.cpp"
 "${ENGINE_ROOT}/server_engine/project/file/file_identity.cpp"
 "${ENGINE_ROOT}/server_engine/project/persistence/source_save_view.cpp"
 "${ENGINE_ROOT}/server_engine/project/file/file_context.cpp"
 "${ENGINE_ROOT}/server_engine/project/string/string_table.cpp"
 "${ENGINE_ROOT}/server_engine/project/semantic/identity.cpp"
 "${ENGINE_ROOT}/server_engine/project/graph/graph.cpp"
 "${ENGINE_ROOT}/server_engine/project/assign/assign_table.cpp"
 "${ENGINE_ROOT}/server_engine/project/persistence/compiled_project.cpp")
target_compile_features(DirectCommitFeasibility10C1 PRIVATE cxx_std_20)
target_include_directories(DirectCommitFeasibility10C1 PRIVATE "${ENGINE_ROOT}/server_engine")
if(MSVC)
 target_compile_options(DirectCommitFeasibility10C1 PRIVATE /W4 /permissive-)
 set_property(TARGET DirectCommitFeasibility10C1 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
enable_testing()
add_test(NAME PLS10C1_feasibility COMMAND DirectCommitFeasibility10C1)
