cmake_minimum_required(VERSION 3.22.1)

project(campello_gpu VERSION 0.24.0)

set(CMAKE_CXX_STANDARD 20)

message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

include(GNUInstallDirs)

include_directories(inc)

configure_file(src/campello_gpu_config.h.in campello_gpu_config.h)

# ---------------------------------------------------------------------------
# Sanitizer build profiles
#
# Build with one of these custom build types to enable sanitizers:
#
#   ASan  — AddressSanitizer + UndefinedBehaviorSanitizer
#           cmake -S . -B build_asan -DCMAKE_BUILD_TYPE=ASan
#           cmake --build build_asan
#           ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build_asan
#
#   LSan  — LeakSanitizer (Linux/GCC/Clang; limited support on Apple Clang)
#           cmake -S . -B build_lsan -DCMAKE_BUILD_TYPE=LSan
#
#   UBSan — UndefinedBehaviorSanitizer only (fastest sanitizer job)
#           cmake -S . -B build_ubsan -DCMAKE_BUILD_TYPE=UBSan
#
# Use lsan_suppressions.txt to filter driver/runtime false positives:
#
#   ASAN_OPTIONS=detect_leaks=1 \
#   LSAN_OPTIONS=suppressions=lsan_suppressions.txt:print_suppressions=0 \
#       ctest --test-dir build_asan
#
# Notes:
#   - Do not combine ASan with TSan; they are mutually exclusive.
#   - ASan/LSan will not catch Objective-C autorelease leaks on macOS;
#     use Xcode Instruments → Allocations for those.
#   - MSVC uses different flags; these profiles are disabled there.
# ---------------------------------------------------------------------------
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_ASAN
        "-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C++ flags for ASan builds" FORCE)
    set(CMAKE_C_FLAGS_ASAN
        "-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C flags for ASan builds" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_ASAN
        "-fsanitize=address,undefined"
        CACHE STRING "Linker flags for ASan builds" FORCE)

    set(CMAKE_CXX_FLAGS_LSAN
        "-fsanitize=leak -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C++ flags for LSan builds" FORCE)
    set(CMAKE_C_FLAGS_LSAN
        "-fsanitize=leak -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C flags for LSan builds" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_LSAN
        "-fsanitize=leak"
        CACHE STRING "Linker flags for LSan builds" FORCE)

    set(CMAKE_CXX_FLAGS_UBSAN
        "-fsanitize=undefined -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C++ flags for UBSan builds" FORCE)
    set(CMAKE_C_FLAGS_UBSAN
        "-fsanitize=undefined -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C flags for UBSan builds" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_UBSAN
        "-fsanitize=undefined"
        CACHE STRING "Linker flags for UBSan builds" FORCE)

    # TSan is mutually exclusive with ASan/LSan (both intercept the same
    # allocator hooks) -- always use it as the sole -DCMAKE_BUILD_TYPE=TSAN
    # profile, never combined with the flags above. Needed for a reliable
    # verdict on data races (e.g. tests/platform/test_device_present_race.cpp):
    # a plain run only *might* crash on a race, timing-dependent, but TSan's
    # happens-before analysis flags unsynchronized access deterministically.
    set(CMAKE_CXX_FLAGS_TSAN
        "-fsanitize=thread -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C++ flags for TSan builds" FORCE)
    set(CMAKE_C_FLAGS_TSAN
        "-fsanitize=thread -fno-omit-frame-pointer -O1 -g"
        CACHE STRING "C flags for TSan builds" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_TSAN
        "-fsanitize=thread"
        CACHE STRING "Linker flags for TSan builds" FORCE)
endif()

