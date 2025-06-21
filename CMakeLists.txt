cmake_minimum_required(VERSION 3.22.1)

project("campello_gpu" VERSION 0.0.6)

configure_file(src/vulkan_android/campello_cpu_config.h.in campello_cpu_config.h)

set(CMAKE_CXX_STANDARD 17)

include_directories(inc)

find_package(Vulkan)
message(STATUS "Vulkan FOUND = ${Vulkan_FOUND}")
message(STATUS "Vulkan Include = ${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan Lib = ${Vulkan_LIBRARY}")

message(STATUS ${PROJECT_NAME})

add_library(${PROJECT_NAME} SHARED
    src/vulkan_android/context.cpp
    src/vulkan_android/device.cpp
)

target_link_libraries(${PROJECT_NAME}
        Vulkan::Vulkan
        log)

target_include_directories (campello_gpu
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
)

target_include_directories(campello_gpu PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           )