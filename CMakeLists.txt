cmake_minimum_required(VERSION 3.22.1)

project(campello_gpu VERSION 0.3.9)

set(CMAKE_CXX_STANDARD 20)

message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

include_directories(inc)

configure_file(src/campello_gpu_config.h.in campello_gpu_config.h)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    include(android.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    include(macos.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    include(ios.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    include(windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(linux.cmake)
endif()

# ---------------------------------------------------------------------------
# Tests
#
# BUILD_TESTS=ON            — universal tests (enums, utils, descriptors).
#                             No GPU or library linking required.
# BUILD_INTEGRATION_TESTS=ON — platform GPU tests. Requires a working device.
#                             Implies BUILD_TESTS=ON.
# ---------------------------------------------------------------------------
option(BUILD_TESTS             "Build unit tests (universal, no GPU)"    OFF)
option(BUILD_INTEGRATION_TESTS "Build GPU integration tests (per-platform)" OFF)

if(BUILD_INTEGRATION_TESTS)
    set(BUILD_TESTS ON)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Examples
#
# BUILD_EXAMPLES=ON — platform-specific example applications.
# ---------------------------------------------------------------------------
option(BUILD_EXAMPLES "Build example applications" OFF)

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_subdirectory(examples/windows/campello_example)
endif()
