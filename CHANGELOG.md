# Changelog

All notable changes to campello_gpu are documented here.

## [0.2.0] - 2026-03-14

### Added
- Google Test unit test infrastructure (`tests/`) with two targets:
  - `campello_gpu_universal_tests` — enum values, pixel format utilities, descriptor construction; no GPU required, runs on every platform
  - `campello_gpu_integration_tests` — per-platform GPU device and buffer tests (opt-in via `BUILD_INTEGRATION_TESTS=ON`)
- `test.sh` — convenience script to configure, build, and run universal tests in one command
- `src/pi/pixel_format.cpp` — extracted `getPixelFormatSize` and `pixelFormatToString` from `utils.cpp` into a standalone translation unit with no platform or device dependencies; now included in all platform builds
- `src/metal/adapter.cpp` — Metal implementation of `Adapter` constructor and `getFeatures()`

### Fixed
- macOS build broken by missing `campello_gpu/context.hpp`: removed the unused `Context` class from `src/metal/context.cpp`; the file is kept as the required one-time Metal-cpp private implementation unit
- `src/metal/device.cpp` aligned with the public `Device` API:
  - `getDefaultDevice()` → `createDefaultDevice(void* pd)`
  - `getDevices()` → `getAdapters()` returning `std::vector<std::shared_ptr<Adapter>>`
  - `createBuffer(size, StorageMode)` → `createBuffer(size, BufferUsage)`
  - `createTexture(StorageMode, w, h, fmt, usage)` → full public signature with `TextureType`, depth, mip levels, and sample count
  - Added missing `createDevice(adapter, pd)`
- `src/metal/texture.cpp` aligned with the public `Texture` API:
  - `getPixelFormat()` → `getFormat()`
  - `getWidth()` / `getHeight()` return type corrected from `uint64_t` to `uint32_t`
  - `getUsageMode()` → `getUsage()` with correct `TextureUsage` flag mapping
  - Added `getDimension()`, `getMipLevelCount()`, `getSampleCount()`, `getDepthOrarrayLayers()`
- `macos.cmake` now includes `src/pi/pixel_format.cpp` and `src/metal/adapter.cpp`
- `android.cmake` now includes `src/pi/pixel_format.cpp` alongside `src/pi/utils.cpp`

---

## [0.1.1] - 2026-03-14

### Fixed
- `Buffer::upload` on Metal: corrected return type from `void` to `bool` to match the public header declaration; now returns `true` on success

---

## [0.1.0] - 2026-03-14

### Added

**WebGPU-inspired command API**
- `CommandEncoder`, `RenderPassEncoder`, `ComputePassEncoder`, `CommandBuffer` — full command recording and submission interface
- `beginRenderPass` / `beginComputePass` on `CommandEncoder`
- `draw`, `setPipeline`, `setVertexBuffer`, `setBindGroup`, `end` on `RenderPassEncoder`
- `dispatchWorkgroups`, `setPipeline`, `setBindGroup`, `end` on `ComputePassEncoder`

**Pipeline system**
- `RenderPipeline` and `ComputePipeline` types with full descriptor support
- `PipelineLayout` and `BindGroupLayout` for shader resource binding
- `BindGroup` for binding concrete resources to a layout
- `createRenderPipeline`, `createComputePipeline`, `createPipelineLayout`, `createBindGroupLayout`, `createBindGroup` on `Device`
- Vertex descriptor (`VertexDescriptor`) for vertex buffer layout definition
- Fragment descriptor (`FragmentDescriptor`)
- Depth/stencil descriptor (`DepthStencilDescriptor`)

**Resource types**
- `ShaderModule` — compiled shader bytecode (SPIR-V on Vulkan)
- `Sampler` with `SamplerDescriptor` (filter mode, wrap mode)
- `TextureView` type and `BeginRenderPassDescriptor` attachment support
- `QuerySet` with `QuerySetDescriptor`

**Constants reorganized into `constants/` subdirectory**
- `aspect.hpp`, `index_format.hpp`, `compare_op.hpp`, `filter_mode.hpp`
- `query_set_type.hpp`, `shader_stage.hpp`, `stencil_op.hpp`, `wrap_mode.hpp`
- Existing constants (`BufferUsage`, `CullMode`, `FrontFace`, `PixelFormat`, `PrimitiveTopology`, `TextureType`, `TextureUsage`, `StorageMode`, `ColorSpace`, `Feature`) moved to `inc/campello_gpu/constants/`

**Descriptors reorganized into `descriptors/` subdirectory**
- `RenderPipelineDescriptor`, `ComputePipelineDescriptor`, `PipelineLayoutDescriptor`
- `BindGroupDescriptor`, `BindGroupLayoutDescriptor`
- `SamplerDescriptor`, `QuerySetDescriptor`
- `BeginRenderPassDescriptor`
- `VertexDescriptor`, `ComputeDescriptor`

**Vulkan Android backend**
- Vulkan instance, physical device, logical device, and swapchain initialization
- Buffer creation (`VkBuffer` + device memory allocation)
- Texture creation (`VkImage`)
- Shader module loading from SPIR-V
- Render pipeline creation (`VkPipeline`)
- Compute pipeline creation
- Pipeline layout and descriptor set layout
- Command encoder and render/compute pass encoder implementations
- Sampler, QuerySet, Adapter implementations
- Vulkan validation layer support

**Android example app**
- Full Android example project under `examples/android/`
- Demonstrates buffer creation, shader loading, pipeline setup, and render loop

**Device API**
- `Device::getAdapters()` returning `Adapter` objects
- `Device::createDefaultDevice(pd)` convenience factory
- `Device::getEngineVersion()` / `getVersion()`
- `Device::getName()`, `Device::getFeatures()`

### Changed
- `render_pass.hpp` renamed to `render_pass_encoder.hpp` (`RenderPassEncoder`)
- `context.hpp` / `Context` replaced by `Sampler` / `sampler.hpp`
- `device_def.hpp` / `DeviceDef` replaced by `Adapter` / `adapter.hpp`
- `swap_chain.hpp` / `SwapChain` removed; swapchain managed internally by `DeviceData`
- Platform-independent source moved from `src/pi/device.cpp` to `src/pi/utils.cpp`
- CMake build split into platform-specific includes: `android.cmake`, `macos.cmake`, `windows.cmake`

### Fixed
- `getVersion()` linking error resolved
- CMake versioning propagated correctly via `configure_file`

---

## [0.0.3] - 2025-06-21

- CMake versioning support via `configure_file`
- `Device::getEngineVersion()` method

## [0.0.1] - Initial release

- Initial project structure
- Stub `Device`, `Buffer`, `Texture` public API
- Handle-based abstraction pattern established
