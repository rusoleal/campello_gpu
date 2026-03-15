# Changelog

All notable changes to campello_gpu are documented here.

## [0.3.3] - 2026-03-15

### Added
- Extended universal test suite — 38 new tests with zero GPU dependency:
  - `test_constants_extended.cpp`: `ShaderStage` flag uniqueness and combination, `TextureUsage` flag uniqueness, `TextureType` values, `IndexFormat`, `CullMode`, `FrontFace`, `CompareOp` (8 values), `StencilOp` (8 values), `PrimitiveTopology` (5 values), `StorageMode`, `Aspect`, `ColorSpace`, `ComponentType` (glTF numeric values), `AccessorType`, `StepMode`
  - `test_descriptors_extended.cpp`: `StencilDescriptor`, `DepthStencilDescriptor` (optional stencil faces), `VertexAttribute/Layout/Descriptor`, `ColorState`, `ColorWrite` flags, `FragmentDescriptor`, `RenderPipelineDescriptor` (optional fields), `ColorAttachment`, `LoadOp`/`StoreOp`, `BeginRenderPassDescriptor`, `BindGroupDescriptor`, `ComputeDescriptor`, `ComputePipelineDescriptor`
- Extended integration test suite — 20 new GPU tests covering `Texture` and `CommandEncoder`:
  - `test_texture.cpp`: all 8 getter methods (`getWidth`, `getHeight`, `getFormat`, `getDimension`, `getMipLevelCount`, `getSampleCount`, `getUsage`, `getDepthOrArrayLayers`), `createView` (explicit args, multiple views), `upload`
  - `test_command_encoder.cpp`: `finish()` without a pass, multiple concurrent encoders, `clearBuffer` (full and partial range), `copyBufferToBuffer` (aligned and with offsets), `beginComputePass`, `writeTimestamp`, `resolveQuerySet`

### Fixed
- **Vulkan** `texture.cpp`: `getDimension()` declared `uint32_t` in the implementation but `TextureType` in the header — corrected return type
- **Vulkan** `compute_pass_encoder.cpp`: missing `#include <vector>` caused build failure on Android NDK clang
- **Vulkan** `device.cpp`: `VK_LAYER_KHRONOS_validation` hard-required at instance creation; emulators/devices without the layer returned `VK_ERROR_LAYER_NOT_PRESENT` and crashed — validation layer now only enabled when present
- **Vulkan** `device.cpp`: `getAdapters()` passed `nullptr` to `vkEnumeratePhysicalDevices` when `getInstance()` failed — added null guard
- **Vulkan** `device.cpp`: `createDevice()` crashed on `vkCreateAndroidSurfaceKHR` when `pd == nullptr` (headless/test mode) — surface, swapchain, and semaphore creation are now skipped entirely when no window handle is provided
- **Vulkan** `device.cpp`: `vkGetDeviceQueue` always used queue family index `0` — now uses the resolved `queueFamilyIndex`
- **CMake** `tests/CMakeLists.txt`: `gtest_discover_tests` ran the integration test binary on the host at build time, which fails for cross-compiled Android targets and deleted the binary — switched to `DISCOVERY_MODE PRE_TEST`

### Changed
- Android NDK target API raised from 30 to 35 to expose Vulkan 1.3 core symbols (`vkCmdBeginRendering`, `vkCmdEndRendering`) in the linker stub

---

## [0.3.2] - 2026-03-15

### Added
- Doxygen-style documentation (`/** */` / `///`) for the entire public API surface:
  - All core handle classes (`Adapter`, `Buffer`, `Texture`, `TextureView`, `Sampler`, `ShaderModule`, `RenderPipeline`, `ComputePipeline`, `PipelineLayout`, `BindGroup`, `BindGroupLayout`, `QuerySet`, `CommandBuffer`, `CommandEncoder`, `RenderPassEncoder`, `ComputePassEncoder`, `Device`)
  - All descriptor structs (`BeginRenderPassDescriptor`, `BindGroupDescriptor`, `BindGroupLayoutDescriptor`, `QuerySetDescriptor`, `PipelineLayoutDescriptor`, `RenderPipelineDescriptor`, `VertexDescriptor`, `FragmentDescriptor`, `DepthStencilDescriptor`, `SamplerDescriptor`, `ComputePipelineDescriptor`, `ComputeDescriptor`)
  - All constant enums (`BufferUsage`, `TextureUsage`, `PixelFormat`, `PrimitiveTopology`, `CullMode`, `FrontFace`, `FilterMode`, `WrapMode`, `CompareOp`, `StencilOp`, `IndexFormat`, `TextureType`, `Feature`, `ShaderStage`, `Aspect`, `StorageMode`, `QuerySetType`, `ColorSpace`)
  - `State` placeholder class

---

## [0.3.1] - 2026-03-15

### Fixed
- `RenderPassEncoder` / `ComputePassEncoder` constructors — `renderCommandEncoder()` and `computeCommandEncoder()` return autoreleased Metal objects; the missing `retain()` call caused EXC_BAD_ACCESS when the run loop's autorelease pool drained at the end of each rendered frame

