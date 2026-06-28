
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
    src/vulkan/device.cpp
    src/vulkan/adapter.cpp
    src/vulkan/buffer.cpp
    src/vulkan/texture.cpp
    src/vulkan/texture_view.cpp
    src/vulkan/shader_module.cpp
    src/vulkan/render_pipeline.cpp
    src/vulkan/compute_pipeline.cpp
    src/vulkan/sampler.cpp
    src/vulkan/query_set.cpp
    src/vulkan/pipeline_layout.cpp
    src/vulkan/bind_group.cpp
    src/vulkan/bind_group_layout.cpp
    src/vulkan/command_buffer.cpp
    src/vulkan/command_encoder.cpp
    src/vulkan/render_pass_encoder.cpp
    src/vulkan/compute_pass_encoder.cpp
    src/vulkan/acceleration_structure.cpp
    src/vulkan/ray_tracing_pipeline.cpp
    src/vulkan/ray_tracing_pass_encoder.cpp
    src/vulkan/fence.cpp
)

target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
)
