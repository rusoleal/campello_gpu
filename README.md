# campello_gpu

A cross-platform GPU abstraction library (C++20) with a WebGPU-inspired API. Provides a unified interface over Metal, Vulkan, and DirectX 12 for rendering, compute and raytracing workloads.

## 🚀 Part of the Campello Engine

This project is a module within the **Campello** ecosystem.

👉 Main repository: https://github.com/rusoleal/campello

Campello is a modular, composable game engine built as a collection of independent libraries.
Each module is designed to work standalone, but integrates seamlessly into the engine runtime.

## Platforms

| OS | Engine | Status |
|----|--------|--------|
| android | Vulkan 1.x | Production ready |
| macos/ios | Metal | Production ready |
| windows | DirectX 12 | Production ready |
| windows | DirectX 11 | Frozen |
| windows | OpenGL | Frozen |
| windows | Vulkan 1.x | Not started |
| macos/ios | OpenGL | Frozen |
| macos/ios | Vulkan 1.x | Not started |
| android | OpenGL | Frozen |
| linux | Vulkan 1.x | Production ready |
| linux | OpenGL | Frozen |

## Requirements

- CMake 3.22.1+
- C++20 compiler
- Platform SDK: Metal (macOS/iOS), Vulkan (Android / Linux), DirectX 12 (Windows)

## Build

```bash
cmake -B build
make -C build
```

CMake automatically selects the backend based on the target platform (`android.cmake`, `macos.cmake`, `windows.cmake`, `linux.cmake`, etc.).

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install libvulkan-dev

# Build the library
cmake -B build
cmake --build build

# Build with tests and examples
cmake -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

## Integration

### find_package (installed library)

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --install build
```

```cmake
find_package(campello_gpu REQUIRED)
target_link_libraries(your_target PRIVATE campello_gpu::campello_gpu)
```

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    campello_gpu
    GIT_REPOSITORY https://github.com/rusoleal/campello_gpu.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(campello_gpu)

target_link_libraries(your_target PRIVATE campello_gpu::campello_gpu)
```

## Usage

All types are in the `systems::leal::campello_gpu` namespace.

```cpp
#include <campello_gpu/device.hpp>

using namespace systems::leal::campello_gpu;

// Create a device (pd is the platform window/surface handle)
auto device = Device::createDefaultDevice(pd);

// Or enumerate adapters and pick one
auto adapters = Device::getAdapters();
auto device = Device::createDevice(adapters[0], pd);

// Query device info
std::string name = device->getName();
std::set<Feature> features = device->getFeatures();
std::string version = Device::getEngineVersion();
```

#### Linux (X11 / Wayland)

```cpp
#include <campello_gpu/platform/linux_surface.hpp>

// X11
LinuxSurfaceInfo surfaceInfo{};
surfaceInfo.display = (void*)display;   // Display*
surfaceInfo.window  = (void*)window;    // Window (cast via uintptr_t)
surfaceInfo.api     = LinuxWindowApi::x11;
auto device = Device::createDefaultDevice(&surfaceInfo);

// Wayland
LinuxSurfaceInfo surfaceInfo{};
surfaceInfo.display = (void*)wl_display;
surfaceInfo.window  = (void*)wl_surface;
surfaceInfo.api     = LinuxWindowApi::wayland;
auto device = Device::createDefaultDevice(&surfaceInfo);
```

### Resource Creation

```cpp
// Buffers
auto buffer = device->createBuffer(size, BufferUsage::vertex | BufferUsage::copySrc);
auto buffer = device->createBuffer(size, BufferUsage::uniform, data);

// Readback buffer (GPU → CPU)
auto readbackBuffer = device->createBuffer(size, BufferUsage::copyDst | BufferUsage::mapRead);

// Textures
auto texture = device->createTexture(TextureType::_2D, PixelFormat::RGBA8Unorm,
                                     width, height, 1, mipLevels, 1,
                                     TextureUsage::textureBinding | TextureUsage::renderTarget);

// Shaders (compiled bytecode/binary)
auto shader = device->createShaderModule(buffer, size);
```

