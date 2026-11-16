
# iOS uses the same Metal backend as macOS.
# CMake handles the sysroot and architecture differences via
# CMAKE_SYSTEM_NAME=iOS / CMAKE_OSX_SYSROOT / CMAKE_OSX_ARCHITECTURES.

# MetalFX (TemporalUpscaler) is Objective-C++ (ARC), loaded at run time (macOS 13 / iOS 16 and
# later; not linked, as some SDKs, e.g. Xcode 16's iOS Simulator, don't have it).
enable_language(OBJCXX)
set_source_files_properties(src/metal/temporal_upscaler.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

add_library(${PROJECT_NAME} SHARED
    src/pi/pixel_format.cpp
    src/pi/utils.cpp
    src/pi/validation_diagnostics.cpp
    src/metal/adapter.cpp
    src/metal/bind_group.cpp
    src/metal/bind_group_layout.cpp
    src/metal/buffer.cpp
    src/metal/command_buffer.cpp
    src/metal/command_encoder.cpp
    src/metal/compute_pass_encoder.cpp
    src/metal/compute_pipeline.cpp
    src/metal/context.cpp
    src/metal/device.cpp
    src/metal/surface.cpp
    src/metal/fence.cpp
    src/metal/pipeline_layout.cpp
    src/metal/acceleration_structure.cpp
    src/metal/query_set.cpp
    src/metal/ray_tracing_pass_encoder.cpp
    src/metal/ray_tracing_pipeline.cpp
    src/metal/render_pass_encoder.cpp
    src/metal/render_pipeline.cpp
    src/metal/sampler.cpp
    src/metal/shader_module.cpp
    src/metal/texture.cpp
    src/metal/texture_view.cpp
    src/metal/temporal_upscaler.mm
)

target_link_libraries(${PROJECT_NAME}
    "-framework Metal" "-framework Foundation" "-framework QuartzCore" objc
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
)
