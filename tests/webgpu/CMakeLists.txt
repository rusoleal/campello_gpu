# ---------------------------------------------------------------------------
# WebGPU (Emscripten) browser test.
#
# GoogleTest can't run inside a browser page, so the WebGPU backend has its
# own self-checking program (webgpu_browser_test.cpp) that runs in headless
# Chrome via run_in_browser.mjs:
#
#   emcmake cmake -B build -S . -DBUILD_INTEGRATION_TESTS=ON
#   cmake --build build
#   ctest --test-dir build --output-on-failure      # needs Node >= 22 + Chrome
#
# or directly: node tests/webgpu/run_in_browser.mjs build/tests/webgpu/webgpu_browser_test.html
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

# Registered with CTest when a host Node is available. The runner finds Chrome
# itself (or via CHROME_PATH) and takes extra flags from CHROME_FLAGS.
find_program(CAMPELLO_NODE_EXECUTABLE NAMES node nodejs)
if(CAMPELLO_NODE_EXECUTABLE)
    add_test(NAME webgpu_browser_test
        COMMAND ${CAMPELLO_NODE_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/run_in_browser.mjs
                $<TARGET_FILE:webgpu_browser_test>)
endif()