# ---------------------------------------------------------------------------
# Xbox console (GXDK) partition build check (Windows only)
#
# CAMPELLO_GDK_GAMING_DESKTOP=ON — compiles the DirectX 12 backend against
#   WINAPI_FAMILY=WINAPI_FAMILY_GAMES instead of the full desktop Win32 API
#   surface. CORRECTED 2026-08-24: this does NOT match what a
#   Gaming.Desktop.x64 build sees — confirmed against the real, free,
#   public Microsoft GDK (installed and built against this session) that
#   Gaming.Desktop.x64's own Platform.Common.props sets
#   WINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP, and that campello_gpu already
#   builds, links, and passes its full integration-test suite as an
#   ordinary HWND-based Win32 app under the real Gaming.Desktop.x64 MSBuild
#   platform with zero source changes — this option isn't needed for that
#   target at all. WINAPI_FAMILY_GAMES is the actual Xbox **console**
#   partition (Gaming.Xbox.*.x64, part of the non-public GXDK, which this
#   project has never had access to) — see TODO.md's "Windows / Xbox
#   (Microsoft GDK) partition support" section for the full writeup this
#   corrects, including where campello_editor's TODO.md Phase 4.5 roadmap
#   originally got this from. Must be declared before windows.cmake is
#   included below, since that file reads it.
#
# Deliberately does NOT require the Microsoft GDK to be installed:
# WINAPI_FAMILY_GAMES itself is defined in the stock Windows SDK's
# winapifamily.h starting at SDK 19041 (confirmed against
# learn.microsoft.com) — this option only restricts which Win32
# declarations this library's *source* can see, a best-effort stand-in
# compile-time partition-conformance check for the actual (unreachable)
# GXDK console partition. campello_gpu calls no GDK-specific runtime APIs
# itself (XGameRuntimeInitialize etc. live in campello_widgets' app-shell
# layer, not here), so it needs neither GDK headers/libs nor
# xgameplatform.lib/xgameruntime.lib to prove this.
# ---------------------------------------------------------------------------
option(CAMPELLO_GDK_GAMING_DESKTOP "Compile the Windows backend under WINAPI_FAMILY_GAMES (see TODO.md Phase 4.5 in campello_editor)" OFF)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    include(android.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    include(macos.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    include(ios.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    include(windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(linux.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    include(wasm.cmake)
endif()

# ---------------------------------------------------------------------------
# Alias target — lets consumers link against campello_gpu::campello_gpu
# whether they use FetchContent (add_subdirectory) or find_package.
# ---------------------------------------------------------------------------
add_library(campello_gpu::campello_gpu ALIAS ${PROJECT_NAME})

# Expose the installed header path for consumers that use find_package.
get_target_property(_campello_gpu_type ${PROJECT_NAME} TYPE)
if(NOT _campello_gpu_type STREQUAL "INTERFACE_LIBRARY")
    target_include_directories(${PROJECT_NAME} PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
endif()

# ---------------------------------------------------------------------------
# Install & export — required for find_package support.
# FetchContent users (add_subdirectory) do not need this block;
# they get the target directly via the alias above.
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
    EXPORT campello_gpuTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY inc/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT campello_gpuTargets
    FILE      campello_gpuTargets.cmake
    NAMESPACE campello_gpu::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/campello_gpu
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/campello_gpuConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/campello_gpuConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/campello_gpu
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/campello_gpuConfigVersion.cmake
    VERSION     ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/campello_gpuConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/campello_gpuConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/campello_gpu
)

# ---------------------------------------------------------------------------
# Vulkan validation layers
#
# CAMPELLO_GPU_VALIDATION=ON  — compile in VK_LAYER_KHRONOS_validation +
#                               VK_EXT_debug_utils messenger (dev/debug only).
#                               Requires vulkan-validationlayers to be installed.
#                               Linux:   sudo apt install vulkan-validationlayers
#                               Android: copy layer .so into the APK's jniLibs
# ---------------------------------------------------------------------------
option(CAMPELLO_GPU_VALIDATION "Enable Vulkan validation layers (debug only)" OFF)

get_target_property(_campello_type ${PROJECT_NAME} TYPE)
if(CAMPELLO_GPU_VALIDATION AND NOT _campello_type STREQUAL "INTERFACE_LIBRARY")
    target_compile_definitions(${PROJECT_NAME} PRIVATE CAMPELLO_GPU_VALIDATION=1)
    message(STATUS "campello_gpu: Vulkan validation layers ENABLED")
endif()

# ---------------------------------------------------------------------------
# Tests
#
# BUILD_TESTS=ON            — universal tests (enums, utils, descriptors).
#                             No GPU or library linking required.
# BUILD_INTEGRATION_TESTS=ON — platform GPU tests. Requires a working device.
#                             Implies BUILD_TESTS=ON.
# ---------------------------------------------------------------------------
option(BUILD_TESTS             "Build unit tests (universal, no GPU)"    OFF)
option(BUILD_INTEGRATION_TESTS "Build GPU integration tests (per-platform)" OFF)

if(BUILD_INTEGRATION_TESTS)
    set(BUILD_TESTS ON)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Examples
#
# BUILD_EXAMPLES=ON — platform-specific example applications.
# ---------------------------------------------------------------------------
option(BUILD_EXAMPLES "Build example applications" OFF)

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_subdirectory(examples/windows/campello_example)
endif()

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_subdirectory(examples/linux)
endif()

if(BUILD_EXAMPLES AND ${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    add_subdirectory(examples/wasm)
endif()
