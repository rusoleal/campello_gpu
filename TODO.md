# campello_gpu — Pending Tasks

## Bugs (breaking / incorrect behavior)

- [x] **[Metal]** `RenderPassEncoder` / `ComputePassEncoder` constructors — `renderCommandEncoder()` and `computeCommandEncoder()` return autoreleased objects; missing `retain()` in constructors caused EXC_BAD_ACCESS when autorelease pool drained after each frame
- [ ] **[Vulkan]** `Device::createBindGroupLayout()` — function has no `return` statement; will crash or fail to compile with warnings-as-errors (`device.cpp:1055–1092`)
- [ ] **[Vulkan]** `Device::createBindGroupLayout()` — `layoutBindings[a].descriptorCount` is commented out (uninitialized); `stageFlags` is never set (`device.cpp:1060–1075`)
- [ ] **[Vulkan]** `Device::getFeatures()` — casts `native` (`DeviceData*`) directly to `VkPhysicalDevice`; will crash at runtime (`device.cpp:589`)
- [ ] **[Vulkan]** `CommandEncoder::beginRenderPass()` — calls `vkCmdBeginRendering` but never constructs or returns a `RenderPassEncoder`; missing `return` statement (`command_encoder.cpp:108`)
- [ ] **[Vulkan]** `CommandEncoder::finish()` — empty body; missing `vkEndCommandBuffer` and return value (`command_encoder.cpp:133`)
- [ ] **[Vulkan]** `Texture::upload()` — only copies to the staging buffer, never issues `vkCmdCopyBufferToImage` to the actual VkImage (`texture.cpp`)
- [ ] **[Vulkan]** `Buffer::getLength()` — always returns `0`; `BufferHandle` doesn't store the allocation size (`buffer.cpp:25`)
- [ ] **[Vulkan]** `Device::createRenderPipeline()` — `VkPipelineDepthStencilStateCreateInfo depthStencil` is declared but never initialized or passed to the pipeline (`device.cpp:776`)
- [ ] **[Vulkan]** Swapchain format/color-space selection always picks `surfaceFormats[0]` without any preference logic (`device.cpp:361–362`)
- [ ] **[Vulkan]** Debug log strings `"pepe1"`, `"pepe2"`, `"pepe3"` left in production code (`device.cpp:254,261,268`)
- [ ] **[DirectX]** `device.cpp` implements stale API (`getDefaultDevice`, `getDevices`, old `createBuffer`/`createTexture` signatures) — entire file is out of sync with `device.hpp` and will not compile

## Missing implementations — Vulkan/Android

