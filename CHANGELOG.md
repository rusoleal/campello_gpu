# Changelog

All notable changes to campello_gpu are documented here.

## [Unreleased]

## [0.23.2] - 2026-08-24

### Added

- **`CAMPELLO_GDK_GAMING_DESKTOP` CMake option and `build-windows-gdk` CI job** — compiles the DirectX 12 backend under `WINAPI_FAMILY=WINAPI_FAMILY_GAMES` using the stock Windows SDK, as a partition-conformance check. Note: despite the option's name, this does **not** correspond to Microsoft GDK's Gaming.Desktop.x64 target — verified this session against the real, free, public Microsoft GDK (installed via `winget install Microsoft.Gaming.GDK`) that Gaming.Desktop.x64 actually uses `WINAPI_FAMILY_DESKTOP_APP` and needs no special handling at all; `campello_gpu` already builds, links, and passes its full integration-test suite unmodified under the real `Gaming.Desktop.x64` MSBuild platform. `WINAPI_FAMILY_GAMES` is the real Xbox **console** partition (part of the non-public GXDK, which this project has no access to), so this option/CI job is a best-effort stand-in check for that partition using only the stock SDK's copy of the same macro — see `TODO.md`'s "Windows / Xbox (Microsoft GDK) partition support" section for the full writeup.

### Fixed

