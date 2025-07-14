
find_package(Vulkan)
message(STATUS "Vulkan FOUND = ${Vulkan_FOUND}")
message(STATUS "Vulkan Include = ${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan Lib = ${Vulkan_LIBRARY}")

include_directories(src/vulkan_android/inc)

add_library(${PROJECT_NAME} SHARED
    src/pi/utils.cpp
    src/vulkan_android/context.cpp
    src/vulkan_android/device.cpp
    src/vulkan_android/device_def.cpp
    src/vulkan_android/buffer.cpp
    src/vulkan_android/texture.cpp
    src/vulkan_android/view.cpp
    src/vulkan_android/shader_module.cpp
)

target_link_libraries(${PROJECT_NAME}
        Vulkan::Vulkan
        log)

target_include_directories (${PROJECT_NAME} PUBLIC 
                            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
                            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan_android/inc>"
                            )

target_include_directories(${PROJECT_NAME} PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           )