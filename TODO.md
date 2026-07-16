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
- [x] **[Metal]** `Device::createFence()` — `MetalFenceData::signaled` defaulted to `true`, so
      `Fence::wait()` on a freshly created fence returned immediately without waiting for the
      submission it was passed to. Fixed to match the Vulkan backend's behavior: Vulkan creates
      fences pre-signaled too (`VK_FENCE_CREATE_SIGNALED_BIT`) but `vkResetFences()` them right
      before each `vkQueueSubmit`, so the create-time default never matters in practice. Metal's
      `Device::submit(cmdBuffer, fence)` was missing that equivalent reset; now resets
      `fenceData->signaled` to `false` before `commit()`, so `wait()` correctly blocks until the
      completion handler signals it — fixes the one-shot create→submit→wait path while staying
      safe for ring-buffer fence reuse.
- [x] **[Metal]** `Buffer::download()` did a raw `memcpy` from `buffer->contents()` with no
      `synchronizeResource:` call first, which is invalid for `MTLResourceStorageModeManaged`
      buffers (any GPU-written compute/storage buffer not created with `mapRead`/`mapWrite`).
      Fixed by encoding a blit-encoder `synchronizeResource:` and waiting on it before the
      `memcpy`, gated on `buf->storageMode() == MTL::StorageModeManaged` (no-op for `Shared`
      buffers/iOS, which are always coherent) — mirrors the existing readback pattern in
      `texture.cpp`'s `download()`.
- [x] **[Linux/Vulkan]** `examples/linux/main.cpp` build failure — removed stale includes for
      non-existent `campello_gpu/constants/load_op.hpp` and `store_op.hpp`.
- [x] **[Linux/Vulkan]** `CommandEncoder::generateMipmaps()` missing return — returned an
      indeterminate value on success; now returns `true`.
- [x] **[Linux/Vulkan]** `Device::createComputePipeline()` null-deref — crashed with null compute
      module or layout; now returns `nullptr` safely and creates an empty layout when needed.
- [x] **[Linux/Vulkan]** `CommandEncoder::beginRenderPass()` with no attachments — segfaulted
      when `colorAttachments` was empty; now supports no-attachment render passes.
- [x] **[Linux/Vulkan]** Draw/dispatch without a bound pipeline — `draw*`, `drawIndexed*`,
      `drawIndirect*`, `dispatchWorkgroups*` now no-op instead of crashing the driver.
- [x] **[Linux/Vulkan]** Offscreen layout-transition crash after empty render passes — barrier is
      skipped when no pipeline/draws were bound to avoid a Mesa Intel driver crash.
- [x] **[Linux/Vulkan]** Dynamic-rendering entry points — now load Vulkan 1.3 core
      `vkCmdBeginRendering`/`vkCmdEndRendering`, with `KHR` fallback.
- [x] **[Linux/Vulkan]** `VK_KHR_surface` unconditionally required — made optional so headless
      contexts work; `VK_KHR_portability_enumeration` is now enabled when available.
- [x] **[Linux/Vulkan]** `renderTarget` usage added `DEPTH_STENCIL_ATTACHMENT` to color formats —
      now only adds depth/stencil usage for depth/stencil formats.
