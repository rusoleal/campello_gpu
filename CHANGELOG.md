# Changelog

All notable changes to campello_gpu are documented here.

## [Unreleased]

## [0.10.0] - 2026-04-22

### Added

- **Cubemap texture support** (`ttCube`, `ttCubeArray`) across all backends
  - `TextureType::ttCube = 5` and `TextureType::ttCubeArray = 6` added to public enum
  - WebGPU-style semantics: storage is a 2D texture with `depthOrArrayLayers = 6` (or multiple thereof); cubemap semantics come from the **view** dimension passed to `Texture::createView(..., dimension)`
  - **Metal**: `createTexture()` maps `ttCube` → `MTLTextureTypeCube`, `ttCubeArray` → `MTLTextureTypeCubeArray`; `tt2d` with `depth > 1` uses `MTLTextureType2DArray`; `getDimension()` and `getDepthOrArrayLayers()` updated for cube types
  - **Vulkan**: `createTexture()` sets `arrayLayers` from `depth` for 2D textures, adds `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` for cube-compatible images; `createView()` supports `VK_IMAGE_VIEW_TYPE_CUBE` and `VK_IMAGE_VIEW_TYPE_CUBE_ARRAY`
  - **DirectX**: `createView()` bug fixed — now correctly switches on the `dimension` parameter (was erroneously using `h->dimension`); added `D3D12_SRV_DIMENSION_TEXTURECUBE` and `D3D12_SRV_DIMENSION_TEXTURECUBEARRAY` descriptors
  - Integration tests: `CreateCubeTextureDirectly`, `CreateCubeArrayTextureDirectly`, `CreateCubeViewFrom2DArray`, `CreateCubeArrayViewFrom2DArray`

- **Linux Vulkan backend** — full headless Vulkan 1.x backend for Linux (`src/vulkan_linux/`)
  - Copy-and-adapt from the production-ready Android Vulkan backend
  - All core API implemented: Device, Adapter, Buffer, Texture, ShaderModule, RenderPipeline, ComputePipeline, BindGroup, CommandEncoder, RenderPassEncoder, ComputePassEncoder, QuerySet, Sampler, Fence
  - Dynamic rendering (`VK_KHR_dynamic_rendering`) for render passes
  - GPU timing via timestamp queries
  - Full metrics & observability: resource counters, memory tracking, pass performance stats, memory pressure management
  - Proper device-local image memory allocation for textures (fixed missing image memory bind from Android backend)
  - Headless-only in this release: windowed X11/Wayland swapchain support deferred to a future phase
  - Hardware ray tracing deferred: `AccelerationStructure`, `RayTracingPipeline`, and `RayTracingPassEncoder` compile but return nullptr until RT extensions are wired up
  - `linux.cmake` updated to build a full `SHARED` library linking `Vulkan::Vulkan`
  - Integration tests updated to run on Linux (`__linux__` added to all `tryCreateDevice()` helpers)

---

## [0.9.0] - 2026-04-19

### Added

- **CMake package installation support** — `campello_gpu` can now be consumed via `find_package(campello_gpu)` in addition to `FetchContent`/`add_subdirectory`
  - `include(GNUInstallDirs)` for standard install path variables
  - `campello_gpu::campello_gpu` alias target works transparently in both `add_subdirectory` and `find_package` contexts
  - `install(TARGETS)` exports the library to `lib/cmake/campello_gpu/campello_gpuTargets.cmake`
  - `install(DIRECTORY inc/)` installs public headers to the system include path
  - `cmake/campello_gpuConfig.cmake.in` — package config template for `find_package` discovery
  - `campello_gpuConfigVersion.cmake` generated via `write_basic_package_version_file` with `SameMajorVersion` compatibility

---

## [0.8.1] - 2026-04-12

### Added

- **New Device synchronization API**
  - `Device::scheduleNextPresent(void* nativeDrawable)` — schedules a platform drawable to be presented after the next submitted command buffer completes; on Metal this fixes "present before render" artefacts by tying presentation to GPU completion and display vsync; no-op on Vulkan and DirectX (presentation handled inside `submit()`)
  - `Device::waitForIdle()` — blocks the calling thread until all previously submitted commands have finished executing on the GPU; useful for cross-device synchronization when one device renders to a texture that another will read

