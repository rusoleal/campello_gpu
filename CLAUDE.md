# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
cmake -B build
make -C build
```

Platform-specific CMake files are selected based on target: `android.cmake`, `macos.cmake`, `ios.cmake`, `linux.cmake`, `windows.cmake`, `wasm.cmake` (Emscripten/WebGPU).

Tests live in `tests/` (GoogleTest): `-DBUILD_TESTS=ON` builds `tests/universal/` (no GPU), `-DBUILD_INTEGRATION_TESTS=ON` adds the per-platform GPU tests in `tests/platform/`. Run them with `ctest --test-dir build --output-on-failure`.

### Web / WebGPU (Emscripten)

```bash
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_INTEGRATION_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure   # needs Node 22+ and Chrome
```

Requires Emscripten 4.0.10+ (the backend targets Dawn's `emdawnwebgpu` port and its upstream `webgpu.h`, not the removed `-sUSE_WEBGPU`). Under Emscripten the universal tests run under Node, and the GPU test is `tests/webgpu/webgpu_browser_test.cpp` (GoogleTest can't run in a browser page), run in headless Chrome by `tests/webgpu/run_in_browser.mjs`. CI runs it on SwiftShader. Examples are in `examples/wasm/`.

## Architecture Overview

`campello_gpu` is a cross-platform GPU abstraction library (C++20) with a WebGPU-inspired API targeting Metal (macOS/iOS), Vulkan (Android, Linux), DirectX 12 (Windows, including UWP/Xbox) and WebGPU (web, via Emscripten). All public types live in the `systems::leal::campello_gpu` namespace.

### Handle-Based Abstraction Pattern

The public API classes (`Device`, `Buffer`, `Texture`, `RenderPipeline`, etc.) hold `void*` pointers to opaque internal handle structs. The actual native GPU objects (e.g., `VkBuffer`, `MTLBuffer`) live inside those handle structs. This keeps platform headers out of the public API.

- Public headers: `inc/campello_gpu/*.hpp`
- Internal handle structs: `src/<backend>/*_handle.hpp`
- Implementations per platform: `src/vulkan/`, `src/metal/`, `src/directx/`, `src/webgpu/`
- Platform-independent utilities: `src/pi/utils.cpp`

### API Lifecycle

```
Device::getAdapters() → Adapter → Device::createDevice(adapter, platformData)
Device → create resources: Buffer, Texture, ShaderModule, PipelineLayout, BindGroupLayout
Device → create pipelines: RenderPipeline, ComputePipeline
Device → CommandEncoder → RenderPassEncoder / ComputePassEncoder → CommandBuffer → submit
```

The `void* pd` parameter passed to device creation is the platform-specific window/surface handle (e.g., `ANativeWindow*` on Android, a `<canvas>` CSS selector string such as `"#canvas"` on the web).

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

### Vulkan Implementation Detail

`src/vulkan/common.hpp` defines the central `DeviceData` struct holding all Vulkan state (instance, physical device, logical device, queues, swapchain). All implementation files receive a `DeviceData*` cast from the public class's `void*` handle. The Vulkan instance and surface setup is in `src/vulkan/device.cpp`.

### WebGPU Implementation Detail

`src/webgpu/common.hpp` defines `WebGPUDeviceData` and the handle structs, plus helpers for webgpu.h async callbacks (`makeCallbackInfo()` + `waitForAsync()`, which block by yielding to the browser through ASYNCIFY). Callback types are derived with templates/`decltype` so the code tolerates small webgpu.h signature changes between Dawn releases. WebGPU-specific rules the backend handles: adapters can create only one device, zero-initialised webgpu.h structs aren't spec defaults (e.g. `multisample.count` and `maxAnisotropy` must be set), depth/stencil load/store ops must be undefined for absent aspects, and there's no encoder-level `writeTimestamp`.
