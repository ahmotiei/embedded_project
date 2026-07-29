cmake_minimum_required(VERSION 3.16)
project(smart_guard_web_section4 VERSION 4.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(MICROHTTPD REQUIRED IMPORTED_TARGET libmicrohttpd)
pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

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
    -Wsign-conversion
    -Wnull-dereference
)

target_link_libraries(
    smart_guard_web
    PRIVATE
    PkgConfig::MICROHTTPD
    PkgConfig::CJSON
    PkgConfig::SQLITE3
    Threads::Threads
)
