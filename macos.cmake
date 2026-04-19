
#find_package(Metal)

add_library(${PROJECT_NAME} SHARED
    src/pi/pixel_format.cpp
    src/pi/utils.cpp
    src/metal/acceleration_structure.cpp
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
    src/metal/pipeline_layout.cpp
    src/metal/query_set.cpp
    src/metal/ray_tracing_pass_encoder.cpp
    src/metal/ray_tracing_pipeline.cpp
    src/metal/render_pass_encoder.cpp
    src/metal/render_pipeline.cpp
    src/metal/sampler.cpp
    src/metal/shader_module.cpp
    src/metal/texture.cpp
    src/metal/texture_view.cpp
)

target_link_libraries(${PROJECT_NAME}
    "-framework Metal" "-framework Foundation" "-framework QuartzCore" objc
)

target_include_directories (${PROJECT_NAME} PUBLIC
                            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
                            )

target_include_directories(${PROJECT_NAME} PUBLIC
                           "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
                           )
