cmake_minimum_required(VERSION 3.16)
project(smart_guard_section3_notifier VERSION 3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)
pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

add_executable(smart_guard_notifier smart_guard_notifier.c)

target_compile_options(
    smart_guard_notifier
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
    smart_guard_notifier
    PRIVATE
    PkgConfig::MOSQUITTO
    PkgConfig::CJSON
    PkgConfig::CURL
)
