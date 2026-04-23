# campello_gpu — Pending Tasks

## Bugs (breaking / incorrect behavior)

- [x] **[Vulkan]** `Device::~Device()` — leaked `VkSwapchainKHR`, `VkSemaphore` ×2, all `VkImageView` swapchain views, `VkSurfaceKHR`, `VkDescriptorPool`; now fully destroyed in order with `vkDeviceWaitIdle` guard
- [x] **[Vulkan]** `createRenderPipeline()` `stageCount` hardcoded to `2` — vertex-only pipelines (no fragment shader) would pass a wrong count; now uses `shaderStages.size()`
- [x] **[Vulkan]** `createTexture()` default view always used `VK_IMAGE_ASPECT_COLOR_BIT` for all formats — depth/stencil textures now correctly use `DEPTH_BIT`, `STENCIL_BIT`, or both based on format
- [x] **[Vulkan]** `createRenderPipeline()` set `pipelineInfo.renderPass` to a real `VkRenderPass` — dynamic rendering requires `VK_NULL_HANDLE`; dead `VkRenderPass` creation removed
- [x] **[Vulkan]** `createRenderPipeline()` always created an empty pipeline layout (`setLayoutCount = 0`) ignoring bind groups — `RenderPipelineDescriptor` now has an optional `layout` field; when set, the caller's `VkPipelineLayout` is used directly
- [x] **[API]** `DepthStencilAttachment::stencilRadOnly` typo in public header — renamed to `stencilReadOnly`
- [x] **[Vulkan]** `Device::submit()` — `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` from `vkQueuePresentKHR` now trigger swapchain recreation via `recreateSwapchain()`: re-queries surface capabilities, rebuilds swapchain with `oldSwapchain`, destroys old image views, recreates image views at the new size
- [x] **[Vulkan]** `Device::createCommandEncoder()` — `vkAllocateCommandBuffers` return value was unchecked; now returns `nullptr` on failure

