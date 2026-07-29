cmake_minimum_required(VERSION 3.16)
project(smart_guard_section4_watchdog VERSION 4.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(smart_guard_watchdog smart_guard_watchdog.c)

target_compile_options(
    smart_guard_watchdog
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
)