- [x] **[Android/Vulkan]** `build-android` CI job failing on all 4 ABIs (caught via the
      `arm64-v8a` job log, session 2026-07-16): `ld.lld: error: undefined symbol:
      vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR` in `src/vulkan/device.cpp`'s
      `createDevice()`. Root cause: the header declares the prototype (so it compiles
      fine), but Android's `libvulkan.so` only statically exports core Vulkan symbols —
      every other KHR extension function in this file (dynamic rendering, acceleration
      structure, ray tracing pipeline) is already resolved dynamically via
      `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` into a `pfn*` pointer; the
      cooperative-matrix properties query (added in the `Feature::cooperativeMatrix`
      work) was the one call site that skipped that pattern and called the symbol
      directly. Fixed by resolving `pfnGetCoopMatProps` via `vkGetInstanceProcAddr`
      before use. **Verified by reproducing the exact CI failure locally**: configured
      and built with the same NDK (27.0.12077973), `ANDROID_ABI=arm64-v8a`,
      `ANDROID_PLATFORM=android-28` as the CI job — confirmed the undefined-symbol link
      error with the old code, then confirmed a clean link after the fix.

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
- [x] **Bug found and fixed (session 2026-07-16):** `Device::getFeatures()` in
      `src/vulkan/device.cpp` never inserted `Feature::raytracing` — only
      `Adapter::getFeatures()` did, even though `DeviceData::rayTracingEnabled`
      was tracked and available right there. `tests/platform/test_raytracing.cpp`'s
      `requireRaytracing()` helper gates every RT test on `device->getFeatures()`,
      so every Vulkan RT integration test was silently skipping even on hardware
      that genuinely supports ray tracing. Invisible on CI because
      `build-linux-vulkan` runs through Mesa lavapipe (no RT support at all), so
      those tests were already skipping for an unrelated, coincidentally-correct
      reason and never exercised the buggy path. Fixed by inserting
      `Feature::raytracing` when `deviceData->rayTracingEnabled` is true,
      mirroring the pattern already used for `cooperativeMatrixEnabled` in the
      same function. Verified via `-fsyntax-only` against real Vulkan headers
      (no Windows/Linux-with-RT-hardware machine available this session to run
      it) — same verification caveat as the earlier cooperative-matrix Vulkan
      work.
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

## Cooperative Matrix Multiply

> Hardware-accelerated small-tile matrix multiply-accumulate, exposed differently per
> API: Vulkan's `VK_KHR_cooperative_matrix`, Metal's `simdgroup_matrix` /
> `simdgroup_multiply_accumulate()`, HLSL Shader Model 6.8's Wave Matrix. Motivated by
> campello_nn's `GgmlQuantizedMatmul`/`GqaMatMul` kernels, which currently do scalar
> per-thread dot products with a manual threadgroup-memory reduction — real hardware
> matrix acceleration could be a substantial lever there. **Verified this session (not
> assumed): this repo's actual Metal dev/test machine (Intel UHD 630) does NOT support
> `simdgroup_matrix`** — `supportsFamily(MTL::GPUFamilyApple6)` returns false, and a real
> test kernel compiles but fails pipeline creation with "AIR builtin function was called
> but no definition was found". Metal support here is Apple-Silicon-only (M1+, GPU family
> Apple6+); do not assume it works during local Intel Mac development — gate it and test
> the fallback path just as carefully as the accelerated one. Vulkan support is
> GPU/driver-dependent (mostly modern desktop/professional GPUs); DirectX support depends
> on Agility SDK + SM6.8 availability, not yet confirmed for this project's DirectX
> backend at all.

