cmake_minimum_required(VERSION 3.20)
project(RE4HeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# cameraunlock-core shared library (static) with REFramework utilities
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_REFRAMEWORK ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern CACHE PATH "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Plugin sources
add_library(RE4HeadTracking SHARED
    src/pch.cpp
    src/plugin_main.cpp
    src/core/mod.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/camera/camera_hook.cpp
    src/camera/game_state_detector.cpp
)

target_precompile_headers(RE4HeadTracking PRIVATE src/pch.h)

set_target_properties(RE4HeadTracking PROPERTIES
    OUTPUT_NAME "RE4HeadTracking"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(RE4HeadTracking PRIVATE cameraunlock cameraunlock_reframework ws2_32)

target_include_directories(RE4HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
)

# Unit tests for pure repo-local logic (no REFramework / Windows runtime deps).
# Off by default so the plugin and CI builds are unaffected; enable with
# -DRE4HT_BUILD_TESTS=ON.
option(RE4HT_BUILD_TESTS "Build RE4HeadTracking unit tests" OFF)
if(RE4HT_BUILD_TESTS)
    enable_testing()
    add_executable(re4ht_math_tests tests/math_types_tests.cpp)
    target_include_directories(re4ht_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
    )
    add_test(NAME re4ht_math_tests COMMAND re4ht_math_tests)
endif()
