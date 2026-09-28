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
    src/camera/gui_compensation.cpp
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

# Opt-in tests for the config: the committed HeadTracking.ini against the canonical table, the
# toggles' saves, and the differential test that holds the import of an old HeadTracking.ini to
# the published build that read it. OFF by default so neither the normal build nor the release
# packager ever drops a test binary into bin/. Enable with -DRE4HT_BUILD_TESTS=ON, or run
# 'pixi run test'.
option(RE4HT_BUILD_TESTS "Build RE4HeadTracking tests" OFF)
if(RE4HT_BUILD_TESTS)
    enable_testing()

    # Every test is named for its target and hangs off this aggregate, so run-tests.ps1 builds
    # them all with one --target and selects them all with one ctest filter.
    add_custom_target(re4ht_tests)

    function(re4ht_add_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(${name} PRIVATE cameraunlock cameraunlock_reframework)
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /WX /permissive-)
        endif()
        add_test(NAME ${name} COMMAND ${name})
        add_dependencies(re4ht_tests ${name})
    endfunction()

    # The matrix and quaternion primitives the GUI compensation takes from core's re_math.h.
    re4ht_add_test(re4ht_math_tests tests/camera_math_tests.cpp)

    # The committed HeadTracking.ini against the table, and the toggles' saves.
    # `--render-config <path>` writes the committed file instead (pixi run render-config).
    re4ht_add_test(re4ht_config_tests tests/config_tests.cpp)
    target_compile_definitions(re4ht_config_tests PRIVATE
        RE4HT_COMMITTED_CONFIG=L"${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")

    # The config differential test. The oracle is the reader of the dev pre-release at 3398b80,
    # the newest published build, copied byte for byte into tests/config_differential/oracle;
    # renaming its namespace lets it link beside core's. oracle_adapter.cpp copies its Config out
    # for the test.
    set(RE4HT_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(re4ht_config_oracle STATIC
        ${RE4HT_ORACLE_DIR}/src/core/config.cpp
        ${RE4HT_ORACLE_DIR}/src/core/logger.cpp
        tests/config_differential/oracle_adapter.cpp
    )
    target_include_directories(re4ht_config_oracle PRIVATE
        ${RE4HT_ORACLE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(re4ht_config_oracle PRIVATE RE4HT=re4ht_oracle)
    target_link_libraries(re4ht_config_oracle PRIVATE cameraunlock)

    re4ht_add_test(re4ht_config_differential tests/config_differential/differential_tests.cpp)
    target_include_directories(re4ht_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(re4ht_config_differential PRIVATE
        RE4HT_DIFFERENTIAL_INPUTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/inputs"
        RE4HT_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")
    target_link_libraries(re4ht_config_differential PRIVATE re4ht_config_oracle)

    # Core's canonical config lint over the committed file and every file the differential test
    # migrated, which it writes beside itself under migrated\.
    add_test(NAME re4ht_config_lint COMMAND node
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:re4ht_config_differential>/migrated)
    set_tests_properties(re4ht_config_lint PROPERTIES DEPENDS re4ht_config_differential)

    # The oracle, the inputs and the core sources the import and the oracle compile, against the
    # hashes tests/config_differential/frozen.tsv records.
    add_test(NAME re4ht_config_frozen COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/check-frozen.ps1)
    set_tests_properties(re4ht_config_differential re4ht_config_lint re4ht_config_frozen PROPERTIES LABELS differential)
endif()
