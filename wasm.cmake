# Emscripten / WebGPU backend

message(STATUS "Building campello_gpu for Emscripten (WebGPU)")

add_library(${PROJECT_NAME} SHARED
    src/pi/pixel_format.cpp
    src/pi/utils.cpp
    src/webgpu/conversions.cpp
    src/webgpu/device.cpp
    src/webgpu/adapter.cpp
    src/webgpu/buffer.cpp
    src/webgpu/texture.cpp
    src/webgpu/texture_view.cpp
    src/webgpu/shader_module.cpp
    src/webgpu/render_pipeline.cpp
    src/webgpu/compute_pipeline.cpp
    src/webgpu/sampler.cpp
    src/webgpu/query_set.cpp
    src/webgpu/pipeline_layout.cpp
    src/webgpu/bind_group.cpp
    src/webgpu/bind_group_layout.cpp
    src/webgpu/command_buffer.cpp
    src/webgpu/command_encoder.cpp
    src/webgpu/render_pass_encoder.cpp
    src/webgpu/compute_pass_encoder.cpp
    src/webgpu/acceleration_structure.cpp
    src/webgpu/ray_tracing_pipeline.cpp
    src/webgpu/ray_tracing_pass_encoder.cpp
    src/webgpu/fence.cpp
)

target_link_options(${PROJECT_NAME} PRIVATE
    -sUSE_WEBGPU=1
    -sASYNCIFY
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
)