- [x] **[Metal]** `RenderPassEncoder` / `ComputePassEncoder` constructors — `renderCommandEncoder()` and `computeCommandEncoder()` return autoreleased objects; missing `retain()` in constructors caused EXC_BAD_ACCESS when autorelease pool drained after each frame
- [x] **[Vulkan]** `Device::createBindGroupLayout()` — function had no `return` statement; now fully implemented with proper `descriptorCount`, `stageFlags`, and `VkDescriptorSetLayout` creation
- [x] **[Vulkan]** `Device::getFeatures()` — was casting `native` (`DeviceData*`) directly to `VkPhysicalDevice`; fixed to use `deviceData->physicalDevice`
- [x] **[Vulkan]** `CommandEncoder::beginRenderPass()` — was missing return statement; now returns a valid `RenderPassEncoder`
- [x] **[Vulkan]** `CommandEncoder::finish()` — was empty; now calls `vkEndCommandBuffer` and returns a `CommandBuffer`
- [x] **[Vulkan]** `Buffer::getLength()` — was always returning `0`; `BufferHandle` now stores allocation size
- [x] **[Vulkan]** Memory cleanup on `createBuffer` failure — `bufferHandle` is now destroyed when `vkAllocateMemory` fails
- [x] **[DirectX]** `device.cpp` implements stale API (`getDefaultDevice`, `getDevices`, old `createBuffer`/`createTexture` signatures) — entire file rewritten to match `device.hpp`
- [x] **[Vulkan]** `Texture::upload()` — rewrote to issue `vkCmdCopyBufferToImage` via one-shot command buffer with proper UNDEFINED→TRANSFER_DST_OPTIMAL→SHADER_READ_ONLY_OPTIMAL layout transitions
- [x] **[Vulkan]** `ComputePassEncoder::setBindGroup()` — `ComputePassEncoderHandle` now stores `pipelineLayout`; `setPipeline()` caches it so `setBindGroup()` passes the correct layout
- [x] **[Vulkan]** `CommandEncoder::beginRenderPass()` — now branches on `colorAttachments[0].view`: offscreen path uses provided TextureView image; swapchain path acquires via `vkAcquireNextImageKHR`
- [x] **[Vulkan]** `Device::submit()` — semaphore usage gated on `cbHandle->hasSwapchain`; headless/compute submissions use no semaphores
- [x] **[Vulkan]** `Device::createRenderPipeline()` — `depthStencil` now zero-initialized and fully populated from `descriptor.depthStencil`; wired to `pipelineInfo.pDepthStencilState`
- [x] **[Vulkan]** Swapchain format/color-space selection always picks `surfaceFormats[0]` — now prefers `VK_FORMAT_B8G8R8A8_SRGB` / `VK_FORMAT_R8G8B8A8_SRGB` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`, falls back to first available
- [x] **[Vulkan]** Debug log strings `"pepe1"`, `"pepe2"`, `"pepe3"` removed from `device.cpp`

## Missing implementations — Vulkan/Android

- [x] `Device::createBindGroup()` — implemented
- [x] `Device::getName()` — now queries `VkPhysicalDeviceProperties::deviceName`
- [x] `Device::getEngineVersion()` — now queries `vkEnumerateInstanceVersion`
- [x] `CommandEncoder::beginComputePass()` — implemented; `ComputePassEncoder` source file added
- [x] `Texture::createView()` — implemented via `VkImageViewCreateInfo`
- [x] `Texture` getters (`getFormat`, `getWidth`, `getHeight`, `getMipLevelCount`, `getSampleCount`, `getUsage`, `getDimension`) — implemented
- [x] `CommandEncoder::clearBuffer()` — implemented via `vkCmdFillBuffer`
- [x] `CommandEncoder::copyBufferToBuffer()` — implemented via `vkCmdCopyBuffer`
- [x] `CommandEncoder::copyTextureToBuffer()` — implemented via `vkCmdCopyImageToBuffer` with layout transitions
- [x] `CommandEncoder::copyTextureToTexture()` — implemented via `vkCmdCopyImage` with layout transitions
- [x] `CommandEncoder::resolveQuerySet()` — implemented via `vkCmdCopyQueryPoolResults`
- [x] `CommandEncoder::writeTimestamp()` — implemented via `vkCmdWriteTimestamp`
- [x] `RenderPassEncoder::draw()` — implemented via `vkCmdDraw`
- [x] `RenderPassEncoder::drawIndexed()` — implemented via `vkCmdDrawIndexed`
- [x] `RenderPassEncoder::drawIndirect()` — implemented via `vkCmdDrawIndirect`
- [x] `RenderPassEncoder::drawIndexedIndirect()` — implemented via `vkCmdDrawIndexedIndirect`
- [x] `RenderPassEncoder::end()` — implemented via `vkCmdEndRenderingKHR` + layout barrier
- [x] `RenderPassEncoder::setPipeline()` — implemented via `vkCmdBindPipeline`; also caches pipeline layout for `setBindGroup`
- [x] `RenderPassEncoder::setVertexBuffer()` — implemented via `vkCmdBindVertexBuffers`
- [x] `RenderPassEncoder::setIndexBuffer()` — implemented via `vkCmdBindIndexBuffer`
- [x] `RenderPassEncoder::setViewport()` — implemented via `vkCmdSetViewport`
- [x] `RenderPassEncoder::setScissorRect()` — implemented via `vkCmdSetScissor`
- [x] `RenderPassEncoder::setStencilReference()` — implemented via `vkCmdSetStencilReference`
- [x] `RenderPassEncoder::setBindGroup()` — implemented via `vkCmdBindDescriptorSets` (GRAPHICS bind point)
- [x] All `ComputePassEncoder` methods (`dispatchWorkgroups`, `dispatchWorkgroupsIndirect`, `end`, `setBindGroup`, `setPipeline`) — implemented
- [x] `Buffer::download()` — implemented via `vkMapMemory` + `vkInvalidateMappedMemoryRanges`
- [x] `Texture::download()` — implemented: allocates readback buffer, one-shot command buffer, layout transitions, `vkCmdCopyImageToBuffer`, synchronous fence wait
- [x] `CommandEncoder::copyBufferToTexture()` — implemented (Vulkan + Metal); public header updated with proper 6-parameter signature
- [x] `RenderPassEncoder::beginOcclusionQuery()` / `endOcclusionQuery()` — implemented via `vkCmdBeginQuery`/`vkCmdEndQuery`; `queryPool` wired from `BeginRenderPassDescriptor::occlusionQuerySet`
- [x] `Adapter::getFeatures()` — implemented; `getAdapters()` now stores `VkPhysicalDevice` directly in `native`; queries `vkGetPhysicalDeviceFeatures` for geometry shader, BC compression, and depth24+stencil8 format support
- [x] `CommandEncoder::beginRenderPass()` depth/stencil attachment — `pDepthAttachment` and `pStencilAttachment` now populated from `descriptor.depthStencilAttachment`; format-based detection of depth-only vs combined depth+stencil; image transitioned to `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
- [x] `Device::createRenderPipeline()` `pipelineRenderingCreateInfo` — `depthAttachmentFormat` and `stencilAttachmentFormat` now populated from `descriptor.depthStencil->format`

