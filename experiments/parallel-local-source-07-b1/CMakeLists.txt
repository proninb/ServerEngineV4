cmake_minimum_required(VERSION 3.25)
project(FinalWriterContract LANGUAGES CXX)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
get_filename_component(ENGINE_ROOT "../.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
 "${ENGINE_ROOT}/experiments/parallel-local-source-07/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-07/ownership.hpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-06/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-06/heads.hpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-04/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-04/memory.hpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-03/generate.py"
 "${ENGINE_ROOT}/experiments/parallel-local-source-02/aliases.hpp"
 "${ENGINE_ROOT}/experiments/parallel-local-source-02/bind.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${ENGINE_ROOT}/experiments/parallel-local-source-07/generate.py"
 "${ENGINE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
add_executable(FinalWriterContractTests contract_tests.cpp)
target_compile_features(FinalWriterContractTests PRIVATE cxx_std_20)
target_include_directories(FinalWriterContractTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${ENGINE_ROOT}/server_engine")
if(MSVC)
 target_compile_options(FinalWriterContractTests PRIVATE /W4 /permissive-)
 target_link_libraries(FinalWriterContractTests PRIVATE psapi)
endif()
enable_testing()
add_test(NAME FinalWriterContract COMMAND FinalWriterContractTests)
set(PLS07_EXE "${ENGINE_ROOT}/build/parallel-local-source-07/Release/ParallelLocalSource07.exe" CACHE FILEPATH "Existing PLS07 experiment executable")
if(EXISTS "${PLS07_EXE}")
 add_test(NAME FullSourceReplayPolicy COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_replay.py"
  "${PLS07_EXE}" "${CMAKE_CURRENT_BINARY_DIR}/replay-policy")
endif()
