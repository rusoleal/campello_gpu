cmake_minimum_required(VERSION 3.22.1)
project(campello_wasm_example)

set(CMAKE_CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# Find campello_gpu — when built as a subdirectory of the main project,
# the target is already available. When built standalone, use find_package.
# ---------------------------------------------------------------------------
if(NOT TARGET campello_gpu)
    find_package(campello_gpu REQUIRED)
endif()

# ---------------------------------------------------------------------------
# WASM / WebGPU example (HTML5 canvas).
# ---------------------------------------------------------------------------
add_executable(campello_wasm_example main.cpp)
target_link_libraries(campello_wasm_example
    campello_gpu
)

target_link_options(campello_wasm_example PRIVATE
    --use-port=emdawnwebgpu
    -sASYNCIFY
    -sALLOW_MEMORY_GROWTH=1
    # index.html is an Emscripten shell template: emcc substitutes its
    # {{{ SCRIPT }}} placeholder with the generated loader script.
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/index.html
)
set_property(TARGET campello_wasm_example APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/index.html)

set_target_properties(campello_wasm_example PROPERTIES
    SUFFIX ".html"
)

# ---------------------------------------------------------------------------
# WASM / WebGPU compute example (SAXPY + async readback), no canvas.
# ---------------------------------------------------------------------------
add_executable(campello_wasm_compute compute.cpp)
target_link_libraries(campello_wasm_compute
    campello_gpu
)

target_link_options(campello_wasm_compute PRIVATE
    --use-port=emdawnwebgpu
    -sASYNCIFY
    -sALLOW_MEMORY_GROWTH=1
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/compute.html
)
set_property(TARGET campello_wasm_compute APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/compute.html)

set_target_properties(campello_wasm_compute PROPERTIES
    SUFFIX ".html"
)