### Fixed

- **Metal** `RenderPassEncoder::setBindGroup` and `RayTracingPassEncoder::setBindGroup` — texture handles were incorrectly cast via `reinterpret_cast` instead of accessing the native handle through the proper indirection, causing crashes when binding textures; corrected to use `tvh->cpuHandle.ptr` for texture and sampler binding

### Test Coverage

- 4 new `RenderPassEncoder` tests for `setBindGroup`:
  - `SetBindGroupWithNullDoesNotCrash`
  - `SetBindGroupWithEmptyGroupDoesNotCrash`
  - `SetBindGroupWithTextureDoesNotCrash` — validates the texture handle casting fix
  - `SetBindGroupWithSamplerDoesNotCrash`

---

## [0.8.0] - 2026-04-12

### Added

**Comprehensive Metrics & Observability System (3-Phase Implementation)**

- **Phase 1 — Resource & Command Counters**
  - `ResourceCounters` struct — live counts of buffers, textures, pipelines, acceleration structures, shader modules, samplers, bind groups, etc.
  - `CommandStats` struct — accumulated submission statistics: command buffers submitted, render/compute/ray tracing passes, draw/dispatch/trace/copy counts
  - `Device::getResourceCounters()`, `Device::getCommandStats()`, `Device::resetCommandStats()`
  - `Metrics` aggregate struct combining memory info, resource counters, and command stats
  - `Device::getMetrics()` convenience method returning complete snapshot

- **Phase 2 — Memory Usage Tracking**
  - `ResourceMemoryStats` struct — per-resource-type byte tracking:
    - `bufferBytes`, `textureBytes`, `accelerationStructureBytes`, `shaderModuleBytes`, `querySetBytes`
    - `totalTrackedBytes` (sum of all types)
    - Peak tracking: `peakBufferBytes`, `peakTextureBytes`, `peakAccelerationStructureBytes`, `peakTotalBytes`
  - `Device::getResourceMemoryStats()` — current memory breakdown
  - `Device::resetPeakMemoryStats()` — reset high-water marks
  - Automatic byte tracking on all resource create/destroy operations across all backends
  - Atomic counters ensure thread-safe updates without contention

- **Phase 3 — GPU Timing & Memory Pressure Management**
  - **GPU Timestamp Collection** (all backends):
    - `CommandBuffer::getGPUExecutionTime()` — returns actual GPU execution time in nanoseconds
    - Metal: uses `MTLCommandBuffer::GPUStartTime`/`GPUEndTime` with timestamp calibration via `sampleTimestamps`
    - Vulkan: uses `VK_QUERY_TYPE_TIMESTAMP` with query pools; start (`TOP_OF_PIPE`) and end (`BOTTOM_OF_PIPE`) timestamps
    - DirectX 12: uses `D3D12_QUERY_HEAP_TYPE_TIMESTAMP` with `EndQuery`/`ResolveQueryData` to readback buffer
  - `PassPerformanceStats` struct — accumulated GPU timing per pass type (render/compute/ray tracing)
  - `Device::getPassPerformanceStats()`, `Device::resetPassPerformanceStats()`
  - **Memory Pressure Management**:
    - `MemoryPressureLevel` enum — `Normal`, `Warning`, `Critical`
    - `MemoryBudget` struct — configurable thresholds (`warningThresholdPercent` 80%, `criticalThresholdPercent` 95%, `targetUsagePercent` 70%)
    - `MemoryPressureCallback` type — user-registered callback invoked on pressure level changes
    - `Device::getMemoryPressureLevel()`, `Device::setMemoryBudget()`, `Device::getMemoryBudget()`
    - `Device::setMemoryPressureCallback()`, `Device::checkMemoryPressure()`
  - `MetricsWithTiming` struct — extends `Metrics` with GPU pass performance data
  - `Device::getMetricsWithTiming()` — complete profiling snapshot

### Changed

