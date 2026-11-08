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
# Blocking calls (device creation, Buffer/Texture::download, waitForIdle,
# getGPUExecutionTime, pipeline validation) yield to the browser event loop.
# Two ways to do that:
#   ASYNCIFY (default): works in every WebGPU browser; rewrites the wasm,
#       making it larger and somewhat slower.
#   JSPI (CAMPELLO_GPU_WASM_JSPI=ON): the browser's JavaScript Promise
#       Integration; no rewriting, smaller and faster code, but needs a browser
#       with JSPI (Chrome/Edge 137+). Blocking calls must then run under main()
#       (or an export listed in -sJSPI_EXPORTS), not in a plain JS callback.
# validationErrorCount() is defined by src/webgpu/device.cpp (uncaptured
# WebGPU errors), so src/pi/validation_diagnostics.cpp isn't built here.
option(CAMPELLO_GPU_WASM_JSPI "Use JSPI instead of ASYNCIFY for blocking WebGPU calls" OFF)
if(CAMPELLO_GPU_WASM_JSPI)
    set(CAMPELLO_GPU_WASM_ASYNC_FLAG -sJSPI)
else()
    set(CAMPELLO_GPU_WASM_ASYNC_FLAG -sASYNCIFY)
endif()
target_compile_options(${PROJECT_NAME} PUBLIC
    --use-port=emdawnwebgpu
)
target_link_options(${PROJECT_NAME} PUBLIC
    --use-port=emdawnwebgpu
    ${CAMPELLO_GPU_WASM_ASYNC_FLAG}
)

# JavaScript interop helpers (Module.campelloGpu, see the file's header),
# linked into every target that links campello_gpu.
include(GNUInstallDirs)
target_link_options(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:--js-library=${CMAKE_CURRENT_SOURCE_DIR}/src/webgpu/campello_gpu_web.js>"
    "$<INSTALL_INTERFACE:--js-library=$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/campello_gpu_web.js>"
)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INTERFACE_LINK_DEPENDS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/webgpu/campello_gpu_web.js>")
install(FILES src/webgpu/campello_gpu_web.js DESTINATION ${CMAKE_INSTALL_LIBDIR})

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
)
