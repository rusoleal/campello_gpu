# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
cmake -B build
make -C build
```

Platform-specific CMake files are selected based on target: `android.cmake`, `macos.cmake`, `windows.cmake`.

No dedicated test infrastructure exists — testing is done through the example apps in `examples/android/` and `examples/apple/`.

## Architecture Overview

`campello_gpu` is a cross-platform GPU abstraction library (C++17) with a WebGPU-inspired API targeting Metal (macOS/iOS), Vulkan (Android), and DirectX 12 (Windows, in progress). All public types live in the `systems::leal::campello_gpu` namespace.

### Handle-Based Abstraction Pattern

The public API classes (`Device`, `Buffer`, `Texture`, `RenderPipeline`, etc.) hold `void*` pointers to opaque internal handle structs. The actual native GPU objects (e.g., `VkBuffer`, `MTLBuffer`) live inside those handle structs. This keeps platform headers out of the public API.

- Public headers: `inc/campello_gpu/*.hpp`
- Internal handle structs: `src/vulkan_android/*_handle.hpp`
- Implementations per platform: `src/vulkan_android/`, `src/metal/`, `src/directx/`
- Platform-independent utilities: `src/pi/utils.cpp`

### API Lifecycle

```
Device::getAdapters() → Adapter → Device::createDevice(adapter, platformData)
Device → create resources: Buffer, Texture, ShaderModule, PipelineLayout, BindGroupLayout
Device → create pipelines: RenderPipeline, ComputePipeline
Device → CommandEncoder → RenderPassEncoder / ComputePassEncoder → CommandBuffer → submit
```

The `void* pd` parameter passed to device creation is the platform-specific window/surface handle (e.g., `ANativeWindow*` on Android).

### Key Types

| Category | Types |
|---|---|
| Initialization | `Adapter`, `Device` |
| Resources | `Buffer`, `Texture`, `TextureView`, `Sampler`, `ShaderModule` |
| Pipelines | `RenderPipeline`, `ComputePipeline`, `PipelineLayout` |
| Binding | `BindGroup`, `BindGroupLayout` |
| Commands | `CommandEncoder`, `RenderPassEncoder`, `ComputePassEncoder`, `CommandBuffer` |
| Misc | `QuerySet` |

Descriptors (configuration structs) are in `inc/campello_gpu/descriptors/`. Enums are in `inc/campello_gpu/constants/`.

### Vulkan Android Implementation Detail

`src/vulkan_android/common.hpp` defines the central `DeviceData` struct holding all Vulkan state (instance, physical device, logical device, queues, swapchain). All implementation files receive a `DeviceData*` cast from the public class's `void*` handle. The Vulkan instance and surface setup is in `src/vulkan_android/device.cpp`.