- **Metal** `Buffer` and `Texture` handles now store `allocatedSize` and `deviceData` pointer for memory tracking
- **Vulkan** `CommandBufferHandle` extended with GPU timing fields (timestamp query results)
- **DirectX** `CommandBufferHandle` extended with `ID3D12QueryHeap*` and `ID3D12Resource*` for timestamp queries
- Internal `MetalDeviceData`, `DeviceData` (Vulkan), `DeviceData` (DirectX) extended with metrics counters

### Test Coverage

- 29 Device tests covering all metrics phases:
  - Resource counters (buffer, texture, sampler creation)
  - Memory stats (bytes tracked, peak tracking)
  - GPU timing (pass performance stats)
  - Memory pressure (budget configuration, callbacks)
- 2 CommandBuffer tests for GPU execution time retrieval

---

## [0.7.1] - 2026-04-11

### Fixed

- **Metal** `Device::createTexture()` — use `MTLStorageModeShared` instead of `MTLStorageModeManaged` for color textures on iOS and Simulator; `Managed` is macOS-only and caused a validation failure on iOS targets
- **Metal** `Device::createBuffer()` — use `MTLResourceStorageModeShared` instead of `MTLResourceStorageModeManaged` for non-mappable buffers on iOS and Simulator; same macOS-only restriction applies

---

## [0.7.0] - 2026-04-07

### Added

- **Ray tracing support** — full hardware-accelerated ray tracing on all three backends (Vulkan/Android, Metal/macOS+iOS, DirectX 12/Windows)
- **New public types**: `AccelerationStructure`, `RayTracingPipeline`, `RayTracingPassEncoder`
- **New descriptors**: `AccelerationStructureGeometryDescriptor`, `BottomLevelAccelerationStructureDescriptor`, `TopLevelAccelerationStructureDescriptor`, `RayTracingPipelineDescriptor`, `RayTracingShaderDescriptor`
- **New constants**: `AccelerationStructureBuildFlag` (`preferFastTrace`, `preferFastBuild`, `allowUpdate`, `allowCompaction`), `AccelerationStructureGeometryType` (`triangles`, `aabbs`), `ShaderStage` values for `rayGeneration`, `rayMiss`, `rayClosestHit`, `rayAnyHit`, `rayIntersection`
- **New `BufferUsage` values**: `accelerationStructureInput`, `accelerationStructureStorage`
- **`BindGroup` acceleration structure binding** — `EntryObjectType::accelerationStructure` for binding TLAS/BLAS in bind groups
- **`Device` RT factory methods**: `createBottomLevelAccelerationStructure()`, `createTopLevelAccelerationStructure()`, `createRayTracingPipeline()`
- **`CommandEncoder` RT commands**: `buildAccelerationStructure()` (BLAS and TLAS overloads), `updateAccelerationStructure()`, `copyAccelerationStructure()`, `beginRayTracingPass()`
- **`RayTracingPassEncoder`**: `setPipeline()`, `setBindGroup()`, `traceRays(width, height, depth)`, `end()`
- **Vulkan backend** (`src/vulkan_android/`): BLAS/TLAS via `VK_KHR_acceleration_structure`, RT pipeline via `VK_KHR_ray_tracing_pipeline`, SBT construction, `vkCmdTraceRaysKHR` dispatch
- **Metal backend** (`src/metal/`): BLAS/TLAS via `MTLPrimitiveAccelerationStructure`/`MTLInstanceAccelerationStructure`, RT pipeline via `MTLComputePipelineState` + `metal::raytracing::intersector`
- **DirectX 12 backend** (`src/directx/`): BLAS/TLAS via `ID3D12Device5::BuildRaytracingAccelerationStructure`, RT pipeline via `CreateStateObject` (DXR state object), shader table construction, `DispatchRays`
- **Ray tracing integration tests** (`tests/platform/test_raytracing.cpp`): 12 GPU tests, skipped automatically when `Feature::raytracing` is absent
- **Apple ray tracing example** (`examples/apple/`): Metal shader (`RaytracingShaders.metal`) + ObjC++ demo (`RaytracingDemo.mm`) — single-triangle BLAS/TLAS, barycentric colour shading
- **Android ray tracing example** (`examples/android/`): C++ demo (`RaytracingDemo.cpp`) — BLAS/TLAS via campello_gpu, SPIR-V shaders loaded from APK assets, GLSL reference source in comments

