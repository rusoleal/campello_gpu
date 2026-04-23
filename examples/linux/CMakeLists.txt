cmake_minimum_required(VERSION 3.22.1)
project(campello_linux_example)

set(CMAKE_CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# Find campello_gpu — when built as a subdirectory of the main project,
# the target is already available. When built standalone, use find_package.
# ---------------------------------------------------------------------------
if(NOT TARGET campello_gpu)
    find_package(campello_gpu REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Linux windowed example (raw X11).
# ---------------------------------------------------------------------------
add_executable(campello_linux_example main.cpp)
target_link_libraries(campello_linux_example
    campello_gpu
    X11
)
target_include_directories(campello_linux_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../inc
)