### Constants & Enums
- [x] Add `Feature::cooperativeMatrix` to `inc/campello_gpu/constants/feature.hpp`
- [x] Consider whether supported element/tile shapes need their own descriptor. **Resolved
      (session 2026-07-16): yes, added.** New public types:
      `inc/campello_gpu/constants/cooperative_matrix_component_type.hpp`
      (`CooperativeMatrixComponentType` — float16/32/64, sint/uint 8/16/32/64, mirrors
      `VkComponentTypeKHR`) and `inc/campello_gpu/cooperative_matrix_properties.hpp`
      (`CooperativeMatrixProperties` — mSize/nSize/kSize/aType/bType/cType/resultType/
      saturatingAccumulation; mirrors `VkCooperativeMatrixPropertiesKHR` minus its `scope`
      field, omitted since it's always subgroup-scope for this extension in practice).
      `Device::getCooperativeMatrixProperties()` added to `device.hpp` right after
      `getFeatures()`. Vulkan converts the tuples already cached on `DeviceData` (see
      below); Metal/DirectX/WebGPU all return an empty vector (documented per-backend why:
      Metal has no runtime shape query at all, the other two don't implement cooperative
      matrix). Verified end-to-end on Metal via a real build+link+run on this session's
      Intel Mac (correctly returns an empty vector); verified on Vulkan via `-fsyntax-only`
      against real Vulkan headers (same caveat as below — no real Vulkan driver available
      to actually run it).

### Vulkan Backend (`src/vulkan/`)
- [x] Detect `VK_KHR_cooperative_matrix` (+ its dependency `VK_KHR_shader_float16_int8` /
      Vulkan 1.2 baseline) in `src/vulkan/adapter.cpp`'s `Adapter::getFeatures()`, following
      the existing `Feature::raytracing` detection pattern in that same function. Also
      wired the same detection into `Device::getFeatures()` in `device.cpp` via the cached
      `DeviceData::cooperativeMatrixEnabled` flag (that function previously derived features
      straight from `vkGetPhysicalDeviceFeatures` rather than what was actually enabled at
      device-creation time).
- [x] Query `vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR` for the actual supported
      `(MSize, NSize, KSize, AType, BType, CType, ResultType, saturatingAccumulation)`
      tuples on the selected physical device — needed before compiling/dispatching any
      cooperative-matrix shader, not just a single yes/no check. Cached on
      `DeviceData::cooperativeMatrixProperties` in `src/vulkan/device.cpp`'s
      `createDevice()`. **Now exposed publicly** (session 2026-07-16) via
      `Device::getCooperativeMatrixProperties()` — see the Constants & Enums item above.
- [x] Enable the extension + `VkPhysicalDeviceCooperativeMatrixFeaturesKHR` device feature
      in `src/vulkan/device.cpp`'s device creation (mirror how raytracing's
      `bufferDeviceAddress`/`accelerationStructure`/`rayTracingPipeline` features are
      enabled there). The existing RT `pNext` chain construction was generalized (tail
      pointer built up before assigning to `dynRenderFeatures`/`createInfo`) so cooperative
      matrix, RT, and dynamic rendering features can all coexist in the chain regardless of
      which subset is actually supported.
      **Not build-verified with the real Vulkan SDK/driver** — this dev machine has no
      Vulkan SDK installed, so `src/vulkan/*.cpp` can't be linked/run locally for the
      Linux/Android targets. Verified instead via a `-fsyntax-only` type-check against the
      real `vulkan_core.h` (borrowed from the Android NDK's bundled headers, which do
      define `VK_KHR_cooperative_matrix`) — confirms the structs/enums/function signatures
      used are correct, but the actual `vkCreateDevice`/extension-enable path is unverified
      against a real driver. Recommend confirming on CI (`build-linux-vulkan` job) or a
      Linux/Android box before trusting this beyond compiling.
- [ ] Confirm the shader toolchain path: GLSL's `GL_KHR_cooperative_matrix` extension (for
      hand-written compute shaders) vs. accepting pre-compiled SPIR-V containing
      `OpTypeCooperativeMatrixKHR` directly (campello_gpu currently loads precompiled
      SPIR-V bytes, not GLSL source, for its Vulkan shader modules — verify the SPIR-V
      route works with whatever shader compiler campello_nn's build step uses)

### Metal Backend (`src/metal/`)
- [x] Add `Feature::cooperativeMatrix` detection to `src/metal/adapter.cpp`'s
      `Adapter::getFeatures()` via `dev->supportsFamily(MTL::GPUFamilyApple6)` — same
      pattern as the existing `supportsRaytracing()`/`supports32BitMSAA()`/
      `supportsBCTextureCompression()` checks in that function. Also added the matching
      check to `Device::getFeatures()` in `src/metal/device.cpp` (mirrors how raytracing/
      BC/MSAA are exposed on both `Adapter` and `Device`). Verified on this session's
      Intel UHD 630: reports `NO` for `cooperativeMatrix` while still correctly reporting
      `YES` for raytracing/BC/MSAA — confirms the detection is real family-gating, not a
      blanket flag.
- [ ] **Test on real Apple Silicon hardware before trusting this path at all** — this
      session's Intel Mac cannot validate the accelerated path locally (confirmed: kernel
      compiles, pipeline creation fails), only the "unsupported, fall back" branch;
      shipping this without ever running the accelerated path on real M-series hardware is
      a real correctness risk, not just a performance question