---

## [0.6.1] - 2026-04-06

### Fixed

- **DirectX** `CommandEncoder::copyBufferToTexture()` — implemented full 6-parameter version (`source`, `sourceOffset`, `bytesPerRow`, `destination`, `mipLevel`, `arrayLayer`); uses `GetCopyableFootprints` for subresource layout, overrides `RowPitch` when `bytesPerRow` is provided, and performs `COMMON→COPY_DEST→COMMON` resource barrier transitions around `CopyTextureRegion`; previously a no-op stub

---

## [0.6.0] - 2026-04-06

### Added

- **Vulkan** `CommandEncoder::copyBufferToTexture()` — proper 6-parameter implementation (`source`, `sourceOffset`, `bytesPerRow`, `destination`, `mipLevel`, `arrayLayer`) with layout transitions; public header signature updated to match (previously a no-parameter stub)
- **Vulkan** `CommandEncoder::copyTextureToBuffer()` — implemented via `vkCmdCopyImageToBuffer` with `TRANSFER_SRC_OPTIMAL` layout transitions and layout restore
- **Vulkan** `CommandEncoder::copyTextureToTexture()` — implemented via `vkCmdCopyImage` with per-image layout transitions
- **Vulkan** `CommandEncoder::beginRenderPass()` — offscreen rendering path: when `colorAttachments[0].view` is set, uses the provided `TextureView` image instead of acquiring a swapchain image; depth/stencil attachment from `descriptor.depthStencilAttachment` now fully wired (`pDepthAttachment`, `pStencilAttachment`) with format-based aspect detection and `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` transition
- **Vulkan** `RenderPassEncoder::setBindGroup()` — implemented via `vkCmdBindDescriptorSets` on the graphics bind point using the pipeline layout cached by `setPipeline()`
- **Vulkan** `RenderPassEncoder::beginOcclusionQuery()` / `endOcclusionQuery()` — implemented via `vkCmdBeginQuery` / `vkCmdEndQuery`; `VkQueryPool` wired from `BeginRenderPassDescriptor::occlusionQuerySet`
- **Vulkan** `RenderPassEncoder::end()` — branches on swapchain vs offscreen: swapchain images transition to `PRESENT_SRC_KHR`, offscreen images to `GENERAL`
- **Vulkan** `ComputePassEncoder::setBindGroup()` — pipeline layout now correctly sourced from the handle (cached by `setPipeline()`) instead of `VK_NULL_HANDLE`
- **Vulkan** `Texture::upload()` — rewrote from staging-buffer-only to a full one-shot command buffer that issues `vkCmdCopyBufferToImage` with `UNDEFINED→TRANSFER_DST_OPTIMAL→SHADER_READ_ONLY_OPTIMAL` transitions; all mip levels covered
- **Vulkan** `Texture::download()` — implemented: allocates host-visible readback buffer, one-shot command buffer, `TRANSFER_SRC_OPTIMAL` transition, `vkCmdCopyImageToBuffer`, synchronous fence wait, memcpy to CPU
- **Vulkan** `Buffer::download()` — implemented via `vkMapMemory` + `vkInvalidateMappedMemoryRanges`
- **Vulkan** `Adapter::getFeatures()` — now queries `vkGetPhysicalDeviceFeatures` and `vkGetPhysicalDeviceFormatProperties`; reports `geometryShader`, `bcTextureCompression`, `depth24Stencil8PixelFormat`; `getAdapters()` stores `VkPhysicalDevice` directly in `native` instead of an index
- **Vulkan** `Device::createRenderPipeline()` — depth/stencil state (`VkPipelineDepthStencilStateCreateInfo`) now fully populated from `descriptor.depthStencil` including compare op, write enable, stencil front/back ops, read/write masks; wired to `pDepthStencilState`; `pipelineRenderingCreateInfo` now sets `depthAttachmentFormat` and `stencilAttachmentFormat` from `descriptor.depthStencil->format`
- **Vulkan** `Device::submit()` — swapchain recreation on `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR` from `vkQueuePresentKHR`: re-queries surface capabilities, rebuilds swapchain with `oldSwapchain`, destroys old image views, recreates image views at the new size
- **Vulkan** Swapchain format selection — now prefers `VK_FORMAT_B8G8R8A8_SRGB` / `VK_FORMAT_R8G8B8A8_SRGB` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`; falls back to `surfaceFormats[0]`
- **API** `RenderPipelineDescriptor::layout` — new optional `std::shared_ptr<PipelineLayout>` field; when set, the caller's `VkPipelineLayout` is used directly, enabling `setBindGroup()` in render pipelines; when absent, an empty layout is auto-created

### Fixed

- **Vulkan** `Device::~Device()` — leaked `VkSwapchainKHR`, two `VkSemaphore`, all swapchain `VkImageView`s, `VkSurfaceKHR`, and `VkDescriptorPool`; all now destroyed in correct order behind `vkDeviceWaitIdle`
- **Vulkan** `createRenderPipeline()` `stageCount` — was hardcoded to `2`; vertex-only pipelines (no fragment shader) would submit an incorrect count; now uses `shaderStages.size()`
- **Vulkan** `createTexture()` default image view — always used `VK_IMAGE_ASPECT_COLOR_BIT` for all formats; depth/stencil textures now get `DEPTH_BIT`, `STENCIL_BIT`, or `DEPTH_BIT | STENCIL_BIT` based on format
- **Vulkan** `createRenderPipeline()` — set `pipelineInfo.renderPass` to a real `VkRenderPass` despite using `VK_KHR_dynamic_rendering`; spec requires `VK_NULL_HANDLE`; dead render pass creation removed
- **Vulkan** `Device::submit()` — always passed swapchain semaphores regardless of whether a swapchain was present; headless and compute-only submissions now use no semaphores
- **Vulkan** `Device::createCommandEncoder()` — `vkAllocateCommandBuffers` return value was unchecked; now returns `nullptr` on failure
- **Vulkan** Debug log strings `"pepe1"`, `"pepe2"`, `"pepe3"` removed from `device.cpp`
- **API** `DepthStencilAttachment::stencilRadOnly` — renamed to `stencilReadOnly` (typo in public header)

### Changed

- **Vulkan** `createTexture()` default view — `TextureViewHandle` now stores `VkFormat format` for use in depth attachment setup and format-dependent logic
- **Vulkan** `RenderPassEncoderHandle` — extended with `isSwapchain`, `currentSwapchainImage`, `offscreenImage`, `offscreenExtent`, `queryPool`, `pipelineLayout` fields
- **Vulkan** `RenderPipelineHandle` — `VkRenderPass renderPass` removed (no longer created); `ownsPipelineLayout` flag added to track ownership of auto-created layouts

---

## [0.5.2] - 2026-04-06

### Fixed
- **Android** `texture_view.cpp` — `TextureView::fromNative` used `static_cast<VkImageView>(nativeTex)` which is rejected on 32-bit ABIs (`armeabi-v7a`, `x86`) where `VkImageView` is `uint64_t` and not a pointer type; changed to `reinterpret_cast`

### CI
- **Android** build matrix expanded from a single `arm64-v8a` job to all four supported ABIs: `arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`
- Removed `continue-on-error: true` from the Android CI job — the Vulkan backend now links successfully on all ABIs

---

## [0.5.1] - 2026-04-06

### Fixed
- **Linux** `pixel_format.hpp` — added missing `#include <cstdint>` causing `uint32_t` build errors on GCC
- **Android** `command_encoder.cpp` — fixed `copyTextureToBuffer` signature mismatch between header declaration and implementation
- **Android** dynamic rendering — `vkCmdBeginRendering`/`vkCmdEndRendering` are Vulkan 1.3 functions; now loaded dynamically via `vkGetDeviceProcAddr` as `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR` for compatibility with Android API 28 (Vulkan 1.1)

