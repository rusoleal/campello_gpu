# ---------------------------------------------------------------------------
# WebGPU (Emscripten) GPU tests, run in headless Chrome via run_in_browser.mjs:
#
#   webgpu_gtest_suite    the GoogleTest GPU suite (tests/platform/) built for
#                         the web, with a browser-friendly main()
#   webgpu_browser_test   web-specific checks: canvas surface and resize,
#                         JavaScript texture interop, GPU timing, errors
#
#   emcmake cmake -B build -S . -DBUILD_INTEGRATION_TESTS=ON
#   cmake --build build
#   ctest --test-dir build --output-on-failure      # needs Node >= 22 + Chrome
#
# or directly: node tests/webgpu/run_in_browser.mjs build/tests/webgpu/<test>.html
# ---------------------------------------------------------------------------

add_executable(webgpu_browser_test webgpu_browser_test.cpp)
target_link_libraries(webgpu_browser_test PRIVATE campello_gpu)
set_target_properties(webgpu_browser_test PROPERTIES
    CXX_STANDARD 20
    SUFFIX ".html"
)
target_link_options(webgpu_browser_test PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
)
set_property(TARGET webgpu_browser_test APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)

# ---------------------------------------------------------------------------
# The GoogleTest GPU suite (tests/platform/) built for the web, run in the
# same browser harness. Needs the GoogleTest targets from tests/, which the
# top-level CMakeLists adds first.
# ---------------------------------------------------------------------------
set(WEBGPU_PLATFORM_TEST_SOURCES
    ../platform/test_device.cpp
    ../platform/test_buffer.cpp
    ../platform/test_texture.cpp
    ../platform/test_texture_view.cpp
    ../platform/test_command_encoder.cpp
    ../platform/test_command_buffer.cpp
    ../platform/test_fence.cpp
    ../platform/test_shader_module.cpp
    ../platform/test_render_pipeline.cpp
    ../platform/test_render_pass_encoder.cpp
    ../platform/test_compute_pass_encoder.cpp
    ../platform/test_raytracing.cpp
    ../platform/test_frame_ring.cpp
    ../platform/test_sampler.cpp
    ../platform/test_bind_group.cpp
    ../platform/test_query_set.cpp
    ../platform/test_pipeline_layout.cpp
    ../platform/test_adapter.cpp
)
add_executable(webgpu_gtest_suite gtest_browser_main.cpp ${WEBGPU_PLATFORM_TEST_SOURCES})
target_include_directories(webgpu_gtest_suite PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(webgpu_gtest_suite PRIVATE campello_gpu GTest::gtest)
set_target_properties(webgpu_gtest_suite PROPERTIES
    CXX_STANDARD 20
    SUFFIX ".html"
)
target_link_options(webgpu_gtest_suite PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    # GoogleTest needs more stack than the default.
    -sSTACK_SIZE=1MB
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
)
if(CAMPELLO_GPU_WASM_JSPI)
    # Match GoogleTest's native wasm exceptions (see tests/CMakeLists.txt).
    target_compile_options(webgpu_gtest_suite PRIVATE -fwasm-exceptions)
    target_link_options(webgpu_gtest_suite PRIVATE -fwasm-exceptions)
else()
    # ...and so does ASYNCIFY's unwinding through GoogleTest's call stack.
    target_link_options(webgpu_gtest_suite PRIVATE -sASYNCIFY_STACK_SIZE=65536)
endif()
set_property(TARGET webgpu_gtest_suite APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)

# Registered with CTest when a host Node is available. The runner finds Chrome
# itself (or via CHROME_PATH) and takes extra flags from CHROME_FLAGS.
find_program(CAMPELLO_NODE_EXECUTABLE NAMES node nodejs)
if(CAMPELLO_NODE_EXECUTABLE)
    add_test(NAME webgpu_browser_test
        COMMAND ${CAMPELLO_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/run_in_browser.mjs
                $<TARGET_FILE:webgpu_browser_test>)
    add_test(NAME webgpu_gtest_suite
        COMMAND ${CAMPELLO_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/run_in_browser.mjs
                $<TARGET_FILE:webgpu_gtest_suite>)
endif()
