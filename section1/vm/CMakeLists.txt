cmake_minimum_required(VERSION 3.16)
project(smart_guard_web VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(MICROHTTPD REQUIRED IMPORTED_TARGET libmicrohttpd)

add_executable(smart_guard_web smart_guard_web.c)

target_compile_options(
    smart_guard_web
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wconversion
)

target_link_libraries(
    smart_guard_web
    PRIVATE
    PkgConfig::MICROHTTPD
    Threads::Threads
)