- [ ] Decide where `simdgroup_matrix` usage lives: campello_gpu itself has no MSL-source
      compilation entry point exposed to callers today (`createShaderModule()` only takes
      precompiled `.metallib` bytes or WGSL source, not raw MSL) — cooperative-matrix
      kernels are almost certainly hand-written MSL (like campello_nn's existing
      `.metal` shaders), so this may end up being campello_nn's concern (a new precompiled
      shader variant, selected via this new `Feature`) more than a `campello_gpu` API
      change; confirm the actual split before designing the API surface

### DirectX 12 Backend (`src/directx/`)
- [x] Research current status (session 2026-07-16, via web search — not from memory,
      given how fast this area is moving): **the TODO's original premise was imprecise.**
      SM6.8's headline feature is actually Work Graphs, not Wave Matrix. "Wave Matrix"
      (`WaveMMA`/`wave_matrix` intrinsics) is its own still-labeled-`6_x` preview spec,
      feature-gated via a distinct `D3D12_FEATURE_DATA_WAVE_MMA` cap — not cleanly tied to
      SM6.8. The actively "current" retail feature is **Cooperative Vectors** (SM6.9, went
      retail in **Agility SDK 1.619**, Feb 2026) — but per Microsoft's own DirectX
      Developer Blog, Cooperative Vectors is already being superseded by a unified
      "LinAlg Matrix" spec targeted at SM6.10, still under review/unreleased as of this
      session (2026-07-16); Microsoft states Cooperative Vectors "won't officially be
      supported in a future Agility SDK." So this is a three-way moving target (WaveMMA
      preview → Cooperative Vectors retail-but-dying → unreleased LinAlg), separate from
      the fact that this repo's `windows.cmake`/`src/directx/common.hpp` link only the
      legacy `d3dcompiler.h` (FXC, HLSL ≤5.1, no Agility SDK NuGet reference, no
      DXC/dxil.dll linkage) — there is no shader-toolchain path to emit SM6.8/6.9 bytecode
      here at all yet, independent of the feature-detection question.
      **Decision: do not implement speculative detection code now.** No Windows machine
      or D3D12/Agility SDK headers were available this session to verify anything against
      (unlike Vulkan, where real NDK headers let the change be type-checked) — writing
      `D3D12_FEATURE_DATA_WAVE_MMA`-shaped code un-verifiable against an actively-churning,
      soon-to-be-superseded spec was assessed as not worth the risk. Revisit once the
      LinAlg Matrix spec ships and stabilizes, or once real Windows/Agility SDK hardware
      is available to verify against.
- [ ] If pursued: add `Feature::cooperativeMatrix` detection to `src/directx/adapter.cpp`'s
      `Adapter::getFeatures()` — target whichever spec (LinAlg Matrix, or Cooperative
      Vectors if LinAlg still isn't out) is current at the time this is picked back up,
      not the WaveMMA name originally assumed here

### WebGPU Backend (`src/webgpu/`)
- [x] Verify current WGSL/WebGPU spec status for any cooperative-matrix-equivalent
      extension before assuming this is out of scope. Checked this session (2026-07-16)
      via web search, not memory: WGSL's "subgroup matrix" proposal (the
      cooperative-matrix equivalent, building on the now-standardized plain `subgroups`
      feature) is confirmed **still in design discussion** as of the gpuweb working
      group's March 2026 meeting minutes — the group was still debating API ergonomics
      (row/column-major layout parameters) with no shipped feature name or enable
      directive yet. Cross-checked against Chrome 144's own release notes, which ship
      unrelated subgroup features (`subgroup_id`) but make no mention of subgroup/
      cooperative matrix — confirms it isn't even behind an experimental Chromium flag.
      Documented this explicitly as a comment in `Adapter::getFeatures()`
      (`src/webgpu/adapter.cpp`) rather than silently omitting `Feature::cooperativeMatrix`,
      per this item's own instruction. Revisit once the proposal reaches at least an
      experimental flag.