## Missing implementations — Metal/macOS

- [x] Add `src/pi/utils.cpp` to `macos.cmake` — `Device::createBuffer(size, usage, data)` has no implementation in the macOS build
- [x] `Device::createShaderModule()` — loads `.metallib` binary via `dispatch_data`
- [x] `Device::createRenderPipeline()` — full MTL PSO with vertex/fragment functions, color attachments, depth format, vertex descriptor
- [x] `Device::createComputePipeline()` — full MTL compute PSO
- [x] `Device::createBindGroupLayout()` — no-op placeholder (Metal uses implicit binding)
- [x] `Device::createBindGroup()` — no-op placeholder (Metal binds directly on encoder)
- [x] `Device::createPipelineLayout()` — no-op placeholder (Metal uses implicit layout)
- [x] `Device::createSampler()` — full filter/wrap/compare/anisotropy mapping
- [x] `Device::createQuerySet()` — backed by shared `MTL::Buffer` (8 bytes/slot)
- [x] `Device::createCommandEncoder()` — creates `MTL::CommandBuffer` from stored command queue
- [x] `CommandEncoder` — full implementation: `beginRenderPass`, `beginComputePass`, `clearBuffer`, `copyBufferToBuffer`, `resolveQuerySet`, `finish`
- [x] `RenderPassEncoder` — full implementation: `draw`, `drawIndexed`, `drawIndirect`, `drawIndexedIndirect`, `setPipeline`, `setVertexBuffer`, `setIndexBuffer`, `setViewport`, `setScissorRect`, `setStencilReference`, `beginOcclusionQuery`, `endOcclusionQuery`, `end`
- [x] `ComputePassEncoder` — full implementation: `setPipeline`, `dispatchWorkgroups`, `dispatchWorkgroupsIndirect`, `setBindGroup`, `end`
- [x] `CommandBuffer` — constructor/destructor wrapping `MTL::CommandBuffer`
- [x] `TextureView` — implemented via `MTL::Texture::newTextureView`; `createView()` fully functional
- [x] `Texture::upload()` — implemented via `MTL::Texture::replaceRegion`
- [x] `Device::getEngineVersion()` — returns library version string
- [x] `getVersion()` (free function in namespace) — returns library version string

## Missing implementations — DirectX/Windows