- [ ] `Device::createBindGroup()` — no implementation exists; will produce a linker error
- [ ] `CommandEncoder::beginComputePass()` — no implementation; no `ComputePassEncoder` source file at all
- [ ] `Texture::createView()` — no `TextureView` handle or implementation in `src/vulkan_android/`
- [ ] `Texture` getters (`getFormat`, `getWidth`, `getHeight`, `getMipLevelCount`, `getSampleCount`, `getUsage`, `getDimension`) — not implemented
- [ ] `Adapter::getFeatures()` — returns empty set; should query `VkPhysicalDeviceFeatures` from the stored physical device
- [ ] `Device::getName()` — returns hardcoded `"unknown"`; should query `VkPhysicalDeviceProperties::deviceName`
- [ ] `Device::getEngineVersion()` — returns hardcoded `"unknown"`
- [ ] `CommandEncoder::clearBuffer()` — empty body (no `vkCmdFillBuffer`)
- [ ] `CommandEncoder::copyBufferToBuffer()` — empty body (no `vkCmdCopyBuffer`)
- [ ] `CommandEncoder::copyBufferToTexture()` — empty body (no `vkCmdCopyBufferToImage`)
- [ ] `CommandEncoder::copyTextureToBuffer()` — empty body
- [ ] `CommandEncoder::copyTextureToTexture()` — empty body (no `vkCmdCopyImage`)
- [ ] `CommandEncoder::resolveQuerySet()` — empty body
- [ ] `CommandEncoder::writeTimestamp()` — empty body
- [ ] `RenderPassEncoder::draw()` — empty body (no `vkCmdDraw`)
- [ ] `RenderPassEncoder::drawIndexed()` — empty body (no `vkCmdDrawIndexed`)
- [ ] `RenderPassEncoder::drawIndirect()` — empty body
- [ ] `RenderPassEncoder::drawIndexedIndirect()` — empty body
- [ ] `RenderPassEncoder::end()` — empty body (no `vkCmdEndRendering`)
- [ ] `RenderPassEncoder::setPipeline()` — empty body (no `vkCmdBindPipeline`)
- [ ] `RenderPassEncoder::setVertexBuffer()` — empty body (no `vkCmdBindVertexBuffers`)
- [ ] `RenderPassEncoder::setIndexBuffer()` — empty body (no `vkCmdBindIndexBuffer`)
- [ ] `RenderPassEncoder::setViewport()` — empty body (no `vkCmdSetViewport`)
- [ ] `RenderPassEncoder::setScissorRect()` — empty body (no `vkCmdSetScissor`)
- [ ] `RenderPassEncoder::setStencilReference()` — empty body
- [ ] `RenderPassEncoder::beginOcclusionQuery()` / `endOcclusionQuery()` — empty bodies
- [ ] `RenderPassEncoder::setBindGroup()` — commented out in public header (`render_pass_encoder.hpp:24`)
- [ ] All `ComputePassEncoder` methods (`dispatchWorkgroups`, `dispatchWorkgroupsIndirect`, `end`, `setBindGroup`, `setPipeline`) — no source file
- [ ] Memory cleanup on `createBuffer` failure — `bufferHandle` is never destroyed when `vkAllocateMemory` fails (`device.cpp:569`)

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
- [x] `Device::getEngineVersion()` — returns `"0.2.0"`
- [x] `getVersion()` (free function in namespace) — returns `"0.2.0"`

## Missing implementations — DirectX/Windows

- [ ] Rewrite `src/directx/device.cpp` to match the current `device.hpp` API (replace all stale method signatures)
- [ ] `Device::getAdapters()` — not implemented
- [ ] `Device::createDevice()` — not implemented (D3D12 adapter/device creation)
- [ ] `Device::createBuffer()` — not implemented (D3D12 heap/resource)
- [ ] `Device::createTexture()` — not implemented
- [ ] `Device::createShaderModule()` — not implemented
- [ ] `Device::createRenderPipeline()` — not implemented (D3D12 PSO)
- [ ] `Device::createComputePipeline()` — not implemented
- [ ] `Device::createBindGroupLayout()` / `createBindGroup()` — not implemented (D3D12 descriptor heap/tables)
- [ ] `Device::createPipelineLayout()` — not implemented (root signature)
- [ ] `Device::createSampler()` — not implemented
- [ ] `Device::createQuerySet()` — not implemented
- [ ] `Device::createCommandEncoder()` — not implemented (command allocator/list)
- [ ] `CommandEncoder`, `RenderPassEncoder`, `ComputePassEncoder`, `CommandBuffer` — all missing
- [ ] All resource classes (`Buffer`, `Texture`, `TextureView`, `ShaderModule`, `RenderPipeline`, `ComputePipeline`, `BindGroup`, `BindGroupLayout`, `PipelineLayout`, `Sampler`, `QuerySet`) — all missing

## Build system

- [x] `ios.cmake` — created; mirrors `macos.cmake` (same Metal backend, CMake handles sysroot/arch)
- [x] `linux.cmake` — created; placeholder stub that builds `src/pi/` only so universal tests can configure and run
- [x] Add `src/pi/utils.cpp` to `macos.cmake`
- [ ] Add `src/pi/utils.cpp` to `windows.cmake` once DirectX `createBuffer` is wired up

## Public API / headers

- [ ] `RenderPassEncoder::setBindGroup()` — declared but commented out in `render_pass_encoder.hpp:24`
- [ ] `RenderPassEncoder::executeBundles()` — declared but commented out in `render_pass_encoder.hpp:25`
- [ ] `CommandEncoder::copyBufferToTexture/copyTextureToBuffer/copyTextureToTexture` — marked `// TODO` in `command_encoder.hpp:28–30`
- [ ] `State` class (`state.hpp`) — declared but completely empty; no methods
