cmake_minimum_required(VERSION 3.22.1)

project(campello_gpu VERSION 0.0.13)

set(CMAKE_CXX_STANDARD 17)

message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

include_directories(inc)

configure_file(src/campello_gpu_config.h.in campello_gpu_config.h)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    include(android.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    include(macos.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    include(ios.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    include(windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(linux.cmake)
endif()
