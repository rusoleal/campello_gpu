
find_package(Vulkan)
message(STATUS "Vulkan FOUND = ${Vulkan_FOUND}")
message(STATUS "Vulkan Include = ${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan Lib = ${Vulkan_LIBRARY}")

add_library(${PROJECT_NAME} SHARED
    src/pi/utils.cpp
    src/vulkan_android/device.cpp
    src/vulkan_android/adapter.cpp
    src/vulkan_android/buffer.cpp
    src/vulkan_android/texture.cpp
    src/vulkan_android/shader_module.cpp
    src/vulkan_android/render_pipeline.cpp
    src/vulkan_android/compute_pipeline.cpp
    src/vulkan_android/sampler.cpp
    src/vulkan_android/query_set.cpp
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