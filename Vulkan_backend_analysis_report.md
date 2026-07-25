# Vulkan backend analysis — campello_gpu

**Scope:** static code review of the Vulkan backend (`src/vulkan/*`, `inc/campello_gpu/*`) focused on frame-time regressions on Android.  
**Note:** This is a source-level analysis; no profiler or GPU capture was run. The most impactful items should be confirmed with a real trace (RenderDoc, AGI, systrace) on the affected devices.

---

## 1. Executive summary

The backend has recently been improved in the right areas (frames-in-flight ring, per-swapchain-image semaphores, per-frame descriptor pools), but it still contains several patterns that will directly increase frame times on Android:

1. **Every GPU-readable buffer lives in host-visible/coherent memory** — the GPU reads vertex/index/uniform/storage data over the system bus on most Android SoCs.
2. **Descriptor sets are allocated and fully written on essentially every draw call** — high CPU overhead and cache churn when draw-count grows.
3. **A single `gpu_mutex` serialises almost all GPU work** — command-buffer allocation, queue submit, texture upload/download and destruction all contend on one lock.
4. **Texture upload/download is synchronous and holds that lock while waiting on the GPU** — background image loading can easily stall the raster thread.
5. **No pipeline cache, no shader/sampler/layout caches** — runtime shader/pipeline creation will cause hitches.
6. **Very conservative pipeline barriers** (`ALL_COMMANDS`, `MEMORY_READ/WRITE`) — serialize the GPU pipeline and hurt tiler performance.

Below is the detailed breakdown with file/line references and recommended fixes.

---

## 2. Critical issues (fix first)

### 2.1 All buffers use `HOST_VISIBLE | HOST_COHERENT` memory

**Location:** `src/vulkan/device.cpp:1288`

```cpp
auto memoryType = findMemoryType(
    bufferRequirements.memoryTypeBits,
    memProperties,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
```

**Impact:**  
Every vertex buffer, index buffer, uniform buffer, storage buffer and indirect buffer is allocated from host-visible memory. On the vast majority of Android devices the GPU has a discrete/non-cache-coherent path to host memory, so the GPU shader/vertex units read render data across the system bus. This is one of the most common causes of “bigger GPU times” on Vulkan mobile.

**What to do:**
- Allocate **device-local** memory (`DEVICE_LOCAL_BIT`) for resources that are only read by the GPU (vertex, index, static uniform/storage, indirect).
- Use a small pool of **persistently mapped staging buffers** in host-visible memory for CPU→GPU uploads.
- For dynamic per-frame uniforms, use either:
  - one large device-local ring buffer updated via staging, or
  - `HOST_VISIBLE | HOST_COHERENT` *only* for that specific dynamic buffer type.
- Implement a memory-suballocation strategy (VMA or a custom buddy/block allocator) so that many small buffers do not each call `vkAllocateMemory`.

---

### 2.2 Descriptor set allocation + full write on every `createBindGroup`

**Locations:** `src/vulkan/device.cpp:2390-2479`

`Device::createBindGroup` allocates a fresh `VkDescriptorSet` from the current frame’s descriptor pool and then calls `vkUpdateDescriptorSets` for every entry. The comments in `common.hpp:95-110` explicitly note that *“Device::createBindGroup() allocates a fresh descriptor set on essentially every draw call.”*

**Impact:**  
CPU cost scales linearly with draw-call count. On a busy UI with 150+ draws this becomes a dominant frame cost. It also creates a lot of small descriptor writes that trash the CPU cache.

**What to do:**
- **Cache descriptor sets.** Key the cache by `(BindGroupLayout, resource handles/offsets)`. Reuse sets across frames; only free them when the layout or resources change.
- Use **dynamic uniform buffer offsets** (`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`) with one large uniform buffer ring, so a single descriptor set can serve many draws.
- Consider **push descriptors** (`VK_KHR_push_descriptor`) for very small/frequent bind groups if the target Android devices support it.
- As an immediate mitigation, keep the per-frame pool but batch descriptor writes: accumulate all writes for a frame and issue one `vkUpdateDescriptorSets` call.

---

### 2.3 Single `gpu_mutex` serialises the entire GPU path

**Location:** `src/vulkan/common.hpp:196`

The mutex protects command-pool allocation/free, queue submit, and is also held for the whole texture upload/download sequence (`texture.cpp:65`, `texture.cpp:306`).

**Impact:**  
Even on a multi-core phone the GPU frontend is effectively single-threaded. A background texture upload that allocates, records, submits and then **waits on a fence** while holding `gpu_mutex` will block the main raster thread from allocating/submitting its own command buffer.