### Tests & Examples
- [ ] Add unit tests (mirroring `tests/platform/test_raytracing.cpp`'s precedent) covering
      `Feature::cooperativeMatrix` detection and graceful skip when absent
- [ ] Add a minimal compute example exercising a real cooperative-matrix multiply-add on
      each backend where supported, verified against a scalar CPU reference — needed
      before trusting this for anything real, given how easily "compiles but doesn't
      actually run" surfaced during this session's Metal investigation

## fp16 / Subgroup Operations Features

> Two new boolean `Feature` flags, researched (session 2026-07-16, via web search —
> not from memory) as a follow-up to the cooperative-matrix work above, which
> raised the question of what else could be exposed the same way. `int8` and
> `int4` were considered and dropped: `int8` has no clean per-backend capability
> query verified this session (Metal in particular has no advertised "int8
> compute" flag), and `int4` isn't a real standalone shader capability on any
> backend today — it only shows up as a cooperative-matrix component type, so it
> belongs in `CooperativeMatrixComponentType` if a backend ever supports it, not
> as a top-level `Feature`.

### Constants & Enums
- [x] Add `Feature::fp16` and `Feature::subgroupOperations` to
      `inc/campello_gpu/constants/feature.hpp`, right after `cooperativeMatrix`.

### Vulkan Backend (`src/vulkan/`)
- [x] `fp16`: query `VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16` via
      `vkGetPhysicalDeviceFeatures2`, gated on the same extension-or-1.2-core
      check already computed for cooperative matrix (`hasFloat16Int8 ||
      isVulkan12Plus`) — presence of the extension only means the struct is
      meaningful to query, not that the bit is set. Enabled at device creation
      by generalizing the existing pNext chain further (fp16 → coopMat → RT →
      dynamic rendering), independent of whether coopMat itself is supported.
      Cached on `DeviceData::fp16Enabled` (new field) since it needs an actual
      enable step, mirroring `cooperativeMatrixEnabled`. Wired into both
      `Adapter::getFeatures()` and `Device::getFeatures()`.
- [x] `subgroupOperations`: query `VkPhysicalDeviceSubgroupProperties` via
      `vkGetPhysicalDeviceProperties2` (core since Vulkan 1.1, no device-creation
      enable step needed, unlike fp16) — require `VK_SHADER_STAGE_COMPUTE_BIT` in
      `supportedStages` plus `BASIC | BALLOT | ARITHMETIC` in
      `supportedOperations`, the minimum set that makes the flag actually useful
      for a compute kernel rather than a bare "subgroups exist" signal. Queried
      fresh in both `getFeatures()` calls (not cached — nothing to cache, same
      as the existing `bcTextureCompression`/`etc2`/`astc`/`geometryShader`
      checks).
- [x] **Verified via `-fsyntax-only` against real Vulkan headers** (Android NDK
      27.0.12077973's bundled `vulkan_core.h`, targeting
      `aarch64-linux-android24`) — both `src/vulkan/adapter.cpp` and
      `src/vulkan/device.cpp` compile clean with these changes. Same caveat as
      the cooperative-matrix work: no real Vulkan driver available on this dev
      machine to verify `vkCreateDevice`/the actual runtime values beyond
      type-correctness.

### Metal Backend (`src/metal/`)
- [x] `fp16`: inserted unconditionally — `half` is a native MSL scalar type on
      every Metal-capable device since MSL 1.0, so there's no `MTLDevice`
      capability query for it (nothing to gate).
- [x] `subgroupOperations`: gated on `supportsFamily(MTL::GPUFamilyApple6)` —
      same bar as `Feature::cooperativeMatrix`, and for a defensible reason:
      web research this session (Apple Developer Forums thread on SIMD-group
      shuffle availability, cross-checked against a WWDC "Metal Enhancements"
      writeup) indicates *full* SIMD-group functions (`simd_shuffle`,
      `simd_ballot`, arithmetic reductions) — as opposed to the more limited
      quad-group functions available since Apple4/A11 — require Apple6+. Did
      **not** additionally gate on `GPUFamilyMac2` (Intel/AMD Macs): general
      knowledge suggests Metal-2-era Mac GPUs support SIMD-group functions too,
      but that specific claim wasn't independently confirmed this session, and
      given this repo's own real Metal test hardware is an Intel Mac, a wrong
      positive there would be a live bug, not a theoretical one. Revisit with a
      confirmed source if Intel/AMD Mac subgroup support turns out to matter.
- [x] **Verified end-to-end via a real build+link+run on this session's Intel
      UHD 630** (same machine the cooperative-matrix work used): `fp16` reports
      `YES`, `subgroupOperations` reports `no` — matching `cooperativeMatrix`'s
      `no` on the same hardware and confirming the family gate is real, not a
      blanket flag, while `raytracing`/`bcTextureCompression` still correctly
      report `YES` alongside it.

### DirectX 12 Backend (`src/directx/`)
- [x] `fp16`: `D3D12_FEATURE_DATA_D3D12_OPTIONS4::Native16BitShaderOpsSupported`
      via `CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, ...)`.
- [x] `subgroupOperations`: `D3D12_FEATURE_DATA_D3D12_OPTIONS1::WaveOps` via
      `CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, ...)`. No
      device-creation enable step exists for either in D3D12 — `CheckFeatureSupport`
      is query-only. Field names confirmed via Microsoft Learn docs (web search,
      not memory) rather than assumed.
      **Not build-verified** — no Windows machine or D3D12 headers available
      this session (same limitation noted throughout the DirectX sections
      above); struct/field names are correct per current MS documentation but
      the actual `CheckFeatureSupport` call path is unverified against a real
      driver.

### WebGPU Backend (`src/webgpu/`)
- [x] `fp16` → `WGPUFeatureName_ShaderF16`, `subgroupOperations` →
      `WGPUFeatureName_Subgroups`, both via the existing `wgpuDeviceHasFeature`
      pattern already used for BC/ETC2/ASTC in `Device::getFeatures()`.
      **Correction to an assumption from the cooperative-matrix session's own
      TODO notes above**: those notes mention "the now-standardized plain
      `subgroups` feature" in passing while confirming *subgroup-matrix* is
      still unshipped — this session confirmed that detail directly: `subgroups`
      shipped in Chrome 134 (Feb 2025) and is requestable as a required device
      feature as of Chrome 145 (Jan 2026). `shader-f16` has shipped even
      longer. Unlike cooperative matrix, both are real, queryable, shippable
      WebGPU features today.
      Left `Adapter::getFeatures()` untouched — it's a pre-existing stub (its
      `native` is always `nullptr`, never populated by `getAdapters()`, so it
      already hardcodes a fixed feature set rather than querying anything, for
      every feature, not just these two). Note also that WebGPU device
      creation in this backend goes through `emscripten_webgpu_get_device()`,
      i.e. the JS-side `requestDevice()` call the host page makes — this C++
      code has no control over which features actually got negotiated, exactly
      like the existing BC/ETC2/ASTC checks; `wgpuDeviceHasFeature()` will
      correctly report `false` for either flag if the host page's JS didn't
      request them.
      **Not build-verified** — no Emscripten toolchain or `webgpu.h` available
      on this dev machine.

### Tests & Examples
- [ ] Add unit tests covering `Feature::fp16`/`Feature::subgroupOperations`
      detection and graceful skip when absent, mirroring
      `tests/platform/test_raytracing.cpp`'s precedent (same gap already open
      for `Feature::cooperativeMatrix` above).

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