### Pipelines & Binding

```cpp
// Layout describes shader resource binding points
auto bindGroupLayout = device->createBindGroupLayout(bindGroupLayoutDescriptor);
auto pipelineLayout  = device->createPipelineLayout(pipelineLayoutDescriptor);

// Render pipeline
auto pipeline = device->createRenderPipeline(renderPipelineDescriptor);

// Compute pipeline
auto pipeline = device->createComputePipeline(computePipelineDescriptor);

// Bind concrete resources to a layout
auto bindGroup = device->createBindGroup(bindGroupDescriptor);
```

### Command Recording & Submission

```cpp
auto encoder = device->createCommandEncoder();

// Render pass
auto renderPass = encoder->beginRenderPass(beginRenderPassDescriptor);
renderPass->setPipeline(pipeline);
renderPass->setVertexBuffer(0, vertexBuffer, 0);
renderPass->setBindGroup(0, bindGroup, {});
renderPass->draw(vertexCount, instanceCount, 0, 0);
renderPass->end();

// Compute pass
auto computePass = encoder->beginComputePass();
computePass->setPipeline(computePipeline);
computePass->setBindGroup(0, bindGroup, {});
computePass->dispatchWorkgroups(x, y, z);
computePass->end();

auto commandBuffer = encoder->finish();
device->submit(commandBuffer);
```

### Ray Tracing

Hardware ray tracing is available when `Feature::raytracing` is reported by the device (Vulkan KHR on Android, Metal on macOS/iOS, DXR on Windows).

```cpp
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>

// Feature check
auto features = device->getFeatures();
if (!features.count(Feature::raytracing)) return; // not supported

// 1. Vertex buffer for triangle geometry
auto vertexBuffer = device->createBuffer(sizeof(vertices),
    BufferUsage::accelerationStructureInput, vertices);

// 2. Bottom-level acceleration structure (BLAS)
AccelerationStructureGeometryDescriptor geoDesc{};
geoDesc.type         = AccelerationStructureGeometryType::triangles;
geoDesc.opaque       = true;
geoDesc.vertexBuffer = vertexBuffer;
geoDesc.vertexStride = sizeof(float) * 3;
geoDesc.vertexCount  = 3;

BottomLevelAccelerationStructureDescriptor blasDesc{};
blasDesc.geometries = { geoDesc };
blasDesc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace;

auto blas   = device->createBottomLevelAccelerationStructure(blasDesc);
auto scratch = device->createBuffer(blas->getBuildScratchSize(), BufferUsage::storage);

auto encoder = device->createCommandEncoder();
encoder->buildAccelerationStructure(blas, blasDesc, scratch);
device->submit(encoder->finish());

// 3. Top-level acceleration structure (TLAS)
AccelerationStructureInstance instance{};
instance.blas = blas;
instance.mask = 0xFF;

TopLevelAccelerationStructureDescriptor tlasDesc{};
tlasDesc.instances  = { instance };
tlasDesc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace;

auto tlas    = device->createTopLevelAccelerationStructure(tlasDesc);
auto scratch2 = device->createBuffer(tlas->getBuildScratchSize(), BufferUsage::storage);

encoder = device->createCommandEncoder();
encoder->buildAccelerationStructure(tlas, tlasDesc, scratch2);
device->submit(encoder->finish());

// 4. Ray tracing pipeline
RayTracingPipelineDescriptor rtDesc{};
rtDesc.rayGeneration = { shaderModule, "rayGenMain" };
rtDesc.layout        = pipelineLayout;
rtDesc.maxRecursionDepth = 1;

auto pipeline = device->createRayTracingPipeline(rtDesc);

// 5. Dispatch rays each frame
auto rtPass = encoder->beginRayTracingPass();
rtPass->setPipeline(pipeline);
rtPass->setBindGroup(0, bindGroup, {}, 0, 0);
rtPass->traceRays(width, height, 1);
rtPass->end();
device->submit(encoder->finish());
```