**What to do:**
- Split the locks:
  - One lock for **command-pool access** (or use one pool per thread).
  - One lock for **queue submit**.
  - Use Vulkan semaphores/fences for GPU-side ordering, not a CPU mutex around the whole operation.
- Move texture uploads to a **dedicated transfer queue** if the device exposes one, or at least to a separate command buffer submitted ahead of the frame with a semaphore dependency.
- Never wait on a fence while holding the submit/pool lock.

---

### 2.4 Texture upload/download is fully synchronous

**Locations:** `src/vulkan/texture.cpp:44-160` (upload), `src/vulkan/texture.cpp:256-401` (download)

Both paths:
1. acquire `gpu_mutex`,
2. allocate a one-shot command buffer,
3. submit,
4. `vkWaitForFences(..., UINT64_MAX)`,
5. free the command buffer,
6. release the mutex.

**Impact:**  
This blocks the calling thread until the GPU is done. If an image loader worker calls `Texture::upload`, the main thread may stall waiting for `gpu_mutex`. It also creates/destroys a fence per upload.

**What to do:**
- Use a **ring of staging buffers** and a **dedicated upload command buffer**.
- Submit uploads without waiting; track completion with a **timeline or binary fence**.
- Only wait when the CPU actually needs to reuse the staging buffer (ring buffer logic).
- Do **not** create a host-visible “texture buffer” that lives for the whole texture lifetime (see next item).

---

### 2.5 Every texture keeps a permanent host-visible staging buffer

**Location:** `src/vulkan/device.cpp:1095`

```cpp
auto buffer = createBuffer(bufferSize, BufferUsage::copySrc);
```

This host-visible buffer is stored in `TextureHandle::buffer` and lives until the texture is destroyed.

**Impact:**  
Wasted host-visible memory. Combined with 2.1, this can exhaust the relatively small host-visible heap on Android and force the driver into slow fallback paths or OOM.

**What to do:**
- Remove the per-texture buffer.
- Use transient staging memory from a ring/pool at upload time only.

---

### 2.6 No `VkPipelineCache`

**Location:** none found in `src/vulkan/*`

`createRenderPipeline`, `createComputePipeline` and `createRayTracingPipeline` all pass `VK_NULL_HANDLE` as the pipeline cache.

**Impact:**  
Every runtime pipeline compile is a cold compile. On Android this is a classic source of frame hitches and long first-frame times.

**What to do:**
- Create one `VkPipelineCache` per `Device`.
- Persist it to disk (`vkGetPipelineCacheData` / `vkCreatePipelineCache` with initial data). Reload on app startup.
- Add a shader-module cache keyed by SPIR-V hash to avoid duplicate `vkCreateShaderModule` calls.
- Add caches for `VkSampler`, `VkDescriptorSetLayout`, `VkPipelineLayout` and `VkRenderPass` (traditional path) to remove redundant object creation.

---

## 3. High-impact GPU-side issues

### 3.1 Overly broad pipeline barriers

Many barriers use `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` and `VK_ACCESS_MEMORY_READ_BIT` / `VK_ACCESS_MEMORY_WRITE_BIT`. Examples:

| Location | Source stage | Issue |
|---|---|---|
| `command_encoder.cpp:232` (offscreen begin) | `ALL_COMMANDS` | full pipeline stall before color attachment |
| `command_encoder.cpp:516` (`copyBufferToTexture`) | `ALL_COMMANDS` | flush everything before a simple transfer |
| `command_encoder.cpp:577` (`copyTextureToBuffer`) | `ALL_COMMANDS` | same |
| `command_encoder.cpp:656` (`copyTextureToTexture`) | `ALL_COMMANDS` | same |
| `command_encoder.cpp:725,741` (`generateMipmaps`) | `ALL_COMMANDS` | per-mip full stalls |
| `texture.cpp:336,369` (download) | `ALL_COMMANDS` | same |

**Impact:**  
These barriers force the GPU to drain all prior work before continuing. On a tiled/mobile GPU this destroys parallelism and can easily double or triple GPU time for copy/blit-heavy frames.

**What to do:**
- Use the **narrowest stage and access masks** for the actual previous/next usage:
  - Buffer uploads: `TRANSFER_WRITE` → `VERTEX_SHADER_READ`, `FRAGMENT_SHADER_READ`, etc.
  - Image copies: `TRANSFER` stages with `TRANSFER_READ/WRITE`.
  - Render-pass transitions: `COLOR_ATTACHMENT_OUTPUT` / `EARLY_FRAGMENT_TESTS`.
