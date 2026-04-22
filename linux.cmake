
# Linux / Vulkan backend
find_package(Vulkan)

if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan SDK not found — campello_gpu library will not be built on Linux. "
                    "Universal tests can still be built and run.")
    # Create a dummy interface target so campello_gpu::campello_gpu alias doesn't break
    # consumers that reference it unconditionally.
    add_library(${PROJECT_NAME} INTERFACE)
    target_include_directories(${PROJECT_NAME} INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
        "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
    )
    return()
endif()

message(STATUS "Vulkan FOUND = ${Vulkan_FOUND}")

add_library(${PROJECT_NAME} SHARED
    src/pi/pixel_format.cpp
    src/pi/utils.cpp
    src/vulkan_linux/device.cpp
    src/vulkan_linux/adapter.cpp
    src/vulkan_linux/buffer.cpp
    src/vulkan_linux/texture.cpp
    src/vulkan_linux/texture_view.cpp
    src/vulkan_linux/shader_module.cpp
    src/vulkan_linux/render_pipeline.cpp
    src/vulkan_linux/compute_pipeline.cpp
    src/vulkan_linux/sampler.cpp
    src/vulkan_linux/query_set.cpp
    src/vulkan_linux/pipeline_layout.cpp
    src/vulkan_linux/bind_group.cpp
    src/vulkan_linux/bind_group_layout.cpp
    src/vulkan_linux/command_buffer.cpp
    src/vulkan_linux/command_encoder.cpp
    src/vulkan_linux/render_pass_encoder.cpp
    src/vulkan_linux/compute_pass_encoder.cpp
    src/vulkan_linux/acceleration_structure.cpp
    src/vulkan_linux/ray_tracing_pipeline.cpp
    src/vulkan_linux/ray_tracing_pass_encoder.cpp
    src/vulkan_linux/fence.cpp
)

target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
)
