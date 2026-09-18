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
)

if(MSVC)
    target_compile_options(ServerEngineV4 PRIVATE /W4 /permissive-)
else()
    target_compile_options(ServerEngineV4 PRIVATE -Wall -Wextra -Wpedantic)
endif()