### GPU → CPU Readback

Copy texture data to a buffer and read it on the CPU:

```cpp
// Create a readback buffer
auto readbackBuffer = device->createBuffer(
    textureSize, 
    BufferUsage::copyDst | BufferUsage::mapRead);

// Copy texture to buffer
auto encoder = device->createCommandEncoder();
encoder->copyTextureToBuffer(texture, mipLevel, arrayLayer,
                              readbackBuffer, offset, bytesPerRow);
device->submit(encoder->finish());

// Read data on CPU
std::vector<uint8_t> data(textureSize);
readbackBuffer->download(0, textureSize, data.data());
```

Or use the convenience method for synchronous texture readback:

```cpp
// One-liner: creates temp buffer, submits command, waits, copies data
std::vector<uint8_t> pixels(width * height * 4);
texture->download(mipLevel, arrayLayer, pixels.data(), pixels.size());
```


### Observability & Metrics

Comprehensive profiling and memory monitoring across all backends:

```cpp
#include <campello_gpu/metrics.hpp>

// --- Resource counters (Phase 1) ---
ResourceCounters counters = device->getResourceCounters();
std::cout << "Buffers: " << counters.bufferCount << "\n";
std::cout << "Textures: " << counters.textureCount << "\n";
std::cout << "Render pipelines: " << counters.renderPipelineCount << "\n";

CommandStats stats = device->getCommandStats();
std::cout << "Draw calls: " << stats.drawCalls << "\n";
std::cout << "Compute dispatches: " << stats.dispatchCalls << "\n";

// Complete snapshot
Metrics m = device->getMetrics();

// --- Memory tracking (Phase 2) ---
ResourceMemoryStats mem = device->getResourceMemoryStats();
std::cout << "GPU memory used: " << mem.totalTrackedBytes / (1024*1024) << " MB\n";
std::cout << "  Buffers: " << mem.bufferBytes / (1024*1024) << " MB\n";
std::cout << "  Textures: " << mem.textureBytes / (1024*1024) << " MB\n";
std::cout << "Peak memory: " << mem.peakTotalBytes / (1024*1024) << " MB\n";

// Reset peak tracking for a new measurement period
device->resetPeakMemoryStats();

// --- GPU timing (Phase 3) ---
auto encoder = device->createCommandEncoder();
// ... record commands ...
auto cmdBuffer = encoder->finish();
device->submit(cmdBuffer);

// Get actual GPU execution time (nanoseconds)
uint64_t gpuTimeNs = cmdBuffer->getGPUExecutionTime();
std::cout << "GPU time: " << (gpuTimeNs / 1e6) << " ms\n";

// Accumulated pass performance stats
PassPerformanceStats perf = device->getPassPerformanceStats();
std::cout << "Render pass GPU time: " << perf.renderPassTimeNs / 1e6 << " ms\n";

// --- Memory pressure management ---
// Configure budget thresholds
MemoryBudget budget;
budget.warningThresholdPercent = 75;   // 75% of available memory
budget.criticalThresholdPercent = 90;  // 90% of available memory
device->setMemoryBudget(budget);

// Register callback for pressure changes
device->setMemoryPressureCallback([](MemoryPressureLevel level, const ResourceMemoryStats& stats) {
    switch (level) {
        case MemoryPressureLevel::Warning:
            std::cerr << "Memory warning: " << stats.totalTrackedBytes / (1024*1024) << " MB\n";
            break;
        case MemoryPressureLevel::Critical:
            std::cerr << "Memory critical! Consider freeing resources\n";
            break;
        default:
            break;
    }
});

// Check current pressure level
MemoryPressureLevel level = device->checkMemoryPressure();
```