---

## [0.5.0] - 2026-03-30

### Added
- **New types** `Offset3D` and `Extent3D` in `campello_gpu/types/` — 3D signed offset and unsigned extent structures for texture/buffer operations
- **Tests** `CommandEncoder::copyTextureToTexture` — 3 integration tests covering full copy, offset-based copy, and partial region copy scenarios

### Changed
- **API** `CommandEncoder::copyTextureToTexture` — signature changed from `(source, destination, width, height)` to `(source, sourceOffset, destination, destinationOffset, extent)` enabling sub-rectangle copies with source and destination offsets
- **Metal** `copyTextureToTexture` — fully implemented using new `Offset3D` and `Extent3D` types mapped to `MTL::Origin` and `MTL::Size`
- **DirectX 12** `copyTextureToTexture` — signature updated (implementation pending)
- **Vulkan** `copyTextureToTexture` — signature updated (implementation pending)

---

## [0.4.1] - 2026-03-28

### Fixed
- **Metal** `command_encoder.hpp` — missing `#include <campello_gpu/texture.hpp>` caused `Texture` to be unresolvable when `Metal.hpp` was included first in `command_encoder.cpp`, as `MTL::Texture` shadowed the public type
- **Metal** `createShaderModule` — was compiling bytecode immediately via `newLibrary()`, rejecting arbitrary or empty bytes; now stores raw bytes and defers compilation to pipeline creation time, matching the intended lazy-validation contract
- **Metal** `createRenderPipeline` — now returns `nullptr` when `descriptor.vertex.module` is null instead of crashing Metal with a missing vertex function
- **Metal** `ComputePassEncoder::dispatchWorkgroups` / `dispatchWorkgroupsIndirect` — calling these without a compute pipeline set caused a Metal crash; both methods now early-return when no pipeline has been bound