- [x] Rewrite `src/directx/device.cpp` to match the current `device.hpp` API (replace all stale method signatures)
- [x] `Device::getAdapters()` — D3D12 DXGI adapter enumeration (GPU preference ordering)
- [x] `Device::createDevice()` — D3D12 device + command queue + descriptor heaps + fence
- [x] `Device::createBuffer()` — D3D12 upload-heap committed resource, persistently mapped
- [x] `Device::createTexture()` — D3D12 default-heap texture with RTV/DSV pre-allocation for render targets
- [x] `Device::createShaderModule()` — stores raw DXIL/DXBC bytecode
- [x] `Device::createRenderPipeline()` — D3D12 graphics PSO with root signature, rasterizer, blend, depth-stencil state
- [x] `Device::createComputePipeline()` — D3D12 compute PSO with root signature
- [x] `Device::createBindGroupLayout()` / `createBindGroup()` — D3D12 descriptor range / descriptor table handles
- [x] `Device::createPipelineLayout()` — D3D12 root signature via universal helper
- [x] `Device::createSampler()` — D3D12 sampler descriptor on shader-visible sampler heap
- [x] `Device::createQuerySet()` — D3D12 query heap + readback buffer
- [x] `Device::createCommandEncoder()` — D3D12 command allocator + command list
- [x] `CommandEncoder` — `beginRenderPass`, `beginComputePass`, `clearBuffer`, `copyBufferToBuffer`, `writeTimestamp`, `resolveQuerySet`, `finish`
- [x] `RenderPassEncoder` — `draw`, `drawIndexed`, `setPipeline`, `setVertexBuffer`, `setIndexBuffer`, `setViewport`, `setScissorRect`, `setStencilReference`, `end`
- [x] `ComputePassEncoder` — `dispatchWorkgroups`, `setPipeline`, `setBindGroup`, `end`
- [x] `CommandBuffer` — wraps D3D12 command list; `Device::submit` executes and waits via fence
- [x] All resource classes (`Buffer`, `Texture`, `TextureView`, `ShaderModule`, `RenderPipeline`, `ComputePipeline`, `BindGroup`, `BindGroupLayout`, `PipelineLayout`, `Sampler`, `QuerySet`) — fully implemented

## Build system

- [x] `ios.cmake` — created; mirrors `macos.cmake` (same Metal backend, CMake handles sysroot/arch)
- [x] `linux.cmake` — full Vulkan backend; builds `src/vulkan_linux/` with complete rendering/compute API (headless first, windowed X11/Wayland deferred)
- [x] Add `src/pi/utils.cpp` to `macos.cmake`
- [x] Add `src/pi/utils.cpp` to `windows.cmake` once DirectX `createBuffer` is wired up

## Raytracing

### Constants & Enums
- [x] Add `BufferUsage::accelerationStructureInput` — geometry/instance data fed into AS builds
- [x] Add `BufferUsage::accelerationStructureStorage` — backing storage for built AS objects
- [x] Add `ShaderStage::rayGeneration`, `::miss`, `::closestHit`, `::anyHit`, `::intersection` values
- [x] Add `AccelerationStructureGeometryType` enum (`triangles`, `boundingBoxes`)
- [x] Add `AccelerationStructureBuildFlag` enum (`preferFastTrace`, `preferFastBuild`, `allowUpdate`, `allowCompaction`)

### Public API Types
- [x] Add `AccelerationStructure` class (`inc/campello_gpu/acceleration_structure.hpp`) — opaque handle for both BLAS and TLAS; expose `getBuildScratchSize()` and `getUpdateScratchSize()`
- [x] Add `RayTracingPipeline` class (`inc/campello_gpu/ray_tracing_pipeline.hpp`) — holds ray gen / miss / hit group shader state
- [x] Add `RayTracingPassEncoder` class (`inc/campello_gpu/ray_tracing_pass_encoder.hpp`) — `setPipeline()`, `setBindGroup()`, `traceRays()`, `end()`

### Descriptors
- [x] Add `AccelerationStructureGeometryDescriptor` — buffer refs, vertex/index format, transform buffer, geometry flags
- [x] Add `BottomLevelAccelerationStructureDescriptor` — list of geometry descriptors + build flags
- [x] Add `TopLevelAccelerationStructureDescriptor` — instance buffer (each instance references a BLAS + transform + mask), build flags
- [x] Add `RayTracingPipelineDescriptor` — ray generation shader, miss shaders, hit groups (closest/any/intersection), `PipelineLayout`, max recursion depth

### Device Factory Methods
- [x] Add `Device::createBottomLevelAccelerationStructure(const BottomLevelAccelerationStructureDescriptor&)`
- [x] Add `Device::createTopLevelAccelerationStructure(const TopLevelAccelerationStructureDescriptor&)`
- [x] Add `Device::createRayTracingPipeline(const RayTracingPipelineDescriptor&)`

### CommandEncoder Extensions
- [x] Add `CommandEncoder::beginRayTracingPass()` — returns `RayTracingPassEncoder`
- [x] Add `CommandEncoder::buildAccelerationStructure(dst, BottomLevelAccelerationStructureDescriptor, scratchBuffer)`
- [x] Add `CommandEncoder::buildAccelerationStructure(dst, TopLevelAccelerationStructureDescriptor, scratchBuffer)`
- [x] Add `CommandEncoder::updateAccelerationStructure(src, dst, scratchBuffer)` — incremental rebuild for dynamic geometry
- [x] Add `CommandEncoder::copyAccelerationStructure(src, dst)` — for AS compaction / clone

