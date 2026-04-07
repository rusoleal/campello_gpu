
add_library(${PROJECT_NAME} SHARED
    src/directx/adapter.cpp
    src/directx/bind_group.cpp
    src/directx/bind_group_layout.cpp
    src/directx/buffer.cpp
    src/directx/command_buffer.cpp
    src/directx/command_encoder.cpp
    src/directx/compute_pass_encoder.cpp
    src/directx/compute_pipeline.cpp
    src/directx/context.cpp
    src/directx/device.cpp
    src/directx/pipeline_layout.cpp
    src/directx/query_set.cpp
    src/directx/render_pass_encoder.cpp
    src/directx/render_pipeline.cpp
    src/directx/sampler.cpp
    src/directx/shader_module.cpp
    src/directx/texture.cpp
    src/directx/texture_view.cpp
    src/directx/acceleration_structure.cpp
    src/directx/ray_tracing_pipeline.cpp
    src/directx/ray_tracing_pass_encoder.cpp
    src/pi/utils.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries(${PROJECT_NAME}
    D3d12
    dxgi
    dxguid
)

target_include_directories(${PROJECT_NAME} PUBLIC
                            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
                            )

target_include_directories(${PROJECT_NAME} PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           )
