# campello_gpu

A cross-platform GPU abstraction library (C++17) with a WebGPU-inspired API. Provides a unified interface over Metal, Vulkan, and DirectX 12 for both rendering and compute workloads.

## Platforms

| OS | Engine | Status |
|----|--------|--------|
| windows | DirectX 12 | In progress |
| windows | DirectX 11 | Freezed |
| windows | OpenGL | Freezed |
| windows | Vulkan 1.x | Not started |
| macos/ios | Metal | In progress |
| macos/ios | OpenGL | Freezed |
| macos/ios | Vulkan 1.x | Not started |
| android | Vulkan 1.x | In progress |
| android | OpenGL | Freezed |
| linux | Vulkan 1.x | Not started |
| linux | OpenGL | Freezed |

## Requirements

- CMake 3.22.1+
- C++17 compiler
- Platform SDK: Metal (macOS/iOS), Vulkan (Android), DirectX 12 (Windows)

## Build

```bash
cmake -B build
make -C build
```

CMake automatically selects the backend based on the target platform (`android.cmake`, `macos.cmake`, `windows.cmake`, etc.).

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

### Resource Creation

```cpp
// Buffers
auto buffer = device->createBuffer(size, BufferUsage::vertex | BufferUsage::copySrc);
auto buffer = device->createBuffer(size, BufferUsage::uniform, data);

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
// submit via platform-specific queue
```
