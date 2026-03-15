
# Linux / Vulkan backend — not yet implemented.
# This stub defines a minimal campello_gpu target so that CMake can configure
# successfully and universal tests (which do not link against the library) can run.

message(STATUS "Linux/Vulkan backend not yet implemented — building placeholder library")

add_library(${PROJECT_NAME} STATIC
    src/pi/pixel_format.cpp
    src/pi/utils.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
    "${PROJECT_BINARY_DIR}"
)