- In `generateMipmaps`, batch layout transitions:
  - One barrier to transition all source mips to `TRANSFER_SRC_OPTIMAL`.
  - One barrier to transition all destination mips to `TRANSFER_DST_OPTIMAL`.
  - Issue all blits.
  - One final barrier to `SHADER_READ_ONLY_OPTIMAL`.

---

### 3.2 Image layout tracking is per-texture, not per-subresource

**Location:** `src/vulkan/texture_handle.hpp:23`

```cpp
VkImageLayout currentLayout;
```

The code transitions individual mips in `copyBufferToTexture`, `generateMipmaps`, etc., but only stores one layout for the whole image.

**Impact:**  
After a partial transition the tracked layout is wrong, which can cause:
- unnecessary transitions,
- validation errors,
- and in the worst case the driver doing extra layout work every frame.

**What to do:**
- Track layout per `(aspect, mip, layer)`, or at least per mip.
- Alternatively, always transition the whole image consistently and use `GENERAL` only when truly needed.

---

### 3.3 Offscreen render passes always transition from `UNDEFINED`

**Location:** `src/vulkan/command_encoder.cpp:224-237`

```cpp
barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
```

This is done for every offscreen `beginRenderPass`, regardless of the caller’s `loadOp`.

**Impact:**  
When the caller wants `loadOp::load`, the transition from `UNDEFINED` tells the implementation that previous contents may be discarded. The contents may survive, but it is undefined behaviour and can force the driver to drop compressed/tiled data, hurting performance and correctness.

**What to do:**
- Use the texture’s actual tracked layout as `oldLayout`.
- Set `srcAccessMask` to `VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT` (or `SHADER_READ_BIT` if the previous use was sampling) and `srcStageMask` to `COLOR_ATTACHMENT_OUTPUT`/`FRAGMENT_SHADER`.

---

## 4. CPU-side / API-usage issues

### 4.1 Dynamic uniform offsets used with non-dynamic descriptor type

**Location:** `src/vulkan/device.cpp:2435`

```cpp
write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
```

But `RenderPassEncoder::setBindGroup` and `ComputePassEncoder::setBindGroup` pass `dynamicOffsets` straight to `vkCmdBindDescriptorSets` (`render_pass_encoder.cpp:135`, `compute_pass_encoder.cpp:69`).

**Impact:**  
For `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` the driver should ignore dynamic offsets, but this is fragile and will trigger validation errors. If the app relies on dynamic offsets they will silently fail.

**What to do:**
- If the layout entry is dynamic, create the descriptor with `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`.
- Use one large uniform ring buffer and bind it once per frame.

---

### 4.2 Render pipeline only declares one color attachment format

**Location:** `src/vulkan/device.cpp:2007-2008`

```cpp
pipelineRenderingCreateInfo.colorAttachmentCount = 1;
pipelineRenderingCreateInfo.pColorAttachmentFormats = &deviceData->surfaceFormat.format;
```

**Impact:**  
If a render pipeline is created for multiple render targets, the pipeline is incompatible with the actual dynamic-rendering pass that uses more attachments, leading to either validation errors or the driver recompiling/fixing pipelines at draw time.

**What to do:**
- Build the `VkPipelineRenderingCreateInfo` from the `RenderPipelineDescriptor`’s actual fragment targets, not from the swapchain format.

---

### 4.3 Sampler anisotropy is always enabled

**Location:** `src/vulkan/device.cpp:2221-2222`

```cpp
info.anisotropyEnable = true;
info.maxAnisotropy = (float)descriptor.maxAnisotropy;
```

**Impact:**  
If `descriptor.maxAnisotropy` is `0`, sampler creation is invalid (`maxAnisotropy` must be ≥ 1). Even when valid, forced anisotropy increases texture-filtering cost on every sample.

**What to do:**
- Only set `anisotropyEnable = VK_TRUE` if:
  - `descriptor.maxAnisotropy > 1`,
  - `VkPhysicalDeviceFeatures::samplerAnisotropy` is supported, and
  - the value is ≤ `limits.maxSamplerAnisotropy`.

---

### 4.4 No memory budget extension

**Location:** `src/vulkan/device.cpp:1468-1491`

`getMemoryInfo()` sums heap sizes but reports `currentAllocatedSize = 0` and does not use `VK_EXT_memory_budget`.