### BindGroup Extension
- [x] Add `BindingType::accelerationStructure` — maps to `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`, MTL acceleration structure argument, DXR SRV
- [x] Update `createBindGroup` to accept `AccelerationStructure` entries alongside buffers/textures/samplers

### Vulkan / Android Backend
- [x] Detect `VK_KHR_acceleration_structure` + `VK_KHR_ray_tracing_pipeline` + `VK_KHR_deferred_host_operations` in `src/vulkan_android/adapter.cpp`; set `Feature::raytracing`
- [x] Enable extensions and device features (`bufferDeviceAddress`, `accelerationStructure`, `rayTracingPipeline`) in `src/vulkan_android/device.cpp`
- [x] Load `VK_KHR_acceleration_structure` and `VK_KHR_ray_tracing_pipeline` function pointers (follow the existing `pfnCmdBeginRenderingKHR` pattern)
- [x] Add `acceleration_structure_handle.hpp` — wraps `VkAccelerationStructureKHR` + backing `VkBuffer`
- [x] Implement BLAS build: `vkCmdBuildAccelerationStructuresKHR` with triangle geometry
- [x] Implement TLAS build: `vkCmdBuildAccelerationStructuresKHR` with instance geometry
- [x] Add `ray_tracing_pipeline_handle.hpp` — wraps `VkPipeline` (ray tracing type) + Shader Binding Table buffers
- [x] Implement `createRayTracingPipeline`: compile ray gen/miss/hit group shaders, build SBT into a `VkBuffer`
- [x] Implement `RayTracingPassEncoder`: `vkCmdBindPipeline(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)` + `vkCmdTraceRaysKHR`

### Metal Backend
- [x] Verify `supportsRaytracing()` detection in `src/metal/adapter.cpp` reaches `Device::getFeatures()` correctly
- [x] Add `acceleration_structure_handle.hpp` — wraps `MTL::AccelerationStructure*`
- [x] Implement BLAS build: `MTLPrimitiveAccelerationStructureDescriptor` → `MTLAccelerationStructureCommandEncoder`
- [x] Implement TLAS build: `MTLInstanceAccelerationStructureDescriptor`
- [x] Implement ray tracing pipeline: build `MTLIntersectionFunctionTable` + visible function table, link into `MTLComputePipelineState` with `linkedFunctions`
- [x] Implement `RayTracingPassEncoder` via compute pass + `MTLIntersectionFunctionTable` binds + `dispatchThreads` (Metal ray tracing uses a compute pass, not a dedicated pass type)

### DirectX 12 Backend
- [x] Verify `D3D12_RAYTRACING_TIER_1_0` detection in `src/directx/adapter.cpp` correctly inserts `Feature::raytracing`
- [x] Add `AccelerationStructureHandle` — wraps `ID3D12Resource*` for AS GPU VA (in `common.hpp`)
- [x] Implement BLAS build: `ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure`
- [x] Implement TLAS build: same API with instance descs in an upload heap buffer
- [x] Implement ray tracing pipeline: `ID3D12Device5::CreateStateObject` with `D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE`, export associations for ray gen / miss / hit groups
- [x] Build Shader Table (ray gen record, miss records, hit group records) into `ID3D12Resource`
- [x] Implement `RayTracingPassEncoder`: `ID3D12GraphicsCommandList4::DispatchRays()` with shader table addresses

### Tests & Examples
- [x] Add unit tests in `tests/platform/test_raytracing.cpp` covering BLAS/TLAS creation, build, pipeline creation, and feature-gated skip when `Feature::raytracing` is absent
- [x] Add a minimal raytracing example in `examples/android/` — `RaytracingDemo.h/.cpp` (single-triangle BLAS→TLAS→RT pipeline, SPIR-V shaders, writes to storage texture via traceRays)
- [x] Add a minimal raytracing example in `examples/apple/` — `RaytracingDemo.h/.mm` + `RaytracingShaders.metal` (same flow using Metal raytracing kernel)

