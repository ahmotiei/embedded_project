cmake_minimum_required(VERSION 3.16)
project(smart_guard_host_agent VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(JPEG REQUIRED)

add_executable(smart_guard_host_agent smart_guard_host_agent.c)

target_compile_options(
    smart_guard_host_agent
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wconversion
)

target_link_libraries(
    smart_guard_host_agent
    PRIVATE
    Threads::Threads
    JPEG::JPEG
)