**Impact:**  
On Android, memory pressure handling is based on a rough percentage of total heap, not the driver’s real budget. This can miss transient OOM situations on devices with aggressive memory reclaim.

**What to do:**
- Enable `VK_EXT_memory_budget` when available.
- Use `VkPhysicalDeviceMemoryBudgetPropertiesEXT` for `heapBudget` / `heapUsage`.

---

## 5. Android-specific observations

### 5.1 Swapchain configuration

**Locations:** `src/vulkan/device.cpp:858-892` and `src/vulkan/device.cpp:2518-2555`

- `presentMode` is hard-coded to `VK_PRESENT_MODE_FIFO_KHR`.
- `preTransform` is forced to `IDENTITY` when supported.

**Impact:**  
`FIFO` is correct for most shipped apps, but it adds latency. `MAILBOX` (when available) can reduce input latency at the cost of power. Forcing `IDENTITY` means the compositor does any required rotation, which is a deliberate correctness choice but costs compositor bandwidth on devices where the display natural orientation differs from the app.

**What to do:**
- Expose `presentMode` as a user/configurable option (FIFO / Mailbox / Immediate for benchmarks).
- Consider a “pre-rotation” mode where the app applies the rotation in its MVP and requests `currentTransform` for lower compositor cost, if the higher-level renderer can support it.

---

### 5.2 No async transfer/compute queues

The backend selects one queue family that supports present/graphics and uses it for everything.

**Impact:**  
Uploads, downloads, compute and graphics are all serialized on the same queue.

**What to do:**
- Select a dedicated transfer queue family when available and use it for texture/buffer uploads.
- Select a dedicated compute queue for heavy compute workloads.

---

## 6. Lower-impact items worth fixing

| Item | Location | Recommendation |
|---|---|---|
| Timestamp read blocks CPU | `src/vulkan/command_buffer.cpp:48` | Use `VK_QUERY_RESULT_64_BIT` **without** `VK_QUERY_RESULT_WAIT_BIT`, or read asynchronously. |
| Per-texture upload creates/destroys a fence | `src/vulkan/texture.cpp:148,156` | Use a fence pool / timeline semaphore. |
| `Buffer::upload` maps/unmaps every call | `src/vulkan/buffer.cpp:36-52` | Keep buffers persistently mapped where possible. |
| `CommandPool` lacks `VK_COMMAND_POOL_CREATE_TRANSIENT_BIT` | `src/vulkan/device.cpp:961` | Add it; the buffers are one-time-submit. |
| `waitForIdle` on queue, not device | `src/vulkan/device.cpp:2815` | Prefer `vkDeviceWaitIdle` if other queues exist. |
| Ray-tracing SBT fallback is silent zero-fill | `src/vulkan/device.cpp:3719-3738` | Add a staging-buffer upload path when direct mapping fails. |
| Shader modules compiled from raw SPIR-V with no cache | `src/vulkan/device.cpp:1685-1713` | Add a hash-keyed cache. |

---

## 7. Recommended order of attack

If the goal is to reduce Android frame times as quickly as possible, tackle them in this order:

1. **Move GPU-read buffers to device-local memory** (2.1) — often the biggest GPU-time win.
2. **Implement a `VkPipelineCache` persisted to disk** (2.6) — removes hitches from runtime pipeline creation.
3. **Fix the broad pipeline barriers** (3.1) — big GPU-side win, especially for mipmaps/copies.
4. **Make texture upload asynchronous and stop holding `gpu_mutex` while waiting** (2.4, 2.3) — removes main-thread stalls.
5. **Add descriptor-set caching and dynamic uniform buffers** (2.2) — removes CPU overhead as draw counts grow.
6. **Remove per-texture permanent staging buffers** (2.5) — saves memory.
7. **Track image layouts per-subresource** (3.2) and **fix offscreen `oldLayout = UNDEFINED`** (3.3).
8. **Add shader/sampler/layout caches** and **memory-budget support** (lower priority but easy wins).

---

## 8. TL;DR for the team

The Vulkan backend is functionally correct but still has a **mobile-unfriendly memory model** and **CPU-heavy binding model**. The two changes most likely to explain “bigger times in Android” are:

- **Buffers live in host-visible memory** → GPU reads over the bus.
- **Every draw allocates and writes a descriptor set** → CPU cost grows with scene complexity.
- **A single mutex and synchronous uploads** → main thread stalls on background work.

Fix those three plus add a pipeline cache and tighten pipeline barriers, and you should see a measurable improvement in both CPU and GPU frame times.
