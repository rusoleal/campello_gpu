cmake_minimum_required(VERSION 3.22.1)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Universal tests — no GPU required, no library linking.
# src/pi/utils.cpp is compiled directly here because it is not part of the
# macOS/iOS cmake builds (only android.cmake includes it). This keeps
# universal tests truly cross-platform.
# ---------------------------------------------------------------------------
add_executable(campello_gpu_universal_tests
    universal/test_constants.cpp
    universal/test_constants_extended.cpp
    universal/test_pixel_format.cpp
    universal/test_descriptors.cpp
    universal/test_descriptors_extended.cpp
    ${CMAKE_SOURCE_DIR}/src/pi/pixel_format.cpp
)
target_include_directories(campello_gpu_universal_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(campello_gpu_universal_tests
    GTest::gtest_main
)
set_target_properties(campello_gpu_universal_tests PROPERTIES
    CXX_STANDARD 20
)

# ---------------------------------------------------------------------------
# Integration tests — require a real GPU device, link against campello_gpu.
# Build only when BUILD_INTEGRATION_TESTS=ON.
# Supported platforms: macOS/iOS (Metal), Android (Vulkan), Windows (DirectX 12).
# ---------------------------------------------------------------------------
if(BUILD_INTEGRATION_TESTS)
    add_executable(campello_gpu_integration_tests
        platform/test_device.cpp
        platform/test_buffer.cpp
        platform/test_texture.cpp
        platform/test_command_encoder.cpp
        platform/test_shader_module.cpp
        platform/test_render_pipeline.cpp
        platform/test_render_pass_encoder.cpp
        platform/test_compute_pass_encoder.cpp
        platform/test_raytracing.cpp
    )
    target_include_directories(campello_gpu_integration_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/inc
        ${CMAKE_BINARY_DIR}
    )
    target_link_libraries(campello_gpu_integration_tests
        campello_gpu
        GTest::gtest_main
    )
    set_target_properties(campello_gpu_integration_tests PROPERTIES
        CXX_STANDARD 20
    )
    # On Windows, campello_gpu is a DLL. Copy it next to the test binary so
    # CTest can find it when running test discovery and execution.
    if(WIN32)
        add_custom_command(TARGET campello_gpu_integration_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:campello_gpu>
                $<TARGET_FILE_DIR:campello_gpu_integration_tests>
        )
    endif()

    include(GoogleTest)
    # PRE_TEST defers test-list discovery to ctest time, which is required for
    # cross-compiled targets (e.g. Android) where the host cannot run the binary.
    gtest_discover_tests(campello_gpu_integration_tests
        DISCOVERY_MODE PRE_TEST)
endif()

include(GoogleTest)
gtest_discover_tests(campello_gpu_universal_tests)
