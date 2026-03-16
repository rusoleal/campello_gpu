cmake_minimum_required(VERSION 3.22.1)
project(campello_example)

set(CMAKE_CXX_STANDARD 20)

add_executable(campello_example WIN32
    main.cpp
    Renderer.cpp
)

target_include_directories(campello_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(campello_example PRIVATE
    campello_gpu
    d3d12
    dxgi
    dxguid
    d3dcompiler
)

# Copy campello_gpu.dll next to the executable after build
add_custom_command(TARGET campello_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:campello_gpu>
        $<TARGET_FILE_DIR:campello_example>
)