## Public API / headers

- [x] **Cubemap texture support** — `TextureType::ttCube` and `TextureType::ttCubeArray` added to public enum
  - WebGPU-style semantics: storage = 2D texture with `depthOrArrayLayers = 6` (or multiple thereof); cube semantics come from view dimension
  - `Texture::createView(..., dimension)` supports `ttCube` and `ttCubeArray` on all backends
  - Metal: `createTexture()` maps cube types to `MTLTextureTypeCube` / `MTLTextureTypeCubeArray`; 2D arrays use `MTLTextureType2DArray`
  - Vulkan: `createTexture()` sets `arrayLayers` from `depth`, adds `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`; `createView()` uses `VK_IMAGE_VIEW_TYPE_CUBE` / `CUBE_ARRAY`
  - DirectX: `createView()` bug fixed (was switching on storage dimension instead of view dimension); cube SRV descriptors added
- [x] `RenderPassEncoder::setBindGroup()` — implemented in Vulkan; present in Metal and DirectX
- [x] `RenderPassEncoder::executeBundles()` — not present in `render_pass_encoder.hpp`; removed from API (render bundles are a WebGPU concept with no Vulkan/Metal equivalent; out of scope)
- [x] `CommandEncoder::copyTextureToBuffer` — implemented (Vulkan + Metal)
- [x] `CommandEncoder::copyTextureToTexture` — implemented (Vulkan + Metal)
- [x] `CommandEncoder::copyBufferToTexture()` — implemented (Vulkan + Metal); public header updated with proper 6-parameter signature
- [x] `State` class (`state.hpp`) — intentional empty placeholder per docstring; reserved for future pipeline/device state tracking; no implementation required now

## Windowed Linux Desktop (X11 / Wayland)

> The Vulkan/Linux backend now supports both headless and windowed operation via `LinuxSurfaceInfo`.

### Surface & Swapchain

- [x] **X11 surface creation** — `Device::createDevice()` interprets `pd` as `LinuxSurfaceInfo*` and creates the surface via `vkCreateXlibSurfaceKHR` loaded through `vkGetInstanceProcAddr`
  - `VK_KHR_xlib_surface` enabled in instance extensions (`device.cpp`)
  - Queue family selection checks `vkGetPhysicalDeviceSurfaceSupportKHR` when a surface is present
- [x] **Wayland surface creation** — same path, uses `vkCreateWaylandSurfaceKHR`
  - `VK_KHR_wayland_surface` enabled in instance extensions
- [x] **Surface-selection priority** — `LinuxSurfaceInfo::api` field (`LinuxWindowApi::x11` or `LinuxWindowApi::wayland`) controls which surface path is taken
- [x] **Window resize handling** — `recreateSwapchain()` now called proactively from `CommandEncoder::beginRenderPass()` when `vkAcquireNextImageKHR` returns `VK_ERROR_OUT_OF_DATE_KHR`. Example tracks `ConfigureNotify` for informational purposes; backend handles recreation automatically.

### Descriptor Sets

- [x] **Acceleration-structure descriptor binding** — `Device::createBindGroup()` now builds `VkWriteDescriptorSetAccelerationStructureKHR` and chains it through `pNext` for `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`

### Presentation Helpers

- [x] **`Device::getSwapchainTextureView()`** — now caches the current swapchain image index in `DeviceData` and returns a `TextureView::fromNative()` wrapper around the active swapchain image view
- [x] **`Device::scheduleNextPresent()`** — documented as no-op; presentation is implicit inside `submit()`

### Build & CI

- [x] **Add Linux desktop build check to CI** — `build-linux-vulkan` job added to `.github/workflows/ci.yml` (compiles `libcampello_gpu.so` + integration tests on Ubuntu)
- [x] **Add Linux integration tests** — CI installs `mesa-vulkan-drivers` and attempts to run integration tests via lavapipe (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`)
- [x] **Linux windowed example** — `examples/linux/` added with raw-X11 clear-screen demo

### Input / Event Loop (out of scope for campello_gpu, but needed by examples)

- [x] Documented raw X11 usage in `examples/linux/README.md`; GLFW3 mentioned as alternative
