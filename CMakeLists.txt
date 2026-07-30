cmake_minimum_required(VERSION 3.22.1)

project(campello_gpu VERSION 0.21.0)

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
endif()

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
