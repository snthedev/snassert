cmake_minimum_required(VERSION 3.25)

project(snassert
    VERSION 1.0.0
    DESCRIPTION "snassert — assertion replacement with message & tips"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Library (header-only, INTERFACE)
# ---------------------------------------------------------------------------
add_library(snassert INTERFACE)
add_library(snassert::snassert ALIAS snassert)

target_compile_features(snassert INTERFACE cxx_std_23)

target_include_directories(snassert INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests (GoogleTest)" ON)

if(BUILD_TESTS)
    include(FetchContent)

    # Keep GoogleTest's own warnings out of our build
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(snassert_tests tests/snassert_tests.cpp)
    target_compile_features(snassert_tests PRIVATE cxx_std_23)
    target_link_libraries(snassert_tests PRIVATE snassert::snassert gtest_main)

    if(MSVC)
        target_compile_options(snassert_tests PRIVATE /W4 /permissive- /MP /utf-8)
    else()
        target_compile_options(snassert_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME snassert_tests COMMAND snassert_tests)
endif()
