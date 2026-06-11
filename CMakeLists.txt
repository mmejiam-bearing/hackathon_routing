cmake_minimum_required(VERSION 3.28)
project(maritime_router CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Global compile options
# ---------------------------------------------------------------------------
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -march=native
)

add_compile_options("$<$<CONFIG:Release>:-O3>")
add_compile_options("$<$<CONFIG:Debug>:-O0;-g;-fsanitize=address,undefined>")
add_link_options("$<$<CONFIG:Debug>:-fsanitize=address,undefined>")

# ---------------------------------------------------------------------------
# System dependencies
# ---------------------------------------------------------------------------
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# RoutingKit — fetched once, compiled as routingkit_static
# No CMake build system in RoutingKit; we wrap its sources ourselves.
# Pin to a specific commit: no versioned releases exist in this repo.
# To update: git ls-remote https://github.com/RoutingKit/RoutingKit HEAD
# ---------------------------------------------------------------------------
include(FetchContent)

set(ROUTINGKIT_COMMIT "54d49bb0cdea56dde182357522e4e86a03c57852")

FetchContent_Declare(
    routingkit
    GIT_REPOSITORY https://github.com/RoutingKit/RoutingKit.git
    GIT_TAG        ${ROUTINGKIT_COMMIT}
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(routingkit)

file(GLOB ROUTINGKIT_SOURCES "${routingkit_SOURCE_DIR}/src/*.cpp")

# OSM decoder defines ntohl, which collides with macOS SDK ntohl macro.
# It is not needed for CCH-based routing in this project.
list(FILTER ROUTINGKIT_SOURCES EXCLUDE REGEX ".*/osm_decoder\\.cpp$")

add_library(routingkit_static STATIC ${ROUTINGKIT_SOURCES})
target_include_directories(routingkit_static PUBLIC
    "${routingkit_SOURCE_DIR}/include"
)
target_link_libraries(routingkit_static PUBLIC ZLIB::ZLIB Threads::Threads)
target_compile_definitions(routingkit_static PUBLIC ROUTING_KIT_ASSUME_LITTLE_ENDIAN)
set_target_properties(routingkit_static PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(routingkit_static PRIVATE -w)   # suppress RoutingKit warnings

# ---------------------------------------------------------------------------
# GoogleTest — fetched once, used by tests/ only
# ---------------------------------------------------------------------------
set(GTEST_COMMIT "v1.14.0")

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        ${GTEST_COMMIT}
    GIT_SHALLOW    TRUE
)
# Prevent GTest from installing alongside the project
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Subdirectories — each owns its own target definition
# ---------------------------------------------------------------------------
add_subdirectory(lib)
add_subdirectory(graph_builder)
add_subdirectory(weather_etl)
add_subdirectory(router_server)
add_subdirectory(tests)