---

## [0.4.0] - 2026-03-28

### Added
- **All backends** GPU→CPU readback support — new `Buffer::download()` method for reading back buffer data to CPU memory
- **All backends** `CommandEncoder::copyTextureToBuffer()` — copies texture subresource data to a buffer for readback operations
- **All backends** `Texture::download()` convenience method — synchronous texture readback that handles buffer creation, command submission, and data copy
- **DirectX 12** Readback heap buffer support in `createBuffer()` — buffers created with `BufferUsage::copyDst | BufferUsage::mapRead` use `D3D12_HEAP_TYPE_READBACK`
- **Tests** Buffer upload/download roundtrip tests with random data verification
- **Tests** Texture upload/download roundtrip tests for RGBA8, R8 formats and various sizes

### Changed
- **DirectX 12** `BufferHandle` now tracks `isReadback` flag and `queue` pointer for readback operations

---

## [0.3.9] - 2026-03-23

### Fixed
- **DirectX 12** `RenderPassEncoder::setVertexBuffer` — `StrideInBytes` was hardcoded to `0`; vertex strides are now stored in `RenderPipelineHandle` at pipeline creation time, copied into `RenderPassEncoderHandle` on `setPipeline`, and applied correctly when building the `D3D12_VERTEX_BUFFER_VIEW`

---

## [0.3.8] - 2026-03-23

### Added
- **DirectX 12** `drawIndirect` / `drawIndexedIndirect` on `RenderPassEncoder` — implemented via `ID3D12CommandSignature`; signatures are created lazily on first use and cached on `DeviceData`
- **DirectX 12** `dispatchWorkgroupsIndirect` on `ComputePassEncoder` — implemented via `ID3D12CommandSignature` with the same lazy-cache pattern
- **DirectX 12** `beginOcclusionQuery` / `endOcclusionQuery` on `RenderPassEncoder` — propagates `QuerySet` heap and type from `BeginRenderPassDescriptor::occlusionQuerySet` into the encoder handle; calls `BeginQuery` / `EndQuery` on the command list
- **DirectX 12** `RenderPassEncoder::setBindGroup` — binds SRV/CBV and sampler descriptor tables via `SetGraphicsRootDescriptorTable`
- **DirectX 12** `getSwapchainTextureView` — lazily resizes the swapchain when the window dimensions change (stores `HWND` in `DeviceData`; calls `ResizeBuffers` + recreates RTVs before returning the current back-buffer view)
- **Tests** Windows DLL copy step in `tests/CMakeLists.txt` — copies `campello_gpu.dll` next to the test binary on `WIN32` so CTest can locate it during discovery and execution

