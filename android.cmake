
find_package(Vulkan)
message(STATUS "Vulkan FOUND = ${Vulkan_FOUND}")
message(STATUS "Vulkan Include = ${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan Lib = ${Vulkan_LIBRARY}")

add_library(${PROJECT_NAME} SHARED
    src/pi/device.cpp
    src/vulkan_android/context.cpp
    src/vulkan_android/device.cpp
    src/vulkan_android/device_def.cpp
    src/vulkan_android/buffer.cpp
    src/vulkan_android/texture.cpp
)

target_link_libraries(${PROJECT_NAME}
        Vulkan::Vulkan
        log)

target_include_directories (${PROJECT_NAME} PUBLIC 
                            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
                            )

target_include_directories(${PROJECT_NAME} PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           )