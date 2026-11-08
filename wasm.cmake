# Emscripten / WebGPU backend

message(STATUS "Building campello_gpu for Emscripten (WebGPU)")

# Emscripten has no dynamic linking here, so the library is static (CMake
# would silently turn SHARED into STATIC anyway).
add_library(${PROJECT_NAME} STATIC
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

# Dawn's emdawnwebgpu port provides <webgpu/webgpu.h> (upstream webgpu.h)
# and its JS implementation; it replaces the legacy -sUSE_WEBGPU binding,
# which Emscripten deprecated and removed. Requires Emscripten >= 4.0.10.
# The flag is needed when compiling (headers) and linking, by this library
# and by anything that links it, hence PUBLIC.
#
# ASYNCIFY backs the blocking calls (device creation, Buffer/Texture::download,
# waitForIdle, getGPUExecutionTime) that yield to the browser event loop.
# validationErrorCount() is defined by src/webgpu/device.cpp (uncaptured
# WebGPU errors), so src/pi/validation_diagnostics.cpp isn't built here.
target_compile_options(${PROJECT_NAME} PUBLIC
    --use-port=emdawnwebgpu
)
target_link_options(${PROJECT_NAME} PUBLIC
    --use-port=emdawnwebgpu
    -sASYNCIFY
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
)