### Fixed
- **DirectX 12** `CommandEncoder::beginComputePass` — `deviceData` was not forwarded to `ComputePassEncoderHandle`, making indirect dispatch and future device-dependent operations unavailable inside compute passes
- **DirectX 12** `Device` destructor — cached command signatures (`drawCmdSig`, `drawIndexedCmdSig`, `dispatchCmdSig`) were not released

---

## [0.3.7] - 2026-03-21

### Added
- **All backends** Alpha-blending support in `createRenderPipeline` — new public types `BlendFactor`, `BlendOperation`, `BlendComponent`, and `BlendState` in `fragment_descriptor.hpp`; `ColorState` gains an `std::optional<BlendState> blend` field. When set, blending is enabled for that color attachment; when absent (default), the attachment remains opaque
- **Metal** `createRenderPipeline` — reads `ColorState::blend` and calls `setBlendingEnabled` / `setRgbBlendOperation` / `setSourceRGBBlendFactor` / `setDestinationRGBBlendFactor` / `setAlphaBlendOperation` / `setSourceAlphaBlendFactor` / `setDestinationAlphaBlendFactor` on each color attachment
- **Vulkan** `createRenderPipeline` — reads `ColorState::blend` per target and builds a `VkPipelineColorBlendAttachmentState` vector with correct `VkBlendFactor` / `VkBlendOp` values (explicit mapping required as Vulkan and Metal factor orderings differ)
- **DirectX 12** `createRenderPipeline` — reads `ColorState::blend` per target and fills `D3D12_RENDER_TARGET_BLEND_DESC` using `toD3D12Blend` / `toD3D12BlendOp` helpers; sets `IndependentBlendEnable = TRUE` when multiple render targets are present

---

## [0.3.6] - 2026-03-21

### Fixed
- **Metal** `Device::createTexture` — depth/stencil pixel formats (`depth16unorm`, `depth32float`, `depth24plus_stencil8`, `depth32float_stencil8`, `stencil8`) were always created with `StorageModeManaged`, which is invalid on macOS; they are now created with `StorageModePrivate`

---

## [0.3.5] - 2026-03-21

### Fixed
- **Metal** `RenderPassEncoder::setBindGroup` — method was entirely absent from `RenderPassEncoder`; textures, samplers, and buffers bound via a `BindGroup` were never forwarded to the Metal encoder, making textured rendering impossible
- **Metal** `Device::createBindGroup` — all resource entries from `BindGroupDescriptor` were discarded (`nullptr` stored); the bind group now retains a `MetalBindGroupData` struct containing the full entry list
- **Metal** `BindGroup` destructor — now correctly frees the `MetalBindGroupData` allocation

### Added
- **Metal** `RenderPassEncoder::setBindGroup` — iterates bind group entries and calls the appropriate Metal encoder methods (`setFragmentTexture`/`setVertexTexture`, `setFragmentSamplerState`/`setVertexSamplerState`, `setFragmentBuffer`/`setVertexBuffer`) so textures, samplers, and buffers are bound to both pipeline stages

---

## [0.3.4] - 2026-03-16

### Added
- Extended Windows/DirectX 12 integration test suite — 36 new GPU tests across 4 new files and 1 existing file:
  - `test_shader_module.cpp`: `createShaderModule` with arbitrary bytes, empty bytes, and multiple concurrent modules
  - `test_render_pipeline.cpp`: `createRenderPipeline` null/empty vertex shader (expected-null paths); `createComputePipeline` null/empty compute shader, with and without a `PipelineLayout`
  - `test_render_pass_encoder.cpp`: `beginRenderPass` (no attachments, `LoadOp::load`, `LoadOp::clear`); `setViewport` (origin zero and non-zero); `setScissorRect`; combined viewport + scissor; `setStencilReference` (zero and non-zero); `setVertexBuffer` (slot 0 and with byte offset); `setIndexBuffer` (`uint16` and `uint32`); `draw` and `drawIndexed`; `end` + `finish`; full off-screen clear pass end-to-end
  - `test_compute_pass_encoder.cpp`: `dispatchWorkgroups` (single, large, and repeated dispatches); `end`; `setBindGroup` with null and with a real `BindGroup`; `finish` after `end` and after `dispatchWorkgroups`
  - `test_command_encoder.cpp`: `Device::submit` with an empty command buffer

---

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