### Added
- `.github/workflows/ci.yml` — GitHub Actions CI pipeline triggered on every push and pull-request:
  - **Universal tests** on macOS, Linux, and Windows (no GPU required)
  - **Metal integration tests** on macOS (GH-hosted runners are real Apple hardware)
  - **iOS build check** — cross-compiles the Metal backend for the iOS simulator (arm64, no code signing required)
  - **Android build check** — cross-compiles the Vulkan backend with the NDK (`arm64-v8a`, API 28); marked `continue-on-error` until remaining Vulkan implementations are complete
- `ios.cmake` — CMake platform file for iOS; identical Metal backend to `macos.cmake`, target platform/sysroot handled by CMake toolchain
- `linux.cmake` — placeholder CMake platform file for Linux; builds `src/pi/` only so CMake can configure and universal tests can run without a Vulkan backend

---

## [0.3.0] - 2026-03-15

### Added
- **Complete Metal/macOS backend** — all public API methods now fully implemented:
  - `Device::createShaderModule()` — loads compiled `.metallib` via `dispatch_data`
  - `Device::createRenderPipeline()` — full MTL PSO with vertex/fragment functions, color attachments, depth format, and vertex descriptor
  - `Device::createComputePipeline()` — MTL compute PSO
  - `Device::createSampler()` — full filter/wrap/compare/anisotropy mapping
  - `Device::createQuerySet()` — backed by shared `MTL::Buffer` (8 bytes/slot)
  - `Device::createCommandEncoder()` — creates `MTL::CommandBuffer` from stored command queue
  - `Device::createBindGroupLayout()`, `createBindGroup()`, `createPipelineLayout()` — no-op placeholders (Metal uses implicit binding)
  - `CommandEncoder` — `beginRenderPass`, `beginComputePass`, `clearBuffer`, `copyBufferToBuffer`, `resolveQuerySet`, `finish`
  - `RenderPassEncoder` — `draw`, `drawIndexed`, `drawIndirect`, `drawIndexedIndirect`, `setPipeline`, `setVertexBuffer`, `setIndexBuffer`, `setViewport`, `setScissorRect`, `setStencilReference`, `beginOcclusionQuery`, `endOcclusionQuery`, `end`
  - `ComputePassEncoder` — `setPipeline`, `dispatchWorkgroups`, `dispatchWorkgroupsIndirect`, `setBindGroup`, `end`
  - `CommandBuffer` — wraps `MTL::CommandBuffer`
  - `TextureView` — `createView()` via `MTL::Texture::newTextureView`
  - `Texture::upload()` — via `MTL::Texture::replaceRegion`
  - `Device::getEngineVersion()` — returns current version string
- `Device::submit(shared_ptr<CommandBuffer>)` — commits the recorded command buffer; closes the frame loop story on all backends
- `TextureView::fromNative(void*)` — static bridge factory to wrap a platform-native texture handle (e.g. `id<MTLTexture>`) into a `TextureView`; retained on construction, released on destruction
- `MetalDeviceData` internal struct — holds `MTL::Device*` and `MTL::CommandQueue*` together behind `Device::native`
- `MetalRenderEncoderData` internal struct — stores `MTL::RenderCommandEncoder*` plus index buffer state for `setIndexBuffer` + `drawIndexed`
- `MetalComputeEncoderData` internal struct — stores `MTL::ComputeCommandEncoder*` plus current pipeline for threadgroup size derivation
- **Triangle example** (`examples/apple/`) — `Renderer.mm` fully rewritten to use campello_gpu for all GPU work; only `view.currentDrawable` and `[drawable present]` remain as native calls
- `TODO.md` — comprehensive task list covering bugs and missing implementations across Metal, Vulkan, and DirectX backends
- Integration tests for Metal (`tests/platform/`) — 14 device tests and 7 buffer tests covering all newly implemented factory methods; all pass on macOS with Metal

### Fixed
- `RenderPassEncoder` / `ComputePassEncoder` constructors — `renderCommandEncoder()` and `computeCommandEncoder()` return autoreleased objects; missing `retain()` caused EXC_BAD_ACCESS when the run loop's autorelease pool drained after each rendered frame
- `BindGroupDescriptor` — replaced raw `union` containing `std::shared_ptr` members (which implicitly deleted the destructor) with `std::variant<BufferBinding, shared_ptr<Texture>, shared_ptr<Sampler>>`
- macOS example `Renderer.mm` — replaced stale `Device::getDefaultDevice()` / `getDevices()` calls (removed API) with `Device::getAdapters()`
- `friend class Device` added to `BindGroupLayout` so `Device::createBindGroupLayout()` can construct instances

### Changed
- `macos.cmake` — added `src/pi/utils.cpp` and all 13 new Metal source files to the library target
- `examples/apple/campello_test/Shaders.metal` — replaced textured-box shader with a minimal triangle shader (`vertexMain` / `fragmentMain`) using hardcoded positions and per-vertex colours; no vertex buffer required
- Integration test `tryCreateDevice()` — enabled `Device::createDefaultDevice(nullptr)` path for `__APPLE__` (was returning `nullptr`, skipping all tests)

---

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