- **[Windows/DirectX] `TextureView::fromNative(nullptr)` returned `nullptr` instead of a wrapper object**, unlike every other backend (Vulkan/Metal/WebGPU all construct the wrapper unconditionally, treating a null native handle as a degenerate-but-legal input). Fixed by removing the early return.
- **[Windows/DirectX] `Device::createPipelineLayout()` failed (`nullptr`) for any `PipelineLayoutDescriptor` with two or more bind group layouts.** `Device::createBindGroupLayout()` always stamps its descriptor ranges' `RegisterSpace` at `0`, so two layouts each starting their own bindings at register 0 (the normal case) collided on the same `(register, space)` slot once composed into one root signature — `D3D12SerializeVersionedRootSignature` rejects that as an overlapping binding. Fixed by stamping each layout's ranges with `RegisterSpace` set to its position within `PipelineLayoutDescriptor::bindGroupLayouts` when building the root signature, matching the `spaceN` HLSL shaders compiled against this API are expected to declare per group.
- **[Windows/DirectX] `Texture::createView()` ignored `baseMipLevel`/`baseArrayLayer` for render-target views**, always reusing the texture's single pre-created mip-0/layer-0 RTV regardless of what subresource the view actually requested — a view onto mip level 1 of a render target rendered into mip level 0 instead. Fixed by allocating a proper per-subresource RTV (from the same `rtvExtraHeap` used by mipmap generation) whenever the requested view isn't the default mip-0/layer-0 one, with lifetime cleanup on `~TextureView()` via a new `TextureViewHandle::rtvExtraIndex`.
- **[Windows/DirectX] A Windows SDK bug left `HMONITOR` undeclared when compiling under `WINAPI_FAMILY_GAMES`** (`windef.h` only declares it under `WINAPI_PARTITION_APP`/`WINAPI_PARTITION_SYSTEM`, and `dxgi.h`'s own fallback only fires for `WINVER < 0x0500` — neither holds under `WINAPI_FAMILY_GAMES`), cascading into nonsensical MSVC parse errors at `DXGI_OUTPUT_DESC`'s `HMONITOR Monitor;` field. Fixed with a manual `typedef HANDLE HMONITOR` guarded to that partition, per a confirmed Microsoft Q&A report of the same issue.
- **[macOS/Metal] `pendingPresentDrawable` was read/released/retained/stored from both the raster thread and the UI thread** (`drawInMTKView:`'s skipped-frame path) with no synchronization, letting the two threads double-release the same drawable and crash later inside Metal's own `presentDrawable:` completion machinery. Guarded with a mutex. Added a TSan build profile and a concurrency regression test (`test_device_present_race.cpp`) that reproduces the race against real drawables from an off-screen `CAMetalLayer`.
- **[macOS/Metal] `RenderPassEncoder::setScissorRect()` truncated x/y/width/height independently**, which only ever shrinks the right/bottom edge. Now rounds outward (floor/ceil) so thin content isn't asymmetrically clipped.

All four DirectX fixes above were verified by building and running `campello_gpu_integration_tests.exe` against real hardware (Intel Iris Graphics 550) both before and after: the pre-fix build reproduces all three test failures (confirmed pre-existing, not introduced by the GDK-partition work), and the post-fix build passes all 205 non-skipped integration tests, including under the real Gaming.Desktop.x64 GDK platform.

## [0.23.1] - 2026-08-17

### Fixed

- **[Windows/DirectX] `Device::createTexture()` ignored the caller-supplied contract that `depth` is irrelevant for `TextureType::ttCube`** (the Vulkan backend hardcodes 6 array layers for cube textures regardless of `depth`, and every campello_renderer call site relies on that) — DirectX instead took `depth` literally, so a cube texture created with `depth=1` ended up with `DepthOrArraySize=1`. Per-face uploads then computed out-of-range subresource indices (`CalcSubresource` against a resource with far fewer subresources than the code assumed), which escalated to `DXGI_ERROR_DEVICE_HUNG` via the D3D12 debug layer's `GetCopyableFootprints`/`CopyTextureRegion` "Subresource is too large" errors. Also never special-cased `ttCube`/`ttCubeArray` in the SRV-creation switch, always falling into the generic Texture2D/Texture2DArray path and producing an SRV of the wrong `D3D12_SRV_DIMENSION`. Now hardcodes 6 array layers for `ttCube` (matching Vulkan) and emits `D3D12_SRV_DIMENSION_TEXTURECUBE`/`TEXTURECUBEARRAY` SRVs.

- **[Windows/DirectX] `Device::createBindGroup()` couldn't guarantee contiguous descriptor-heap slots for a BindGroup with 2+ non-sampler entries.** It called `allocSrvIndex()` once per entry, whose LIFO free-list reuse can hand back non-adjacent indices — but a root-signature descriptor table's ranges must be physically contiguous in heap-write order, since `SetGraphicsRootDescriptorTable` is bound with a single base GPU handle and every subsequent range is read as adjacent to it. This misaligned every table entry after the first, surfacing as D3D12 debug-layer errors like "descriptor of type CBV... mismatching types" (expected SRV) or "resource dimensions (UNKNOWN)... expected TEXTURECUBE" and "has not been initialized" — traced to the IBL-bake bind group (CBV then SRV-CUBE), reproducibly escalating to `DXGI_ERROR_DEVICE_HUNG`. Fixed by reserving one contiguous run up front via a new `allocSrvIndexRange(count)` (scans `srvFreeSlots`, now a `std::set<UINT>` instead of a `std::vector`, for a contiguous run of free indices before falling back to bump-allocating) instead of N independent `allocSrvIndex()` calls.

- **[Windows/DirectX] `Device::createBindGroup()` had the identical contiguity bug for sampler entries, plus samplers had no non-shader-visible staging heap.** For a BindGroup with 2+ distinct `Sampler` entries (e.g. the combined PBR material bind group, 8 sampler entries), `createBindGroup()` only ever kept the *last* sampler's descriptor as the whole sampler table's base GPU handle — every other range in the table read uninitialized descriptors ("Descriptor Range... of type D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER has not been initialized"). Root cause was two-fold: `createSampler()` wrote each Sampler's canonical descriptor directly into the shader-visible `samplerHeap` (unsafe as a `CopyDescriptorsSimple` source on this hardware — the same CPU write-only/write-combined restriction already documented for `srvStagingHeap` applies equally to sampler heaps), and there was no per-BindGroup contiguous-range allocator for samplers the way `allocSrvIndexRange()` provides for SRVs/CBVs. Added a non-shader-visible `samplerStagingHeap` (samplers now write their canonical descriptor there) and `allocSamplerIndexRange(count)`/`samplerFreeSlots`/`recycleSamplerSlots()`, mirroring the SRV scheme exactly; `createBindGroup()` now copies each sampler entry into one reserved contiguous run of the shader-visible `samplerHeap` instead of reusing Sampler objects' own slots directly.

- **[Windows/DirectX] `CommandEncoder::beginRenderPass()` silently dropped any color attachment with a null `view`**, instead of treating it as "render to the device's own swapchain" — the convention `campello_renderer`'s `Renderer::render()` (no-arg overload, used on Windows/Android) relies on, and which the Vulkan backend already honors (see its `isSwapchain`/`hasExplicitView` handling in `beginRenderPass()`). Combined with `createCommandEncoder()`/`CommandEncoder::finish()`/`Device::submit()` *unconditionally* transitioning and presenting the current swapchain backbuffer on every single command encoder — even ones with no swapchain-targeting render pass at all — the PRESENT↔RENDER_TARGET barriers stayed internally self-consistent (so the D3D12 debug layer never flagged anything), while the actual presented backbuffer was never bound as a render target, cleared, or drawn into: a permanently black window with zero validation errors. Fixed by binding and clearing the current backbuffer's RTV directly inside `beginRenderPass()` when a color attachment has no view (mirrors the CPU-descriptor computation `Device::getSwapchainTextureView()` already does). The unconditional transition/present-on-every-encoder behavior itself was left as-is — harmless now that the backbuffer is actually rendered into, and out of scope for this fix.

- **[Windows/DirectX] The swapchain was never actually resized on a window resize**, crashing the app with `STATUS_ACCESS_VIOLATION`/a debug-layer fail-fast exit. The only code that resized the DirectX swapchain buffers lived inside `Device::getSwapchainTextureView()` — which the null-view `beginRenderPass()` fix above made dead code, since nothing calls it on Windows anymore. Once `beginRenderPass()` started actually binding and drawing into the backbuffer, a real window resize left the depth buffer (recreated fresh by `campello_renderer::Renderer::resize()`) and the color backbuffer (still the OLD size) as mismatched render-pass attachments. Fixed by moving the resize-detection logic into a new `DeviceData::ensureSwapchainSize()`, called at the very top of `createCommandEncoder()` — *before* it records that frame's PRESENT→RENDER_TARGET barrier against `renderTargets[frameIndex]`, since resizing later (e.g. inside `beginRenderPass()`) would leave that barrier referencing an already-released, stale resource pointer.

- **[Windows/DirectX] The DSV (depth-stencil view) descriptor heap — a fixed 32 slots — was a pure bump allocator with zero reclamation**, permanently leaking one slot per depth `Texture` for the lifetime of the `Device`. `campello_renderer::Renderer::resize()` creates a brand-new depth texture (and thus a new DSV) on every resize event; combined with the Windows example calling `renderer->resize()` synchronously on every `WM_SIZE` (Win32 dispatches dozens of these per second during a live drag-resize, all from inside the OS's own nested modal loop, with no frame actually submitted in between to let reclamation catch up), a single continuous drag-resize exhausted the heap almost instantly. Symptom was unusually hard to diagnose: the process vanished instantly with no WER "Application Error" report and no System-log TDR event — the D3D12 debug layer, on hitting `CreateDepthStencilView` with an out-of-bounds handle, fail-fast-terminates via `RaiseException(STATUS_FATAL_APP_EXIT)`, a deliberate hard exit that bypasses normal AppCrash reporting. Fixed with proper slot reclamation (`allocDsvIndex()`/`freeDsvSlots()`/`recycleDsvSlots()`, mirroring the existing `rtvExtraIndex` pattern exactly, `TextureHandle::dsvIndex` freed in `~Texture()`) and a bump from 32 to `DeviceData::kDsvHeapCapacity` (256) slots.

- **[Windows/DirectX] The shader-visible sampler descriptor heap was only 128 slots with no exhaustion guard**, silently corrupting memory once exceeded rather than failing cleanly. `campello_renderer` rebuilds every combined per-material `BindGroup` fresh on each `render()` call (no cross-frame caching — see its own doc comment on why), each with up to 8 sampler entries; a model with more than ~16 materials already needed more than 128 slots live at once. Confirmed via a real "load a second, more complex model" reproduction under `cdb.exe`: `ID3D12Device::CopyDescriptorsSimple: ... does not refer to a location in a descriptor heap. DestDescriptorRangeStart is the issue`. Fixed by raising the heap to `DeviceData::kSamplerHeapCapacity` (2048 — D3D12's hard ceiling for a shader-visible sampler heap, `D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE`) and adding a bounds check to `allocSamplerIndexRange()` (returns `UINT(-1)` on exhaustion, mirroring `allocRtvExtraIndex()`'s existing pattern) that `createBindGroup()` now checks and fails cleanly on instead of writing descriptors past the heap.

- **[Windows/DirectX] `CommandEncoder::generateMipmaps()` permanently leaked one SRV, one RTV-extra, and one sampler descriptor-heap slot per mip level, forever, for every texture ever mipmapped** — `allocSrvIndex()`/`allocRtvExtra()`/`allocSamplerIndexRange()` were all called with no matching free. `allocRtvExtra()`'s own doc comment even said as much: "where the caller doesn't wrap the allocation ... to free it ... later". Harmless for a handful of small textures, but `campello_renderer` calls this every frame on its `opaqueSceneTexture` (used for `KHR_materials_transmission` screen-space refraction, sized to the full render target with a full mip chain — up to 11 levels at 1280×720) — this alone leaked 3 descriptor-heap slots per mip level *per frame*, exhausting the (then 1024-slot) RTV-extra heap within seconds of continuous rendering. Fixed by switching from the transient `allocRtvExtra()` convenience wrapper to `allocRtvExtraIndex()`/`rtvExtraCpuAt()` directly (so the index is available to free) and calling `freeSrvSlots()`/`freeRtvExtraSlots()`/`freeSamplerSlots()` at the end of each iteration/call — safe even though the GPU hasn't executed the command list yet, since these queue into the current generation's pending-free list and only become reusable once `beginFrameRing()` confirms that generation's fence has signaled, by which point the mip-blit draw has long finished reading them (the same pattern `BindGroup::~BindGroup()` already relies on).

- **[Windows/DirectX] `CommandEncoder::generateMipmaps()`'s internal downsample pixel shader computed its sample UV from `Texture2D::GetDimensions()` on an SRV restricted to a single source mip (`MostDetailedMip`/`MipLevels=1`)**, relying on that call correctly returning the *view-relative* size rather than the underlying resource's absolute mip-0 size — an API-semantics detail this shader (uniquely in this codebase, target `ps_5_0` via the legacy FXC `D3DCompile`, not the `ps_6_0`/DXC path everything else uses) never had verified. If `GetDimensions()` returned the full resource's mip-0 size regardless of `MostDetailedMip`, only the *first* downsample step in a chain would happen to compute the correct UV scale (by coincidence, since destination size equals source-size×0.5 there); every subsequent step would then sample a shrinking corner of the source instead of its full extent, producing degenerate (not properly blurred) mip content beyond mip 1. Fixed by passing the known-correct destination pixel size directly via a root-constant pair (`D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`, register `b0`) instead of relying on `GetDimensions()`.

## [0.23.0] - 2026-08-13

### Added

- **`Texture::createView()` gains an explicit `mipLevelCount` parameter** (all backends: Vulkan, Metal, DirectX, WebGPU). Defaults to `-1` ("all remaining levels from `baseMipLevel`", the prior behavior) — the right choice for a view that will be *sampled*. A render-pass color/depth attachment view requires exactly one mip level; passing `1` here is what makes that shape expressible instead of always defaulting to the full remaining chain.
- **`systems::leal::campello_gpu::validationErrorCount()` / `resetValidationErrorCount()`** (`campello_gpu/validation_diagnostics.hpp`) — lets a test assert "zero Vulkan validation errors fired" programmatically instead of a human reading stderr. Real counter in `CAMPELLO_GPU_VALIDATION=ON` builds (backed by the existing `VkDebugUtilsMessengerEXT` callback); hardcoded to `0` otherwise, since no messenger is installed to observe anything.

### Fixed

- **[Linux/Vulkan] `CommandEncoder::beginRenderPass()`'s dynamic-rendering offscreen path always transitioned the target image's layout with `oldLayout = VK_IMAGE_LAYOUT_UNDEFINED`**, even when the pass's `loadOp` was `load` (i.e. the caller explicitly wants this pass's writes preserved on top of existing content — `IDrawBackend::beginOffscreenPass(preserve_content=true)`, used by campello_widgets' `RenderDrawSurface` for incremental multi-frame canvas accumulation). `UNDEFINED` as `oldLayout` is a hint that the driver is free to discard the image's prior contents during the transition — correct for a genuinely first-use or intentionally-cleared pass, but wrong once the image has already gone through a previous offscreen pass (`RenderPassEncoder::end()` always leaves it in `SHADER_READ_ONLY_OPTIMAL` afterward). On real hardware this visibly discarded fragments of previously-written content: a freehand drawing surface accumulating stroke segments across many frames rendered as a broken/dotted line instead of solid, and content wiped by a prior `clear()` pass could resurface on a later write. The traditional (non-dynamic-rendering) render-pass path already handled this correctly (`buildRenderPass()`'s `initialLayout` already switches on `loadOp`); this brings the dynamic-rendering manual-barrier path up to the same correctness, using `oldLayout = SHADER_READ_ONLY_OPTIMAL` / `srcAccessMask = VK_ACCESS_SHADER_READ_BIT` whenever `loadOp == load`. Verified fixed on Intel Iris Graphics 550 (Mesa, dynamic rendering) — the only current caller of `preserve_content=true` is campello_widgets' draw-surface incremental update, so the fix is narrowly and safely scoped.

- **[Linux/Vulkan] `Texture`/`TextureView` destructors could destroy a still-in-use GPU resource.** `Device::submit()` is pipelined (`kFramesInFlight`-deep, doesn't block), so a `Texture`/`TextureView` could be dropped (e.g. `ImageCache` replacing a cached entry) microseconds after a frame that referenced it was submitted but before the GPU actually finished — `vkDestroyImage`/`vkDestroyImageView`/`vkFreeMemory` ran immediately and unconditionally, tripping `VUID-vkDestroyImage-image-01000`/`VUID-vkDestroyImageView-imageView-01026` under validation layers (real UB without them). Fixed by queuing the raw handles into a new `DeviceData::pendingTextureDestroys` ring, drained once `beginFrameRing()`/`Device::~Device()` prove the GPU is done with that generation — mirroring `genCommandBuffer[]`'s existing lifetime pattern. Found and fixed alongside a second, related bug: `TextureView::~TextureView()` reached through `ownerTexture->deviceData` to queue its own deferred destroy, but a `TextureView`'s lifetime is independent of its owning `Texture`'s — dropping the `Texture` first left `ownerTexture` dangling, crashing with `SIGSEGV`. `TextureViewHandle` now carries its own `deviceData`, copied at creation time instead of derived through `ownerTexture` at destroy time. Verified under AddressSanitizer: the dangling-pointer crash reproduces deterministically as `heap-use-after-free` on the pre-fix code and is clean after.

- **[Linux/Vulkan] `CommandEncoder::generateMipmaps()` only ever blitted array layer 0** — a single hardcoded `VkImageBlit` region regardless of how many layers the texture had, so mipmapping a cubemap or array texture left every face/layer past the first with uninitialized memory in its higher mips. Now issues one region per array layer. Also fixed the per-mip layout bookkeeping: it read a stale whole-texture `currentLayout` scalar on iterations after the first (that scalar is only updated once, after the whole loop finishes) and restored the final mip to `GENERAL` instead of `SHADER_READ_ONLY_OPTIMAL`, leaving it not actually sampling-ready.

- **[Linux/Vulkan] `CommandEncoder::copyBufferToTexture()` could claim the wrong `oldLayout`** for a multi-call upload targeting different subresources of the same texture (e.g. one call per cubemap face) — it read the shared whole-texture `currentLayout` scalar, which a *previous* call's barrier may have already advanced, even though the subresource this call targets was never actually touched and is genuinely still `VK_IMAGE_LAYOUT_UNDEFINED`. Tripped `VUID-vkCmdDraw-None-09600` the first time something sampled an "untransitioned" face. Now unconditionally uses `VK_IMAGE_LAYOUT_UNDEFINED`, which is always valid here since the call fully overwrites the subresource's contents regardless of what came before.

- **[Linux/Vulkan] Offscreen render-pass barriers always targeted subresource (mip 0, layer 0)**, even when the attachment view targeted a different mip/layer (e.g. one face of a cubemap render target) — `CommandEncoder::beginRenderPass()`'s entry barrier and `RenderPassEncoder::end()`'s exit barrier both hardcoded `{ 0, 1, 0, 1 }`. Every other subresource of the owning image was silently left untransitioned. Now threads the view's actual `baseArrayLayer`/`baseMipLevel` (new fields on `TextureViewHandle`, set by `Texture::createView()`) through to both barriers via new `RenderPassEncoderHandle::offscreenBaseArrayLayer`/`offscreenBaseMipLevel` fields.

- **[Linux/Vulkan] `Buffer::upload()` could violate `VUID-VkMappedMemoryRange-size-01390`** — both `vkMapMemory()`'s offset and `vkFlushMappedMemoryRanges()`'s range must be `nonCoherentAtomSize`-aligned in general, which a raw `[offset, offset+length)` write (e.g. a 372-byte uniform buffer update) doesn't generally satisfy. Now rounds the mapped/flushed range outward to the atom boundary (a new `DeviceData::nonCoherentAtomSize`, cached from `VkPhysicalDeviceLimits` at device creation), clamped to the allocation so it never runs past the end of memory; the actual `memcpy` still only ever touches the caller's real range.

- **[Linux/Vulkan] `Device::createSampler()` unconditionally enabled anisotropic filtering**, violating `VUID-VkSamplerCreateInfo-anisotropyEnable-01071` (requires `maxAnisotropy` in `[1.0, maxSamplerAnisotropy]` whenever enabled) for any caller that left `SamplerDescriptor::maxAnisotropy` at its zero-value default (no default member initializer — "set to 1.0 to disable" per its own doc comment). Now only enables anisotropy when `maxAnisotropy > 1.0`.

- **[Linux/Vulkan] `Device::createRenderPipeline()` always used the device's swapchain surface format for a pipeline's color target**, even for offscreen pipelines or headless devices with no window at all (`surfaceFormat.format == VK_FORMAT_UNDEFINED`) — crashed Mesa's Intel ANV driver inside `vkCreateGraphicsPipelines`. Now prefers the pipeline descriptor's own color target format.

- **[Linux/Vulkan] `Device::createBindGroupLayout()`/`createBindGroup()` didn't track each binding's declared descriptor type**, so a caller reusing one binding number for two different resource kinds across pipeline variants (valid on Metal's separate argument-index spaces, invalid on Vulkan's single per-binding-type descriptor set model) corrupted or validation-rejected the bind group. Now skips entries that don't match the binding's declared type.

- **[Linux/Vulkan] `Device::submit(commandBuffer, fence)` didn't retain the submitted `CommandBuffer`** — callers commonly pass a temporary straight into `submit()`; without retaining it, its destructor ran synchronously and called `vkFreeCommandBuffers`/`vkDestroyQueryPool` on a buffer the GPU was still executing asynchronously. Also now chains a semaphore signal into the device's own internal frame-ring fence when a caller supplies its own external fence for a swapchain submission, fixing a real VUID violation where the internal fence stayed permanently signaled and the next acquire reused a semaphore whose previous wait was never consumed.

- **[Linux/Vulkan] The persistent descriptor pool (long-lived `persistent=true` bind groups) only reserved `SAMPLED_IMAGE`/`SAMPLER` capacity, never `UNIFORM_BUFFER`** — any buffer entry written into a persistent bind group at a texture-declared binding was silently dropped by `createBindGroup()`'s type filter, so nothing had exercised the gap until a real uniform buffer write hit it (`vkAllocateDescriptorSets()` failure). Now reserves `UNIFORM_BUFFER` capacity too.

- **[Linux/Vulkan] A texture's default `VkImageView` (created alongside every `Texture`) picked `VK_IMAGE_VIEW_TYPE_2D` for `TextureType::ttCube`** instead of `VK_IMAGE_VIEW_TYPE_CUBE` — harmless as long as cube textures were only ever sampled through an explicitly-created `TextureView`, but broke the first caller binding a cube `Texture` directly (`VUID-vkCmdDrawIndexed-viewType-07752`).

- **[macOS/DirectX/WebGPU] `createBindGroup()` still used the old single-parameter signature** after the persistent-pool bool was added to `Device::createBindGroup()`'s declaration, breaking the iOS/macOS and wasm CI builds.

- **[macOS/DirectX/WebGPU] `validationErrorCount()`/`resetValidationErrorCount()` were undefined symbols** outside Vulkan builds — only `src/vulkan/device.cpp` defined them, so any test target linking against the Metal, DirectX, or WebGPU backends (e.g. the macOS integration tests) failed to link. Added a shared always-zero fallback, wired into `ios.cmake`, `macos.cmake`, `windows.cmake`, and `wasm.cmake`.

- **[macOS] `Texture::createView()` left Metal's array-layer "all remaining" sentinel unresolved**, passing `UINT32_MAX` straight into `NS::Range` and tripping Metal's argument validation — only the matching mip-level sentinel was being resolved. Also, `depth24plus_stencil8` was cast directly to Metal's optional/Intel-only `Depth24Unorm_Stencil8`, which paravirtualized CI devices don't support; a new `toMTLPixelFormat()` substitutes the always-supported `Depth32Float_Stencil8` on devices lacking the optional format, matching WebGPU's "depth24plus" semantics.

- **[macOS] `Texture::createView()` picked `MTL::TextureType2D` whenever `dimension` was left at its `tt2d` default**, even when the resolved slice range spanned more than one array layer — Metal requires a `Type2D` view's slice range length to be exactly 1, so viewing "all remaining layers" of an array texture aborted. Now resolves the array layer count first and promotes to `Type2DArray` when it's `>1`, mirroring the check `Device::createTexture()` already uses at creation time.

### Known Issues

- **[Linux/Vulkan] Swapchain image acquire→first-write `WRITE_AFTER_READ` hazard.** Enabling `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` reports a `WRITE_AFTER_READ` hazard on `vkQueueSubmit()`: the color-attachment-write layout transition after `vkAcquireNextImageKHR()` doesn't include `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT` in its `srcStageMask`, so it doesn't synchronize with the prior acquire read. Not caught by default validation (only surfaces with synchronization validation explicitly enabled) and hasn't visibly corrupted a frame on the one driver tested (Intel Iris Graphics 550, Mesa) — but it's a real, reproducible gap, not a false positive. Not yet fixed; needs either an image barrier with the right `srcStageMask` or a `VK_SUBPASS_EXTERNAL` dependency at render-pass begin.
- **[Linux/Vulkan] `Device::createShaderModule()`/`Device::createComputePipeline()`/`Device::createRenderPipeline()` crash (`SIGSEGV`) on empty/invalid SPIR-V bytecode** instead of returning `nullptr`. The Vulkan validation layer correctly reports `VUID-VkShaderModuleCreateInfo-codeSize-01085`/`-pCode-08738` first, but the local driver (Mesa/Intel ANV) doesn't abort on the validation error and the code crashes attempting to use the resulting invalid handle. Reproduces in isolation (`ShaderModule.CreateWithEmptyBytesReturnsNonNull` and four related `RenderPipeline`/`ComputePipeline` tests), confirmed unrelated to any change in this release — pre-existing, just not previously exercised under validation layers. Not yet fixed; needs an explicit `codeSize > 0`/magic-number check ahead of the driver call, mirroring the null-check pattern `createComputePipeline()` already uses for a null module/layout.

## [0.22.1] - 2026-08-07

### Fixed

- **[Linux/Vulkan] `Device::createComputePipeline()` crashed on a null compute module or a null layout** — unlike `createRenderPipeline()`, it unconditionally dereferenced `descriptor.compute.module->native` and `descriptor.layout->native`. Now null-checks the module (returns `nullptr`, matching the DirectX backend's existing behavior) and falls back to an internally-created empty `VkPipelineLayout` when `descriptor.layout` is absent, mirroring `createRenderPipeline()`'s established pattern — including ownership tracking (`ComputePipelineHandle::ownsPipelineLayout`) so the destructor only destroys layouts it actually created.

- **[Linux/Vulkan] `getAddressMode()`/`getCompareOp()`/`pixelFormatToNative()` had no `default:` case**, falling off the end of a non-`void` function — undefined behavior that GCC compiles to a trap instruction. Concretely triggered because `SamplerDescriptor::addressModeU/V/W` had no default member initializer: a zero-initialized `SamplerDescriptor{}` produced `WrapMode(0)`, not a valid enumerator (real values are the GL wrap-mode constants 10497/33071/33648), crashing `getAddressMode()` with `SIGILL` the moment any caller created a sampler from a default-constructed descriptor. Fixed both ends: all three switches gained sane `default:` fallbacks, and `addressModeU/V/W` now default to `WrapMode::clampToEdge`.

- **[Linux/Vulkan] `RenderPassEncoder::draw*()`/`ComputePassEncoder::dispatch*()` recorded `vkCmdDraw`/`vkCmdDispatch` with no pipeline ever bound** — invalid per spec (`VUID-vkCmdDraw-None-08606`), and the local Mesa/Intel driver segfaults on it during recording rather than deferring to a validation error. Now no-ops when `pipelineLayout == VK_NULL_HANDLE` (i.e. `setPipeline()` was never called), matching the guard already used by `setBindGroup()`/`setPushConstants()` in the same files.

- **[Linux/Vulkan] `RenderPassEncoder::end()` crashed on a render pass begun with no attachments** — it unconditionally built a post-pass image-layout-transition barrier against `data->offscreenImage`/`data->currentSwapchainImage`, both `VK_NULL_HANDLE` for a fully attachment-less pass, crashing `vkCmdPipelineBarrier`. Now skips the barrier when there is no image to transition.

- **[Linux/Vulkan] `Texture::createView()` mapped `Aspect::all` to `VK_IMAGE_ASPECT_COLOR_BIT` unconditionally** — wrong for depth/stencil formats, where "all" should mean every aspect the format actually has. A VUID violation (`vkCreateImageView(): ... aspect flags but depth/stencil image formats must have at least one of VK_IMAGE_ASPECT_DEPTH_BIT and VK_IMAGE_ASPECT_STENCIL_BIT set`) that the local driver tolerates silently instead of rejecting — caught by cross-checking new test coverage against Vulkan validation layers. Now derives the aspect mask from the view's actual format, reusing the same format-to-aspect logic already used for a texture's internal default view in `Device::createTexture()`.

- **[Linux/Vulkan] `Device::createTexture()`'s `TextureUsage::renderTarget` → `VkImageUsageFlags` mapping always used `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`**, even for depth/stencil pixel formats — `vkGetPhysicalDeviceImageFormatProperties2` rejects that combination (`VK_ERROR_FORMAT_NOT_SUPPORTED` under validation), breaking depth/stencil render targets (shadow maps, depth pre-pass) on any driver that actually enforces it, though this one silently tolerates it. Now picks `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` for depth/stencil formats.

- **[Android/Vulkan] `CommandBuffer::getGPUExecutionTime()` hung the calling thread forever** when called on a command buffer that was `finish()`ed but never submitted — the timestamp-writing commands are only *recorded* into the buffer, never executed, so `vkGetQueryPoolResults(..., VK_QUERY_RESULT_WAIT_BIT)` blocked on a query that could never become available. Invisible on desktop Mesa/Intel, which apparently doesn't actually block in this case; reproduced as a genuine, indefinite hang on a real Qualcomm Adreno 618 device (Xiaomi Redmi Note 10, Android 13) — confirmed in isolation via `adb shell` with a timeout. Fixed by tracking submission state (`CommandBufferHandle::submitted`, set by both `Device::submit()` overloads right after a successful `vkQueueSubmit`) and returning `0` immediately when not yet submitted, before ever calling `vkGetQueryPoolResults()` — exactly the behavior `CommandBuffer.GetGPUExecutionTimeReturnsZeroBeforeSubmission` already asserted.

- **[Android/Vulkan] The traditional-render-pass fallback crashed on any render pass with no color attachments** — `CommandEncoder::beginRenderPass()` takes this path (`useTraditional`) only on hardware without `VK_KHR_dynamic_rendering`, e.g. a real Adreno 618 on Vulkan 1.1; the desktop Intel driver (Vulkan 1.4, dynamic rendering always available) never exercises this branch at all, so it went untested until run on real hardware. It null-derefed `descriptor.colorAttachments[0].view` when the vector was empty. `buildRenderPass()` had the matching bug one level down: it unconditionally created a color `VkAttachmentDescription` and set `colorAttachmentCount = 1` even for `colorFormat == VK_FORMAT_UNDEFINED`, which `vkCreateRenderPass()` rejects. Fixed both: `colorFormat` is now `VK_FORMAT_UNDEFINED` when there are no color attachments (mirroring how `depthFormat` already defaults to `VK_FORMAT_UNDEFINED` when there's no depth attachment), and `buildRenderPass()` gained a `hasColor` gate mirroring the existing `hasDepth` one. Verified fixed on both a Xiaomi Redmi Note 10 (Adreno 618) and a Samsung Galaxy Tab S7 FE (Adreno 642L).

- **[Build] `tests/CMakeLists.txt` couldn't build `BUILD_TESTS=ON` for any cross-compiled target** — `gtest_discover_tests(campello_gpu_universal_tests)` was missing `DISCOVERY_MODE PRE_TEST`, present on `campello_gpu_integration_tests` but overlooked on this target. Without it, CMake tries to *execute* the freshly-linked binary on the build host immediately after linking, to discover its test list — which fails outright for a cross-compiled binary (Android, iOS device) the host can't run. Fixed by adding the same `DISCOVERY_MODE PRE_TEST`; this is what unblocked cross-compiling and running the suite on real Android hardware in the first place.

### Tests

- **Six new platform integration test files** (`test_sampler.cpp`, `test_bind_group.cpp`, `test_query_set.cpp`, `test_pipeline_layout.cpp`, `test_texture_view.cpp`, `test_adapter.cpp`) covering `Sampler`, `BindGroup`/`BindGroupLayout`, `QuerySet`, `PipelineLayout`, `TextureView`, and `Adapter` — previously exercised only incidentally as setup code inside other tests, with no dedicated coverage of their own parameter space (every `WrapMode`/`FilterMode`/`CompareOp` combination, every `EntryObjectType`, mip/array/cube `TextureView` subranges, explicit-adapter device creation, etc.). 55 new tests; found 4 of the 8 bugs fixed in this release.
- **`test_device.cpp`'s `GetAdaptersReturnsAtLeastOneOnSupportedPlatform`** was gated behind `__ANDROID__`/`__APPLE__`/`_WIN32` and unconditionally `GTEST_SKIP()`'d on Linux — but `Device::getAdapters()` is implemented unconditionally in the Vulkan backend and works fine there. Added `__linux__` to the platform guard, recovering coverage of a working codepath that had never actually been exercised on this platform.
- Full suite (universal + integration) verified at **324/324 passing on Linux/Vulkan** (Intel Iris Graphics 550, Mesa), **239/239 passing on Android/Vulkan** on both a Xiaomi Redmi Note 10 (Adreno 618, Android 13) and a Samsung Galaxy Tab S7 FE (Adreno 642L, Android 14) — cross-compiled with NDK r29 for `arm64-v8a`/API 33 and run directly via `adb shell` (no APK/Gradle needed, since GoogleTest binaries run as plain native executables on Android). Ray tracing tests skip on both real devices (no hardware RT support); all other suites pass with zero failures.

## [0.22.0] - 2026-08-04

### Added

- **[Linux/Vulkan] `Device::createTextureFromDmaBuf()`** — imports an externally-allocated dma-buf as a read-only `Texture` without copying, via the new `DmaBufTextureDescriptor`/`DmaBufPlane` types in `inc/campello_gpu/platform/linux_dmabuf.hpp`. This is the primitive a Wayland compositor needs to composite a client's buffer (as handed to it by wlroots) directly — groundwork for `campello_native`, the planned compositor for the `campello_machine` console OS. Linux/Vulkan only: the method doesn't exist as a symbol at all on Metal/DirectX12/WebGPU (guarded with `#if defined(__linux__) && !defined(__ANDROID__)` in the shared `device.hpp`), since dma-buf is a Linux kernel mechanism with no equivalent concept — or possible caller — on those platforms. Enables `VK_KHR_external_memory_fd`, `VK_EXT_external_memory_dma_buf`, and `VK_EXT_image_drm_format_modifier` when available; uses the *explicit* DRM-modifier image path (`VkImageDrmFormatModifierExplicitCreateInfoEXT`) since the buffer's layout is already fixed by its exporter, chains `VkMemoryDedicatedAllocateInfo` (several drivers, RADV included, expect dma-buf imports to be dedicated rather than suballocated), and selects a memory type from the intersection of `vkGetImageMemoryRequirements2()`'s and `vkGetMemoryFdPropertiesKHR()`'s `memoryTypeBits` — using either alone is wrong. `campello_gpu` never closes a plane's fd, on success or failure: the Vulkan driver dups its own reference during import (verified real-world Mesa/RADV behavior for `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT` specifically, a refcounted kernel object meant for multiple simultaneous consumers — distinct from `OPAQUE_FD`, where the base spec's "ownership transfers to the driver" language applies), keeping the fd-lifetime contract simple rather than needing to get that distinction right per-driver. Only single-plane (`planeCount == 1`) import is implemented; multi-planar formats (e.g. YUV) return `nullptr` for now — not needed for the initial compositor use case. `Texture::upload()` now guards against a null staging buffer, since imported textures don't have one.

- **[Linux/Vulkan] `Device::getSupportedDmaBufModifiers()` and `Device::getDrmDeviceNode()`** — the two remaining capability queries a wlroots-compatible renderer needs alongside dma-buf import. Verified against wlroots' actual source before implementing: its Vulkan renderer never calls DRM/KMS functions directly and only ever imports pre-allocated dma-bufs, never exports its own — that's the DRM backend/allocator's job, not the renderer's. `getSupportedDmaBufModifiers()` backs `wlr_renderer_impl`'s `get_render_formats()`/`get_dmabuf_texture_formats()` (which DRM format modifiers this device can render into or sample, respectively, selected by the `requiredUsage` argument), via `vkGetPhysicalDeviceFormatProperties2()` + `VkDrmFormatModifierPropertiesListEXT`. `getDrmDeviceNode()` backs `get_drm_fd()` — which `/dev/dri` node this Vulkan device corresponds to (`VK_EXT_physical_device_drm`'s primary/render major:minor numbers), queried once at device creation like `memoryProperties`/cooperative-matrix data already are, so a compositor can confirm this device matches its own already-open DRM fd by comparing device numbers; `campello_gpu` never opens a device file itself. New `DeviceData::dmaBufImportEnabled` cached flag so the modifier query doesn't trust `VkDrmFormatModifierPropertiesListEXT` to behave when the extension wasn't actually enabled at device creation.

### Fixed

- **Missing `<cstdint>` include in `begin_render_pass_descriptor.hpp`** — the header uses `uint32_t` in several fields but never included `<cstdint>` itself, relying entirely on it arriving transitively via some other header included earlier in a given translation unit. Held on GCC 13 (the compiler CI's `ubuntu-24.04` runner actually uses — confirmed from its own job log), but broke immediately when compile-verifying this release's other changes on GCC 16: newer libstdc++ has been progressively pruning exactly this kind of accidental transitive include. Manifested as a confusing downstream error ("no member named `stencilClearValue`") in `src/vulkan/command_encoder.cpp`, one call site removed from the actual missing include — `uint32_t` failing to parse as a type meant the compiler couldn't add that struct member at all for that translation unit. A real, latent, compiler-version-dependent bug, not a contradiction of CI having been green.

### Tests

- **`tests/platform/test_dmabuf.cpp`** (Linux only — `list(APPEND ...)` gated on `CMAKE_SYSTEM_NAME` in `tests/CMakeLists.txt`, since the API it tests doesn't exist as symbols on other backends) — covers `createTextureFromDmaBuf()`'s input validation (multi-plane rejection, negative fd, zero dimensions — all rejected before the driver is ever touched, so these hold regardless of environment) plus `getSupportedDmaBufModifiers()`/`getDrmDeviceNode()` as pure capability queries needing no real dma-buf. Verified directly against CI's actual environment (`vulkaninfo` under lavapipe/llvmpipe): supports `VK_KHR_external_memory_fd`/`VK_EXT_external_memory_dma_buf` but not `VK_EXT_image_drm_format_modifier`/`VK_EXT_physical_device_drm`, and no `/dev/dri` node exists in the container at all — tests assert on that gracefully-degraded behavior (empty modifier list, `DrmDeviceNode::valid == false`) rather than hardcoding an assumption, confirmed by actually running them: 8 pass, 1 skips (the DRM-node field check, which only applies when a real DRM device is reported). A real dma-buf round-trip test (GBM-allocate, import, sample) is deliberately not included yet — no `/dev/dri` exists in CI, so it would only ever skip there, and a new `libgbm` build dependency isn't worth adding for a path never exercised; revisit once there's real hardware to run it against.

## [0.21.1] - 2026-08-02

### Added

- **`examples/macos_offscreen/raytracing_scene` — headless Cornell box ray tracing demo** — standalone command-line example exercising campello_gpu's ray tracing API end to end: a 16:9 Cornell box with 12 procedurally-placed cubes/spheres (checkerboarded, sharing just 2 BLAS across 17 TLAS instances), hard shadows, and up to 3 chained mirror-reflection bounces off every surface. Includes an in-process benchmark mode (configurable iteration count and resolution) and a README with build instructions and sample timings.

### Fixed

- **[Metal] `RayTracingPassEncoder::setBindGroup()` crashed whenever a texture was bound to a ray tracing pass** — it cast the `Texture`'s opaque `native` handle directly to `MTL::Texture*`, but on Metal that handle is actually a `MetalTextureHandle*` wrapper (the real `MTL::Texture*` plus allocation bookkeeping), same as `render_pass_encoder.cpp`, `command_encoder.cpp`, and `texture.cpp` already unwrap correctly — `ray_tracing_pass_encoder.cpp` was the one call site skipping that indirection. Found while building a ray tracing demo that binds an output storage texture for `rayGenMain` to write into: `setTexture:atIndex:` received a garbage pointer read from the wrong struct offset, faulting inside the AGX driver (`EXC_BAD_ACCESS` in `-[AGXG17FamilyComputeContext setTexture:atIndex:]`). Fixed by unwrapping through `MetalTextureHandle` like the other call sites; `tests/platform/test_raytracing.cpp` also gained broad coverage of BLAS/TLAS creation, build-flag variants, update/copy commands, and pipeline creation edge cases that weren't exercised before.

## [0.21.0] - 2026-07-30

### Added

- **Sanitizer build profiles (`ASan`, `LSan`, `UBSan`) as custom `CMAKE_BUILD_TYPE` values** — e.g. `cmake -S . -B build_asan -DCMAKE_BUILD_TYPE=ASan`. New `lsan_suppressions.txt` filters known-noisy leak reports from graphics drivers/loaders/system runtimes (Metal/AGX, Vulkan loader and vendor ICDs, DXGI/D3D12, Dawn/wgpu, shader compilers, libc++/libobjc internals) so real, project-owned leaks aren't buried in driver noise. Used to root-cause and verify the Metal command-buffer/encoder leak fix below via a real before/after Instruments + LeakSanitizer comparison.
- **`Device::createBuffer()` prefers device-local memory for non-mappable buffers** — previously always allocated `HOST_VISIBLE | HOST_COHERENT` memory regardless of usage, which is correct but suboptimal on discrete GPUs for buffers with no `mapRead`/`mapWrite` usage (pure GPU-side data — vertex/index/storage buffers written once via `upload()` and never touched by the CPU again). Now tries `DEVICE_LOCAL | HOST_VISIBLE` first (the common case on UMA/mobile GPUs, where this changes nothing since that memory is already host-visible), falls back to pure `DEVICE_LOCAL` (discrete GPUs — routes `Buffer::upload()`/`download()` through a new staging-buffer-plus-command-copy path, added alongside this), and finally to `HOST_VISIBLE | HOST_COHERENT` for buffers that request `mapRead`/`mapWrite` or when device-local memory isn't available at all.

### Changed

- **[Vulkan] Physical device memory properties cached once per `Device` instead of re-queried on every `Device::createBuffer()`/staging-buffer allocation** — `vkGetPhysicalDeviceMemoryProperties()` describes a fixed, unchanging property of the physical device, but was being re-queried on every single buffer allocation. Found mattering in practice on Android: `campello_widgets`' Vulkan draw backend calls `createBuffer()` fresh for every draw call's vertex data (see its own fix for the real solution — pooling those buffers instead of recreating them — but this at least removes the redundant per-call query cost in the meantime for any caller that does need to create buffers per-draw). `DeviceData::memoryProperties` is now populated once at device creation and reused by both `Device::createBuffer()` and `Buffer::upload()`/`download()`'s staging-buffer path.

- **[Vulkan] `kFramesInFlight` raised from 2 to 3** — matches the swapchain's own `minImageCount` (also forced to 3, see `recreateSwapchain()`). Gives the CPU a full extra frame of slack ahead of the GPU before `beginFrameRing()`'s fence wait ever blocks it. Measured on a Galaxy Tab S7 FE (Adreno 642L): helps bursty, event-driven workloads (e.g. `campello_widgets`' pointer-driven draw-surface demo) noticeably — that "images in flight" wait dropped from ~9.75ms to ~4.46ms average — but doesn't move a continuously-saturated ticker-driven workload (already submitting flat-out every vsync, so extra ring depth just changes burstiness, not sustained throughput). Costs one more frame of input-to-display latency and one more generation's worth of descriptor pools/fences/semaphores/retained command buffers.

- **[Vulkan] `VK_PRESENT_MODE_MAILBOX_KHR` requested as the default present mode** (both swapchain creation and `recreateSwapchain()`), via a new `choosePresentMode()` helper that automatically falls back to `VK_PRESENT_MODE_FIFO_KHR` if a surface doesn't support it. Removes the `vkAcquireNextImageKHR`/"images in flight" fence wait's entire cost — measured on the same Tab S7 FE dropping from ~10.6ms/frame average to ~0.2ms — because that wait is CPU time blocked until the display actually wants the next image; MAILBOX lets the CPU/GPU move on immediately instead, always keeping only the most recently completed frame ready for the next vsync. Verified this is not the classic "unbounded render loop floods the GPU with wasted frames" MAILBOX battery-drain scenario: `campello_widgets`' own frame requests are already vsync-gated by `FrameScheduler`/the platform choreographer callback, independent of present mode — measured frame count stayed ~60/sec (matching the display's refresh rate) under both FIFO and MAILBOX. The remaining, smaller caveat is GPU clock-scaling: some mobile driver DVFS heuristics lean on a predictable vsync-anchored work rhythm to downclock confidently between frames, and MAILBOX's submission timing can be less predictable for those heuristics — real and driver-dependent, but not something this repo has instrumented directly (no on-device power/clock-frequency measurement, just Vulkan-level timing).

- **[Vulkan] Cache for the transient offscreen `VkRenderPass` objects `command_encoder.cpp`'s traditional-render-pass-fallback `beginRenderPass()` builds** — previously called `vkCreateRenderPass()`/`vkCreateFramebuffer()` fresh for every single offscreen composite (every `ClipRRect`/`ClipOval`/`ShaderMask`/`BoxShadow` capture pass), despite `buildRenderPass()` being a pure function of just four parameters (color format, depth format, load op, final layout) with no image-specific state — in practice only 1-2 *distinct* combinations ever occur per frame despite ~15-20 offscreen composites needing one. New `DeviceData::offscreenRenderPassCache` (keyed by `RenderPassKey`) reuses the same `VkRenderPass` across composites and frames instead, torn down only at device destruction. Framebuffers are still per-image-view and remain transient (destroyed with their `CommandBuffer`) since they aren't pure-function-cacheable the same way. This is a genuine CPU/driver-object-creation cost reduction, confirmed via `CommandBuffer::getGPUExecutionTime()` to NOT be what was driving that measurement's ~10-12ms floor (see the box-shadow blur fix in `campello_widgets`' own changelog for what actually was) — real GPU hardware timestamps only capture GPU-side execution, not CPU-side object creation before it, so this fix's value is in reduced CPU/driver overhead and hitch risk, not the GPU_EXEC number specifically.

### Fixed

- **[Metal] Real memory leak in the command-buffer/encoder lifecycle, root-caused via Xcode Instruments** — three separate leaks, all in `src/metal/*`:
  - `RenderPassEncoder`/`ComputePassEncoder`/`RayTracingPassEncoder` now track an `ended` flag and call `endEncoding()` from their destructor if the caller never called `end()` explicitly. An un-ended Metal encoder keeps its owning command buffer "in flight" indefinitely from the driver's perspective, which was the dominant source of unbounded memory growth (confirmed via a live before/after Instruments trace running the `campello_widgets` gallery app's Images tab, which continuously creates/destroys encoders every frame).
  - New `MetalAutoreleasePool` RAII wrapper (`src/metal/common.hpp`) applied at every public entry point that creates Metal-cpp objects via non-`alloc`/`new`/`copy`-prefixed methods (`Device::createDefaultDevice()`/`createDevice()`/`getAdapters()`/`getName()`/`getFeatures()`/`getEngineVersion()`/`getMemoryInfo()`/`getMetrics()`/`getMemoryPressureLevel()`, `CommandEncoder::beginRenderPass()`/`beginComputePass()`/`beginRayTracingPass()`/`clearBuffer()`/`copyBufferToBuffer()`/`copyBufferToTexture()`/`copyTextureToBuffer()`/`copyTextureToTexture()`/`generateMipmaps()`/`finish()`/`resolveQuerySet()`/`writeTimestamp()`, `Buffer::upload()`/`download()`, `Texture::upload()`/`download()`) — per Metal-cpp's Objective-C-derived memory convention, these return autoreleased objects that only get drained when an active autorelease pool exists; on a background thread or non-AppKit consumer with none active, they leaked until the next unrelated pool drain happened to occur (or never, on a thread that doesn't have one).
  - `Texture::download()` was creating a brand new `MTL::CommandQueue` via `device->newCommandQueue()` on every single call and never releasing it — now reuses the device's own persistent command queue (`MetalDeviceData::commandQueue`) instead.
  - Also completes `Adapter`'s reference-counting: the shared `inc/campello_gpu/adapter.hpp` gained a real (non-defaulted) `~Adapter()` declaration, requiring a definition on every backend. Metal's `Device::getAdapters()` was retaining each `MTL::Device*` it returned but nothing ever released it — `~Adapter()` now does. DirectX's `IDXGIAdapter1*` had the identical bug (the `AddRef` from `EnumAdapterByGpuPreference()` was never matched by a `Release()`) — fixed the same way. Vulkan (`VkPhysicalDevice` handles are enumerated from a `VkInstance`, not individually reference-counted) and WebGPU (`native` is always null on this backend) needed no-op destructors purely to satisfy the shared declaration.

- **[Vulkan] `Device::submit()` recreated the entire swapchain on literally every frame on a physically-rotated display** — both `submit()` overloads treated `VK_SUBOPTIMAL_KHR` from `vkQueuePresentKHR` the same as the mandatory `VK_ERROR_OUT_OF_DATE_KHR`, triggering `recreateSwapchain()` (a full `vkDeviceWaitIdle()` plus destroying and reallocating every swapchain image via the platform's Gralloc HAL) unconditionally. But `VK_SUBOPTIMAL_KHR` is advisory per spec — "the swapchain can still be used to successfully present" — and on a tablet in landscape (surface `currentTransform = ROTATE_90`), it's *permanent*, not transient: this renderer deliberately never pre-rotates content, so `recreateSwapchain()` always requests `preTransform = IDENTITY` whenever the surface's `supportedTransforms` includes it, regardless of the surface's actual `currentTransform` — so every recreation just produces another swapchain that immediately reports SUBOPTIMAL again on the very next present, forever. Root-caused via a real Perfetto trace on a Galaxy Tab S7 FE (`campello_widgets`' gallery app): confirmed `CreateSwapchainKHR` firing once per `AcquireNextImageKHR` (i.e. every single frame, ~155/155 in one capture), each costing 5-16ms plus cascading buffer teardown/reallocation, while actual GPU execution time (measured via the existing but previously-unused `CommandBuffer::getGPUExecutionTime()`) was a steady ~10ms and CPU-side draw-list encoding ~10-20ms — this alone was the dominant cost, not GPU or CPU work. Now only recreates on `VK_ERROR_OUT_OF_DATE_KHR` (genuinely mandatory — the swapchain is no longer usable at all, e.g. after an actual resize); content already presents correctly with `IDENTITY` on a rotated display regardless (confirmed visually), SUBOPTIMAL was never a correctness issue, just an unnecessary optimality hint being treated as an error. Measured live: gallery app's Images tab on the same device went from ~21fps (~48ms/frame, every frame paying for a full swapchain rebuild) to ~34-43fps (~23ms/frame) — more than double, now bottlenecked by legitimate CPU draw-list encoding cost instead.

## [0.20.0] - 2026-07-24

### Added

- **`PushConstantRange` / `PipelineLayoutDescriptor::pushConstantRanges` / `RenderPassEncoder::setPushConstants()`** — lets callers update a small range of inline per-draw data (a handful of floats, e.g. a transform + color) directly in command-buffer state, without allocating a fresh descriptor set/bind group just to update a few bytes. Vulkan is the primary beneficiary and gets a real implementation (`vkCmdPushConstants`, wired into `vkCreatePipelineLayout` via the new `pushConstantRanges`); Metal, DirectX 12, and WebGPU each get an explicit no-op stub, since none of them need this concept the same way — Metal/D3D12 have their own cheaper small-constant-data paths, WebGPU has no stable equivalent yet.

- **`Device::getSwapchainPixelFormat()`** — returns the pixel format this device's swapchain was actually created with (`PixelFormat::invalid` for a headless/offscreen-only device). Not every backend/surface combination supports every format — see the Vulkan BGRA8/RGBA8 fix below — so callers building their own textures meant to be render-pass-compatible with the swapchain (e.g. offscreen composite targets) now have a way to ask instead of assuming BGRA8. Implemented on all four backends: Vulkan returns the format actually negotiated at swapchain creation; Metal always returns `bgra8unorm` (its swapchain is owned entirely outside this library by the consumer's `MTKView`, which every consumer configures as BGRA8Unorm); DirectX returns `rgba8unorm` (fixed at swapchain creation, no fallback path); WebGPU converts its tracked `surfaceFormat`.

### Fixed

- **[Vulkan] `Device::submit()` fully serialized the CPU and GPU every frame** — called `vkQueueWaitIdle` unconditionally after every submission, the same class of bug already fixed on the DirectX 12 backend in 0.19.0. Under sustained GPU-side load (driver/compositor contention, thermal throttling, a fast fling forcing many offscreen composite passes) this stalled whichever thread called `submit()`, which on Android is also the thread servicing input and vsync — freezing the whole app. Replaced with a `kFramesInFlight = 2` ring (`DeviceData::beginFrameRing()`): per-slot semaphores/fences let `submit()` signal and return immediately, only waiting — at the start of the *next* `createCommandEncoder()` — for the one ring slot about to be reused, normally already long finished by then. All waits (ring-slot fence, image acquire, images-in-flight) are bounded at 3s rather than `UINT64_MAX`, so a genuinely stuck GPU degrades to "skip this frame" instead of hanging forever with zero CPU usage and no way back.

- **[Vulkan] Fixed-size descriptor pool silently exhausted after the first frame or two of real content** — a single 100-set `VkDescriptorPool`, created once and never reset, filled up permanently since `Device::createBindGroup()` allocates a fresh descriptor set on essentially every draw call. Every draw after that point silently no-op'd (`createBindGroup()` failing, checked and skipped — no crash, no validation error), producing a black screen with zero CPU usage and zero errors. Fixed by giving each frame-in-flight ring slot its own 4096-set pool, reset via `vkResetDescriptorPool` once `beginFrameRing()` confirms that slot's prior GPU work is done — mirroring the DirectX 12 backend's per-generation descriptor-slot recycling.

- **[Vulkan] Acquired swapchain image could still be owned by an older in-flight frame** — the swapchain is created with more images than `kFramesInFlight` ring slots, so a ring slot's own fence proves the GPU is done with *that slot's* last submission but not that a freshly-acquired image (which may have last been used by a different, older frame) is actually free. Added the standard "images in flight" tracking (`DeviceData::imagesInFlight`): `CommandEncoder::beginRenderPass()` now also waits on the specific image's last owning fence after acquire.

- **[Vulkan] Restarted render pass (e.g. after an offscreen sub-pass) used the wrong barrier for `loadOp::load`** — the image-layout transition back to `COLOR_ATTACHMENT_OPTIMAL` always used `VK_ACCESS_NONE`/`VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT` as if starting fresh, even though a restarted pass loads (not clears) the previous contents. The barrier now makes prior color writes visible and stalls color output appropriately when `loadOp` is `load` rather than `clear`.

- **[Vulkan] `Texture::upload()` / `Texture::download()` raced the main render thread over a shared `VkCommandPool`** — `VkCommandPool` requires external synchronization for every operation that touches it, including recording into any command buffer allocated from it. These methods can run on a background thread (e.g. an image-loader worker pool) while the main thread simultaneously records via `Device::createCommandEncoder()`'s `commandPool`; an earlier version only locked around the allocate and submit+free steps, leaving the entire record section unlocked and racing the raster thread (caught via Vulkan's validation layer as `UNASSIGNED-Threading-MultipleThreads-Write`). Fixed by giving texture uploads/downloads their own dedicated `uploadCommandPool`, and holding one lock across the whole allocate-record-submit-wait-free sequence instead of splitting it.

- **[Vulkan] Swapchain format selection could pick RGBA8 over BGRA8 depending on driver-reported list order** — previously took "whichever of BGRA8_UNORM/RGBA8_UNORM appears first in the surface's format list," which chose RGBA8 on at least one real Android/Adreno device. Renderer clients (including `campello_widgets`) hardcode `bgra8unorm` for offscreen composite textures (shadows, clip shapes) to match what the Metal backend always uses, so a device landing on RGBA8 made every swapchain-format pipeline render-pass-incompatible with those offscreen composites (`VUID-vkCmdDraw-renderPass-02684`), while ordinary direct-to-swapchain draws rendered fine — an "some content renders correctly, some doesn't" symptom. Now strictly prefers BGRA8_UNORM, only falling back to RGBA8_UNORM if the surface doesn't offer BGRA8 at all. See the new `Device::getSwapchainPixelFormat()` above for how callers can now detect when that fallback occurred.

- **[Vulkan] Swapchain `preTransform` could present already-correct content as if it needed rotation** — always requested `surfaceCapabilities.currentTransform`, telling the compositor "my images are pre-rotated to match the display," but nothing in this renderer actually applies a pre-rotation matrix. On a device reporting a non-identity transform (landscape orientation, or an Android WSI quirk reporting rotation even in portrait), the compositor then rotated genuinely-unrotated content, showing the UI sideways/misaligned. Now requests `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` whenever the surface supports it (falling back to `currentTransform` only if it doesn't), both at initial swapchain creation and in `recreateSwapchain()`.

### Tests

- `PushConstantRange`/`PipelineLayoutDescriptor` universal tests — construction, defaults, and multi-range `pushConstantRanges`.
- `RenderPassEncoder.SetPushConstants*` integration tests — exercise `setPushConstants()` without a bound pipeline (no compiled cross-platform shader is available in this suite), verifying the Vulkan guard path and the Metal/DirectX/WebGPU no-op stubs never crash.
- New `FrameRing.*` integration tests exercising the frame-in-flight ring, descriptor-pool recycling, and upload-thread separation added above — all headless (`Device::createDefaultDevice(nullptr)`), since the ring/descriptor pools/fences are created unconditionally regardless of whether a real window surface is provided: `ManyConsecutiveFrameCyclesDoNotHang` runs 50 `createCommandEncoder()`/`submit()` cycles, well past `kFramesInFlight`, so every ring slot gets reused several times over; `ManyBindGroupsInASingleGenerationAllSucceed` allocates 250 bind groups within a single frame-ring generation, a direct regression test for the fixed-size-descriptor-pool exhaustion bug above (the old pool capped at 100 sets total); `ConcurrentTextureUploadDuringFrameSubmissionDoesNotCrash` hammers `Texture::upload()` from a background thread while the main thread submits ordinary frames, a regression test for the `VkCommandPool` race fixed above.
- **CI**: `build-linux-vulkan` now installs `vulkan-validationlayers` and configures with `-DCAMPELLO_GPU_VALIDATION=ON`, so synchronization/threading bugs like the `VkCommandPool` race above surface as validation errors on every push instead of depending on Mesa lavapipe happening to crash or produce visibly wrong output. The `|| true` that previously swallowed the Linux Vulkan integration test step's exit code was also removed — integration test failures now actually fail the job, where before they were silently ignored.

## [0.19.0] - 2026-07-16

### Added

- **[Vulkan/Metal] `Feature::cooperativeMatrix`** — hardware-accelerated small-tile matrix multiply-accumulate detection, wired into both `Adapter::getFeatures()` and `Device::getFeatures()`. Metal gates on `MTL::GPUFamilyApple6`; Vulkan gates on `VK_KHR_cooperative_matrix` plus its `shaderFloat16`/Vulkan-1.2 dependency, and enables `VkPhysicalDeviceCooperativeMatrixFeaturesKHR` at device creation (generalizing the existing raytracing `pNext` chain so both can coexist). DirectX and WebGPU do not implement cooperative matrix yet — researched and deferred, see `TODO.md`.

- **`CooperativeMatrixComponentType` / `CooperativeMatrixProperties` / `Device::getCooperativeMatrixProperties()`** — lets callers query the actual supported `(MSize, NSize, KSize, AType, BType, CType, ResultType)` tile shapes before compiling/dispatching a cooperative-matrix kernel, instead of relying on a bare feature flag. Vulkan returns real queried tuples (via `vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR`); Metal/DirectX/WebGPU return an empty vector (Metal has no runtime shape query at all — shapes are fixed by MSL template parameters at shader-compile time).

- **`Fence::didFail()` / `Fence::failureReason()`** on Vulkan and DirectX (previously Metal-only, via `MTLCommandBuffer::status()`). Vulkan detects `VK_ERROR_DEVICE_LOST`; DirectX detects `GetCompletedValue() == UINT64_MAX` plus `GetDeviceRemovedReason()`. Both are coarser than Metal (whole-device loss, not per-submission failure).

- **[Vulkan/Metal/DirectX/WebGPU] `Feature::fp16`, [Vulkan/Metal/DirectX] `Feature::subgroupOperations`** — two new boolean capability flags. `fp16`: Vulkan `shaderFloat16` (queried and actually enabled at device creation), Metal (unconditional — native MSL `half` type on every device), DirectX `Native16BitShaderOpsSupported`, WebGPU `shader-f16`. `subgroupOperations`: Vulkan subgroup basic+ballot+arithmetic support in the compute stage, Metal gated on `MTL::GPUFamilyApple6` (same bar as `cooperativeMatrix`), DirectX `WaveOps`. Not queried on WebGPU — Emscripten SDK 3.1.74's bundled `webgpu.h` doesn't define `WGPUFeatureName_Subgroups` yet, even though the feature itself has shipped in Chrome since version 134; see `TODO.md`.

- **[DirectX 12] Debug tooling** — `_DEBUG`-only D3D12 validation layer + `ID3D12InfoQueue` wiring, polled once per `Device::submit()` (`ID3D12InfoQueue1::RegisterMessageCallback` isn't supported on all driver stacks, e.g. older Intel iGPU drivers).

### Fixed

- **[Vulkan] `Fence::wait()` / `isSignaled()`** — only checked `VK_SUCCESS`, so a lost device was treated as "never signaled," letting callers spin/hang forever. Now also detects `VK_ERROR_DEVICE_LOST`.

- **[Vulkan] `Device::getFeatures()` missing `Feature::raytracing`** — only `Adapter::getFeatures()` inserted it, even though `DeviceData::rayTracingEnabled` was already tracked and available in that function; every Vulkan ray tracing integration test was silently skipping as a result.

- **[Android/Vulkan] Build failure on all 4 ABIs** — `ld.lld: error: undefined symbol: vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR`. Android's `libvulkan.so` only statically exports core Vulkan symbols; every other KHR extension function in `device.cpp` is already resolved dynamically via `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`, but the cooperative-matrix properties query called this one directly. Fixed by resolving it the same way. Caught via CI job log inspection and reproduced/verified locally by building against the real Android NDK toolchain before and after the fix.

- **[WebGPU] Build failure (`build-wasm`)** — `error: use of undeclared identifier 'WGPUFeatureName_Subgroups'`. Emscripten SDK 3.1.74's bundled `webgpu.h` does not define this enumerator (confirmed by fetching the header at that exact SDK tag). Fixed by not querying `Feature::subgroupOperations` on this backend. Caught via CI job log inspection and reproduced/verified locally by installing the exact CI Emscripten version and building before and after the fix.

- **[DirectX 12] `srvHeap` / `rtvExtraHeap` descriptor slot leak** — slots were only ever incremented, never reclaimed even after the owning `BindGroup`/`Texture` was destroyed — fatal for callers that create fresh bind groups or render targets per frame. `BindGroup::~BindGroup()` and `Texture::~Texture()` now push freed slots onto a pending list, merged into the reusable free list after `Device::submit()`'s `waitForGpu()` (GPU descriptor tables are read at execution time, not recording time), guarded by a mutex since these destructors can run on a background thread.

- **[DirectX 12] `rtvExtraHeap` capacity exhaustion corrupted D3D12 runtime memory** — the free-list reclamation above only makes a freed slot reusable after the *next* `Device::submit()`'s `waitForGpu()`; a single frame that creates more distinct render-target textures than the heap has slots for still exhausts it before any recycling can help. The heap was also fixed at a mere 64 slots with no bounds checking — `rtvExtraCpuAt()` computed a CPU descriptor handle past the heap's actual backing storage for any index ≥ 64, and the subsequent `CreateRenderTargetView` write there corrupted adjacent D3D12 runtime memory, observed via a full minidump as `D3D12SDKLayers.dll` itself raising a hard exception several frames deep inside its own validation code — not a crash at the call site that actually caused it. Reproduced by a widgets consumer whose grid-of-clipped-thumbnails layout could mint more than 64 distinct offscreen (ClipRRect/ClipOval) render-target sizes within a single frame. Fixed by growing `rtvExtraHeap` to `DeviceData::kRtvExtraHeapCapacity` (1024) and having `allocRtvExtraIndex()` return a sentinel (`UINT(-1)`) when that capacity would be exceeded; `Device::createTexture()` now fails the texture creation cleanly in that case instead of writing out of bounds.

- **[DirectX 12] Texture SRV staging heap** — `Device::createTexture()`'s pre-baked SRV previously lived in the same shader-visible `srvHeap` that `createBindGroup()` later copies from via `CopyDescriptorsSimple`, which D3D12 forbids (a shader-visible heap's CPU handle cannot be a copy source). Moved to a new non-shader-visible staging heap.

- **[DirectX 12] Root signature could not mix sampler and resource ranges** — `createUniversalRootSignature()` built one descriptor table per `BindGroupLayout`, mixing `SAMPLER` and `CBV/SRV/UAV` ranges in the same table, which D3D12 forbids. Now emits two tables (`[resource, sampler]`) per layout, matching `setBindGroup()`'s existing `[index, index+1]` convention.

- **[DirectX 12] `createRenderPipeline()` ignored the caller's `PipelineLayout`** — always built a hardcoded fixed PBR/IBL SRV-only root signature, so no render pipeline could ever bind a uniform buffer. Now builds the root signature from `descriptor.layout`, matching `createComputePipeline()`.

- **[DirectX 12] Missing resource-state barriers for offscreen render targets used as shader resources** — D3D12 has no implicit state tracking; a texture rendered to and then sampled (blur/composite passes) needs an explicit transition between the two uses. `beginRenderPass()`/`RenderPassEncoder::end()` now transition color attachments between `RENDER_TARGET` and a shader-readable state via a new `TextureHandle::currentState` field.

- **[DirectX 12] `RenderPassEncoder::setBindGroup()` crash when called before `setPipeline()`** — called `SetGraphicsRootDescriptorTable` with no root signature ever set on the command list, undefined behavior per the D3D12 spec, observed as a hard access violation rather than a validation error. Now guarded by `RenderPassEncoderHandle::hasRootSignature`, set by `setPipeline()`.

- **[DirectX 12] `createBottomLevelAccelerationStructure()` / `createTopLevelAccelerationStructure()` crash on non-RT hardware** — only checked that the `ID3D12Device5` interface existed (an API/runtime-version check, unrelated to hardware capability), not `D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier` (the actual check `getFeatures()` already uses). Now returns `nullptr` early when the raytracing tier is unsupported, instead of crashing.

- **[DirectX 12] `Device::submit()` fully serialized the CPU and GPU every frame** — `submit()` called `waitForGpu()` (a full pipeline drain) after every single frame; combined with a vsync-gated `Present(1, 0)`, this meant the CPU could never start recording frame N+1 until frame N had completely finished executing *and* presented, so any normal per-frame GPU execution-time variance (routine on integrated GPUs) had no slack to absorb and directly stalled the whole pipeline for a missed vblank. Confirmed via a consumer's own raster sub-phase timing: CPU-side command encoding stayed ~3ms across both "good" (~9-13ms total) and "bad" (~35-85ms total) frames for the *same* recorded work — the entire swing was inside `submit()`'s wait. Fixed by replacing the wait-every-frame model with a frame-in-flight ring exactly as deep as the swapchain itself (`DeviceData::kFramesInFlight`, tied to `kFrameCount`): `submit()` now signals a fence and stores the `CommandBuffer` against its ring slot without blocking, and the actual wait (`DeviceData::beginFrameRing()`) moved to the *start* of the next `createCommandEncoder()`, where it only waits for the specific frame that last occupied the ring slot about to be reused — normally already long finished by the time the ring comes back around to it. This is a source-compatible behavior change to `Device::submit()`'s previously-documented "blocks until GPU idle" contract; see the "Tests" note below for the one caller-visible consequence found in this codebase's own test suite.

- **[DirectX 12] `srvHeap`/`rtvExtraHeap` pending-slot recycling was unsafe once `submit()` stopped blocking** — a direct correctness follow-through of the fix above. The pending-free lists (`srvPendingFreeSlots`/`rtvExtraPendingFreeSlots`) were a single shared list, drained synchronously right after `waitForGpu()` — safe only because nothing could be "ahead" of the GPU at that point. Once `submit()` no longer blocks, a resource could drop its last reference (e.g. from a background asset-loading thread) while the *immediately preceding* frame's GPU work — one ring rotation back, not yet confirmed done — might still be reading that exact descriptor slot, silently corrupting a live draw. Fixed by partitioning each pending list into one array slot per ring generation (`std::array<std::vector<UINT>, kFramesInFlight>`), attributing each free to whichever generation is current at the moment it happens, and draining only that generation's own list once its own fence is confirmed signaled inside `beginFrameRing()`.

- **[DirectX 12] `createCommandEncoder()` recreated its command allocator/list/GPU-timing query resources from the driver every single frame** — `CreateCommandAllocator`/`CreateCommandList`/`CreateQueryHeap`/`CreateCommittedResource` (the timestamp readback buffer) were all called fresh every frame and released ~2 frames later, a measured ~3ms/frame of avoidable CPU-side driver overhead on an integrated GPU. Since `beginFrameRing()`'s fence wait already proves the GPU is done with whatever a given ring slot last submitted — exactly the precondition `ID3D12CommandAllocator::Reset()` requires — `createCommandEncoder()` now reclaims (steals) those four objects from the ring slot's retiring `CommandBuffer` and calls `Reset()` on the allocator/list instead of recreating them, falling back to real creation only the first time a given ring slot is ever used. Dropped this function's measured average cost from ~4.1ms to ~0.17ms in a consumer's live gallery app.

- **[DirectX 12] `CommandEncoder::generateMipmaps()` / `Texture::upload()` assumed a texture always starts in `COMMON` state** — both hardcoded `StateBefore = D3D12_RESOURCE_STATE_COMMON` when building their first resource-transition barrier. Wrong for any texture created with render-target usage: `Device::createTexture()` creates those already in `RENDER_TARGET` state for every subresource, not `COMMON` — a real before-state mismatch the D3D12 debug layer flags (`"before=COMMON... does not match... RENDER_TARGET"`), previously masked simply because no caller had hit the exact code path that would expose it. `generateMipmaps()` additionally needed per-subresource first-touch tracking (each mip level of a mip chain is touched exactly once as a copy source and once as a copy destination) and had to build its barrier list dynamically rather than unconditionally emitting both barriers — D3D12 also forbids a transition whose `StateBefore` equals `StateAfter`, which a render-target-usage texture's untouched subresources hit on first touch (already sitting in `RENDER_TARGET`, the exact state the destination barrier would transition *to*). Both functions now read and update `TextureHandle::currentState` (the same field the resource-state-barrier fix above introduced) instead of assuming a fixed starting state.

### Tests

- `Fence.DidFailIsFalseAfterSuccessfulSubmission` — exercises `Fence::didFail()` / `failureReason()` against a real successful submission on real hardware.
- `Device.GetCooperativeMatrixPropertiesDoesNotThrow`, `Device.GetCooperativeMatrixPropertiesEmptyWhenFeatureAbsent`, `Device.CooperativeMatrixFeatureNotReportedOnDirectX` — exercise `Device::getCooperativeMatrixProperties()`.
- Fixed `CommandEncoder.CopyTextureToTextureMipLevels` — was copying a 64x64 region into a 32x32 mip-1 destination, out of bounds per the D3D12 debug layer.
- Fixed the `RenderPassEncoder` test helper `makeRTView()` — returned a `TextureView` after letting its source `Texture` (and D3D12 resource) be destroyed, a use-after-free newly exposed by the resource-state tracking above.
- Fixed `CommandEncoder.GenerateMipmapsDoesNotCrash` — dropped its `tex` immediately after `submit()` with nothing else keeping it alive. Safe under the old blocking `submit()`; unsafe (and always a latent violation of `submit()`'s own documented contract) once `submit()` stopped blocking — `tex` is a separate resource, and `submit()` only guarantees the `CommandBuffer` object itself is safe to release immediately, not everything it referenced. Now calls `device->waitForIdle()` before `tex` goes out of scope, matching the pattern already used by the neighboring `GenerateMipmapsProducesCorrectContent` test.

## [0.18.0] - 2026-07-04

### Added

- **[Vulkan] `DeviceData::gpu_mutex`** — `std::mutex` in `DeviceData` that serializes all `VkCommandPool` and `VkQueue` access across threads. `VkCommandPool` is not thread-safe; concurrent `vkAllocateCommandBuffers` / `vkFreeCommandBuffers` calls from the raster thread and the main thread previously caused undefined behaviour.

### Fixed

- **[Vulkan/Linux] Thread safety for queue and command pool operations** — `vkAllocateCommandBuffers`, `vkFreeCommandBuffers`, `vkQueueSubmit`, `vkQueuePresentKHR`, and `vkQueueWaitIdle` are now guarded by `gpu_mutex` in `Device::createCommandEncoder()`, both `Device::submit()` overloads, `Device::waitForIdle()`, `CommandBuffer::~CommandBuffer()`, `Texture::upload()`, and `Texture::download()`.

- **[Vulkan] Double `vkAcquireNextImageKHR` per frame** — `CommandEncoderHandle` now tracks `swapchainImageAcquired`; `beginRenderPass()` skips the acquire on any call after the first within the same frame, preventing a hang when a frame encodes more than one render pass against the swapchain image.

- **[Vulkan] Offscreen `TextureView` use-after-free** — `RenderPassEncoderHandle` now holds a `shared_ptr<void> offscreenViewRef` that keeps the caller's `TextureView` alive for the duration of the pass. Previously, destroying the `TextureView` on the CPU side while `vkCmdBeginRenderingKHR` still held the raw `VkImageView` handle triggered a validation error.

- **[Vulkan] Offscreen image layout after render pass** — `RenderPassEncoder::end()` now transitions offscreen images to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` (was `VK_IMAGE_LAYOUT_GENERAL`), matching the layout expected by `createBindGroup()` for subsequent texture sampling. The pipeline barrier destination stage is narrowed to `VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT`.

- **[Vulkan/Linux] Dynamic rendering broken on Intel hasvk driver** — GPUs served by `libvulkan_intel_hasvk.so` (Gen8 BSW/HSW/BYT, Gen8LP CHV, Gen8 BDW) advertise Vulkan 1.3 but their dynamic rendering implementation is unreliable in practice. `createDevice()` now detects these devices by name and forces the traditional `vkCmdBeginRenderPass` / `vkCmdEndRenderPass` path regardless of API version.

- **[Vulkan] Swapchain format preference** — the format selection loop now prefers `VK_FORMAT_B8G8R8A8_UNORM` / `VK_FORMAT_R8G8B8A8_UNORM` over the previous sRGB preference, consistent with the Metal backend and the UNORM pixel format used by campello_widgets for offscreen textures.

- **[Vulkan] `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` on color render targets** — `TextureUsage::renderTarget` no longer adds `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` to color-format images. The flag is only valid for depth/stencil formats; including it on a color image caused `vkCreateImage` to fail on strict drivers.

- **[Vulkan] Vertex input bindings and attributes silently ignored** — `Device::createRenderPipeline()` always passed `vertexBindingDescriptionCount = 0` and `vertexAttributeDescriptionCount = 0`, making vertex buffer data invisible to shaders. The descriptor now builds `VkVertexInputBindingDescription` and `VkVertexInputAttributeDescription` arrays from `RenderPipelineDescriptor::vertex.buffers`.

## [0.17.0] - 2026-06-30

### Added

- **[Linux/Wayland] `LinuxSurfaceInfo::width` / `height`** — new fields on `LinuxSurfaceInfo` that carry the desired framebuffer size in pixels. Required for Wayland, where the compositor always reports `VkSurfaceCapabilitiesKHR::currentExtent = {UINT32_MAX, UINT32_MAX}` ("choose any size within min/max bounds") instead of a concrete extent. Both initial swapchain creation and `recreateSwapchain()` now read these fields and clamp the result to `[minImageExtent, maxImageExtent]`.

- **[Linux/Wayland] `campello_gpu_wayland_resize(uint32_t w, uint32_t h)`** — new `extern "C"` function (compiled in when `CAMPELLO_GPU_WAYLAND` is defined) that lets a Wayland runner (e.g. `campello_widgets`) trigger a swapchain resize between frames without destroying the `VkDevice`. It patches `DeviceData::imageExtent` and calls `recreateSwapchain()` directly. A file-scope `g_wayland_device` pointer is set on device creation and cleared in `~Device()`.

### Fixed

- **[Android/Vulkan] Build error: `LinuxSurfaceInfo` undeclared on Android** — swapchain extent resolution at `device.cpp:726` cast `pd` to `LinuxSurfaceInfo*` without an `#ifndef __ANDROID__` guard, causing a compile error when building for Android (where `linux_surface.hpp` is not included). The block is now guarded; Android falls back to `minImageExtent` if `currentExtent.width == UINT32_MAX` (which should not occur in practice given that `ANativeWindow` always reports a concrete size).

- **[Vulkan] Wayland swapchain extent** — `createDevice()` and `recreateSwapchain()` both previously used `surfaceCapabilities.currentExtent` unconditionally. On Wayland that value is always `{UINT32_MAX, UINT32_MAX}`, causing the swapchain to be created with a 4 GiB × 4 GiB extent. Both paths now detect `currentExtent.width == UINT32_MAX` and resolve the extent from caller-supplied dimensions (clamped to the surface's `min/maxImageExtent`).

- **[Vulkan] `createDevice()` surface/swapchain/device leaks on error paths** — `VkSurfaceKHR`, `VkSwapchainKHR`, and `VkDevice` were not destroyed when `createDevice()` returned `nullptr` early due to capability query failures, no suitable queue family, `vkCreateDevice` failure, `vkCreateSwapchainKHR` failure, or `vkGetSwapchainImagesKHR` failure. All early-return paths now call the appropriate `vkDestroySurfaceKHR` / `vkDestroySwapchainKHR` / `vkDestroyDevice` before returning.

## [0.16.0] - 2026-06-28

### Added

- **Vulkan validation layer support** — new `CAMPELLO_GPU_VALIDATION` CMake option (default `OFF`)
  - When enabled, registers `VK_LAYER_KHRONOS_validation` at instance creation and sets up a
    `VkDebugUtilsMessengerEXT` that prints `[Vulkan ERROR/WARNING/INFO/VERBOSE]` to stderr on
    Linux and to logcat on Android
  - `examples/linux/run.sh --validation` (or `-v`) enables the flag automatically for the Linux
    example build
  - Install on Ubuntu/Debian: `sudo apt install vulkan-validationlayers`

- **Linux example launch script** — `examples/linux/run.sh`
  - Builds the project with `cmake`/`make` and immediately executes the Linux example
  - Accepts `--validation` / `-v` to enable Vulkan validation layers for that run

### Fixed

- **[Vulkan] Swapchain `minImageCount=0` hang** — on hardware where
  `VkSurfaceCapabilitiesKHR::maxImageCount == 0` (meaning "no maximum" per the Vulkan spec),
  the formula `std::min(std::max(minImageCount, 3), maxImageCount)` silently clamped the
  requested image count to zero.  A zero-image swapchain caused `vkAcquireNextImageKHR` to
  never signal its semaphore, making `vkQueueSubmit` appear to hang and
  `vkQueuePresentKHR` to segfault.  Fixed in both `createDevice` and `recreateSwapchain` by
  guarding the upper clamp: `if (maxImageCount > 0) count = std::min(count, maxImageCount);`
  Discovered via the new validation layer flag (`vkCreateSwapchainKHR: minImageCount is 0`).

- **[Vulkan] `waitStage` dangling pointer in `Device::submit()`** — `VkPipelineStageFlags
  waitStage` was declared inside the `if (cbHandle->hasSwapchain)` block while
  `pWaitDstStageMask` pointed to it; the pointer dangled by the time `vkQueueSubmit` read it.
  `waitStage` is now declared in the outer function scope in both `submit` overloads.

- **[Vulkan] `recreateSwapchain` hardcoded `compositeAlpha`** — `compositeAlpha` was
  unconditionally set to `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`; on drivers that only support
  `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` this caused swapchain creation to fail.  Fixed by
  probing `caps.supportedCompositeAlpha` and selecting the first supported mode (preferring
  OPAQUE → INHERIT → PRE_MULTIPLIED → POST_MULTIPLIED).

- **[Vulkan] `deviceData->surfaceFormat` always `VK_FORMAT_UNDEFINED`** — in `createDevice`,
  the inner `VkSurfaceFormatKHR chosenFormat` declaration inside the swapchain creation block
  shadowed the outer variable, so `deviceData->surfaceFormat` was never populated.
  `createRenderPipeline` on the dynamic-rendering path passed `VK_FORMAT_UNDEFINED` to
  `VkPipelineRenderingCreateInfo::pColorAttachmentFormats`, causing every draw call to be
  silently rejected by the driver.  Fixed by removing the inner type declaration so the outer
  `chosenFormat` is correctly assigned.  Discovered via Vulkan validation layers
  (`VUID-vkCmdDraw-pColorAttachments-08963`).

- **[Linux example] `vkDestroyShaderModule: Invalid device` crash on exit** — `pipelineDesc`
  held `shared_ptr<ShaderModule>` references (via `vertex.module` and `fragment->module`) until
  end-of-main, keeping the shader module alive past `shaderModule.reset()`.  `device.reset()`
  then destroyed the `VkDevice` first, and the deferred module destruction triggered a Vulkan
  Loader abort.  Fixed by scoping `pipelineDesc` inside a block that ends immediately after
  `createRenderPipeline` returns, releasing the extra references before teardown.

- **[Linux example] Colored triangle rendering** — added triangle rendering to the Linux
  example using the same pre-compiled SPIR-V shader as the Android example (embedded in
  `examples/linux/triangle_shader.h`), confirming the dynamic-rendering pipeline path works
  correctly on Linux/Intel.

## [0.15.0] - 2026-06-26

### Added

- **Linux/Vulkan CI & runtime robustness**
  - Vulkan 1.3 core dynamic-rendering entry points are now loaded when available, with
    `VK_KHR_dynamic_rendering` as a fallback.
  - `VK_KHR_surface` is no longer required at instance creation, enabling headless
    Linux/Vulkan contexts.
  - `VK_KHR_portability_enumeration` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`
    are enabled when available, improving compatibility with portability ICDs such as
    Mesa lavapipe and MoltenVK.
  - Compute pipelines now create an internal empty pipeline layout when the caller passes
    `nullptr`, matching the existing render-pipeline behavior.

### Fixed

- **[Linux] Example build** — `examples/linux/main.cpp` included non-existent
  `campello_gpu/constants/load_op.hpp` and `campello_gpu/constants/store_op.hpp`.
  Those includes were removed; the enums are already available via
  `begin_render_pass_descriptor.hpp`.
- **[Linux] `CommandEncoder::generateMipmaps()` missing return** — returned an
  indeterminate value on success; now returns `true`.
- **[Linux] `Device::createComputePipeline()` null-deref** — crashed when the compute
  shader module or pipeline layout was `nullptr`; now returns `nullptr` safely and
  cleans up any internally created layout.
- **[Linux] `RenderPassEncoder::beginRenderPass()` with no attachments** — segfaulted
  when no color attachment was provided; now creates a valid no-attachment pass.
- **[Linux] Draw/dispatch without a bound pipeline** — `draw`, `drawIndexed`,
  `drawIndirect`, `drawIndexedIndirect`, `dispatchWorkgroups`, and
  `dispatchWorkgroupsIndirect` now no-op instead of crashing the driver when no
  pipeline is bound.
- **[Linux] Offscreen render-target layout-transition crash** — a pipeline barrier after
  a no-draw dynamic-rendering pass crashed some Mesa Intel drivers; the barrier is
  skipped when no pipeline/draws were bound.
- **[Linux] Depth/stencil usage on color render targets** — `renderTarget` usage no
  longer adds `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` for color formats.

## [0.14.0] - 2026-06-26

### Added

- **`ComputePipeline::getWorkgroupSize()`** — returns the threadgroup size a compute pipeline will
  use at dispatch time. On Metal this is the pipeline's `threadExecutionWidth()`; on Vulkan,
  DirectX 12, and WebGPU it currently returns `{1, 1, 1}` (those backends already respect the
  shader-declared local size, and callers can use this query to align their host-side dispatch
  math). No dispatch behavior changed.

### Fixed

- **Missing `<cstdint>` include in `compute_pipeline.hpp`** — `WorkgroupSize` uses `uint32_t` but
  the header did not include `<cstdint>`, causing a build failure on toolchains that do not pull
  it in transitively (e.g. Ubuntu 24.04 / GCC 13 in CI).

## [0.13.3] - 2026-06-25

### Fixed

- **[Metal] `Fence::wait()` returning immediately on a fresh fence** — `MetalFenceData::signaled` defaulted to `true`, so a freshly created fence's `wait()` never blocked on the submission it was passed to via `Device::submit(cmdBuffer, fence)`. `Device::submit()` now resets the fence to unsignaled right before `commit()`, mirroring the Vulkan backend's `vkResetFences()` call before each `vkQueueSubmit` — fixes the one-shot create→submit→wait usage documented as "typical usage" in `fence.hpp`, while staying safe for ring-buffer fence reuse.
- **[Metal] `Buffer::download()` reading stale data from `Managed` buffers** — the raw `memcpy` from `buffer->contents()` had no `synchronizeResource:` call first, which Apple's docs require before a CPU read can see a prior GPU write on a `MTLResourceStorageModeManaged` buffer (any GPU-written compute/storage buffer not created with `mapRead`/`mapWrite`). `download()` now encodes and waits on a blit-encoder `synchronizeResource:` before the `memcpy`, gated on the buffer's storage mode being `Managed` (no-op for `Shared` buffers/iOS, which are always coherent).

## [0.13.2] - 2026-06-20

### Added

- **`FrameTimeSampler::recordDuration()`** — records an explicitly-measured duration directly into the circular buffer, alongside the existing `record()` (which computes a delta between successive calls). Lets consumers track actual phase-work cost (e.g. build/layout/paint duration, GPU encode/submit duration) instead of only call-to-call cadence — used by `campello_widgets::Renderer`'s Flutter-style two-lane performance overlay.

## [0.13.1] - 2026-04-28

### Fixed

- **`Device::submit()` null `signalFence` crash** — all backends except WebGPU dereferenced `signalFence` without a null check when the caller passed `nullptr` (a legal optional parameter)
  - **Metal** — `addCompletedHandler` lambda now early-returns if `signalFence` is null or its `native` handle is null; prevents `EXC_BAD_ACCESS` when the completion handler fires on a background dispatch queue
  - **DirectX 12** — `queue->Signal()` is now skipped when `signalFence` is null
  - **Vulkan (Linux & Android)** — `vkResetFences` is guarded and `vkQueueSubmit` receives `VK_NULL_HANDLE` instead of dereferencing a null fence handle

## [0.13.0] - 2026-04-27

### Added

- **ASTC and ETC2 feature detection** — `Feature::astcTextureCompression` and `Feature::etc2TextureCompression` added to public API
  - **WebGPU** — queries `WGPUFeatureName_TextureCompressionETC2` / `ASTC` via `wgpuDeviceHasFeature`
  - **Metal** — reports ASTC+ETC2 on iOS; reports ASTC on Apple Silicon Macs (via `supportsBCTextureCompression` proxy)
  - **Vulkan (Linux & Android)** — queries `VkPhysicalDeviceFeatures::textureCompressionETC2` / `textureCompressionASTC_LDR`
  - **DirectX 12** — ETC2/ASTC unsupported on native DX12 (no change)
- **Compressed texture block helpers** — `isCompressedFormat()`, `getPixelFormatBlockDimensions()`, `getPixelFormatBlockBytes()` in `pixel_format.hpp`
  - All BC, ETC2, and ASTC variants correctly report block dimensions and byte sizes
- **WASM CI** — GitHub Actions job (`build-wasm`) compiles the WebGPU backend with Emscripten on every push/PR
- **Async callback APIs** for non-blocking GPU readback on WebGPU/WASM
  - `Buffer::downloadAsync(offset, length, data, callback)` — chains `wgpuQueueOnSubmittedWorkDone` → `wgpuBufferMapAsync` → user callback without `emscripten_sleep`
  - `Texture::downloadAsync(mipLevel, arrayLayer, data, length, callback)` — same async chain for texture-to-buffer readback
  - `CommandBuffer::getGPUExecutionTimeAsync(callback)` — async timestamp query readback
  - Non-WebGPU backends implement these as immediate callbacks (synchronous behavior, same-thread)

### Fixed

- **WebGPU `Device::waitForIdle()`** — now actually blocks until GPU work completes using `emscripten_sleep` polling (previously returned immediately, breaking synchronization)
- **WebGPU `copyBufferToTexture` / `copyTextureToBuffer`** — extent now uses mip-level dimensions instead of full texture dimensions (previously copied wrong size for `mipLevel > 0`)
- **WebGPU mipmap generation resource leak** — `initMipmapGenResources` now rolls back partial allocations on failure (previously leaked `vsModule` if `fsModule` failed, causing permanent mipmap generation failure)
- **WebGPU `calculateTextureSize`** — now uses block-based math for compressed formats (`ceil(w/blockW) * ceil(h/blockH) * blockBytes`) instead of `w * h * bpp`, fixing severe over/under-allocation for BC/ETC2/ASTC textures
- **WebGPU `Texture::upload` / `download` / `downloadAsync`** — now compute row lengths and image sizes in block rows for compressed formats; previously passed bit counts as byte counts, causing incorrect data transfers for all formats

### Changed

- **`CommandEncoder::generateMipmaps` returns `bool`** instead of `void`
  - Returns `true` when mip generation commands were successfully recorded
  - Returns `false` for error conditions: null texture, wrong type, wrong usage, unsupported format, resource creation failure, or no mip levels to generate
  - Updated in all backends: WebGPU, Metal, Vulkan (Linux & Android), DirectX 12

### Tests

- `GenerateMipmapsProducesCorrectContent` — pixel-correctness test: uploads solid red to an 8×8 RGBA8 texture, generates 4 mip levels, downloads each level and verifies they all remain red
- Fixed `GetPixelFormatSize.ETC2CompressedFormats` universal test — `etc2_rgb8a1unorm` is 4 bits/texel (was incorrectly expecting 8)

### Tests

- `Buffer.AsyncDownloadRoundtrip` — verifies async buffer download produces correct data
- `Texture.AsyncDownloadRoundtrip` — verifies async texture download produces correct data
- `CommandBuffer.GetGPUExecutionTimeAsyncWorksAfterSubmission` — verifies async timing callback fires without crash

## [0.12.0] - 2026-04-23

### Added

- **Mipmap generation** — `CommandEncoder::generateMipmaps(texture)`
  - **Metal** — uses native `MTL::BlitCommandEncoder::generateMipmaps()`
  - **Vulkan (Linux & Android)** — iterative `vkCmdBlitImage` with `VK_FILTER_LINEAR` from mip N-1 into mip N
  - **DirectX 12** — shader-based render pass using `D3DCompile`-time HLSL, fullscreen triangle pixel shader with bilinear sampling, per-format PSO caching on `DeviceData`
- **`CommandEncoder::copyTextureToTexture` per-mip support** — added `srcMipLevel` and `dstMipLevel` parameters to the public API
  - **DirectX 12** — previously a stub; now fully implemented via `CopyTextureRegion` with subresource indices
  - **Metal** — passes mip levels through to `MTL::BlitCommandEncoder::copyFromTexture`
  - **Vulkan (Linux & Android)** — `vkCmdCopyImage` now targets specific mip levels with per-level layout barriers

### Tests

- `CopyTextureToTextureMipLevels` — integration test that uploads to mip 0, copies mip 0 → mip 1, and verifies the result
- `GenerateMipmapsDoesNotCrash` — smoke test covering the new mipmap generation API

## [0.11.1] - 2026-04-23

### Fixed

- **CMake** `target_include_directories` guarded with `INTERFACE_LIBRARY` check to avoid errors on header-only alias targets
- **Linux Vulkan** `recreateSwapchain` moved into `systems::leal::campello_gpu` namespace so `command_encoder.cpp` can link against it
- **Linux Vulkan** added missing `#include <cstring>` in `buffer.cpp` and `texture.cpp`
- **Linux Vulkan** added missing forward declaration for `findMemoryType` in `device.cpp`
- **Linux Vulkan** added missing `default` cases to `getAddressMode`, `getCompareOp`, and `pixelFormatToNative` switch statements
- **Public API** `command_buffer.hpp` — added missing `#include <cstdint>`
- **Universal** `pixel_format.cpp` — added missing `default` cases to `getPixelFormatSize` and `pixelFormatToString` switch statements

## [0.11.0] - 2026-04-23

### Added

- **Linux windowed desktop support** — full X11 and Wayland swapchain integration
  - New public header `inc/campello_gpu/platform/linux_surface.hpp` with `LinuxSurfaceInfo` and `LinuxWindowApi` enum
  - `Device::createDevice(pd)` accepts `LinuxSurfaceInfo*` as the `pd` parameter
  - X11: `vkCreateXlibSurfaceKHR` loaded dynamically via `vkGetInstanceProcAddr`; no X11 link-time dependency
  - Wayland: `vkCreateWaylandSurfaceKHR` loaded dynamically via `vkGetInstanceProcAddr`; no Wayland link-time dependency
  - Instance extensions `VK_KHR_xlib_surface` and `VK_KHR_wayland_surface` enabled only when reported by the Vulkan loader (headless Linux remains fully functional)
  - Surface capabilities and formats queried before queue-family selection; `vkGetPhysicalDeviceSurfaceSupportKHR` used when a surface is present
  - `Device::getSwapchainTextureView()` now returns a `TextureView::fromNative()` wrapper around the active swapchain image view instead of `nullptr`
  - Swapchain image index cached in `DeviceData` and updated during `CommandEncoder::beginRenderPass()` after `vkAcquireNextImageKHR`

- **Window resize handling** — swapchain recreation on both reactive and proactive paths
  - `recreateSwapchain()` (previously `static` in `device.cpp`) now accessible from `command_encoder.cpp` via declaration in `common.hpp`
  - Reactive: `Device::submit()` already recreated the swapchain when `vkQueuePresentKHR` returned `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`
  - Proactive: `CommandEncoder::beginRenderPass()` now detects `VK_ERROR_OUT_OF_DATE_KHR` from `vkAcquireNextImageKHR`, calls `recreateSwapchain()`, and returns `nullptr` so the next frame starts fresh
  - `CommandEncoderHandle` gains a `DeviceData* deviceData` back-pointer (forward-declared to avoid header cycles) for shared state updates

- **Acceleration-structure descriptor binding** — `Device::createBindGroup()` now supports `AccelerationStructure` entries
  - Builds `VkWriteDescriptorSetAccelerationStructureKHR` and chains it through `VkWriteDescriptorSet::pNext`
  - Uses `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`
  - Required for ray-tracing descriptor sets that bind TLAS/BLAS resources

- **Linux windowed example** (`examples/linux/`)
  - Raw X11 example (`main.cpp`) that opens a 640×480 window and clears the swapchain to a rotating color
  - `examples/linux/CMakeLists.txt` — standalone or in-tree build; links `campello_gpu` + `X11`
  - `examples/linux/README.md` — build/run instructions and Wayland usage notes
  - Root `CMakeLists.txt` includes `examples/linux/` when `BUILD_EXAMPLES=ON` on Linux

### CI

- **New `build-linux-vulkan` job** in `.github/workflows/ci.yml`
  - Runs on `ubuntu-latest`
  - Installs `libvulkan-dev` and `mesa-vulkan-drivers`
  - Builds `libcampello_gpu.so` (not just universal tests)
  - Builds integration tests
  - Runs universal tests
  - Attempts to run integration tests via Mesa lavapipe (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`)

### Fixed

- **Vulkan** `device.cpp`: `chosenFormat` variable shadowing — the inner `VkSurfaceFormatKHR chosenFormat` inside the swapchain `if` block shadowed the outer variable, causing `deviceData->surfaceFormat` to always be initialized to `{}` even when a real surface existed; fixed by assigning to the outer variable

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
