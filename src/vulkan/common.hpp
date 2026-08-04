#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>
#ifdef __ANDROID__
#include <android/native_window.h>
#include <android/log.h>
#define LOG_DEBUG(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) (void)0
#endif
#include <campello_gpu/metrics.hpp>
#include <campello_gpu/command_buffer.hpp>
#if defined(__linux__) && !defined(__ANDROID__)
#include <campello_gpu/platform/linux_dmabuf.hpp>
#endif

namespace systems::leal::campello_gpu {

    /**
     * @brief Find a memory type matching the requested property flags.
     * @return The memory type index, or UINT32_MAX if no match is found.
     */
    inline uint32_t findMemoryTypeIndex(uint32_t typeFilter,
                                        const VkPhysicalDeviceMemoryProperties &memProperties,
                                        VkMemoryPropertyFlags properties)
    {
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1u << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    /**
     * @brief Picks `preferred` if the surface actually supports it, else
     * falls back to VK_PRESENT_MODE_FIFO_KHR (the only present mode every
     * Vulkan-conformant surface is required to support).
     *
     * Both Vulkan swapchain call sites request MAILBOX here as the
     * deliberate default — see the CHANGELOG entry this shipped alongside.
     * On a real device (Galaxy Tab S7 FE, Adreno 642L, traditional render
     * pass fallback) this removed the entire `vkAcquireNextImageKHR` /
     * "images in flight" fence wait cost (measured ~10ms/frame average,
     * down to ~0.2ms), because that wait is really just CPU time spent
     * blocked until the display has vsync'd — under FIFO, a frame that
     * finished rendering early still has to sit and wait for the display
     * to actually want it. MAILBOX lets the CPU/GPU move on immediately
     * once a frame is submitted, always keeping only the most recent
     * completed frame ready for the next vsync to pick up.
     *
     * This is NOT the classic "MAILBOX floods the GPU with wasted frames"
     * scenario, and doesn't carry that scenario's battery cost: that
     * pattern only happens with an unbounded render loop (nothing pacing
     * how often a new frame is requested). campello_widgets' own frame
     * requests are already vsync-gated by FrameScheduler/the platform's
     * choreographer callback (see run_app.mm/run_app.cpp), independent of
     * the swapchain's present mode — verified on-device: frame count
     * stayed ~60/sec (matching the display's own refresh rate) whether
     * FIFO or MAILBOX was active, never more. The remaining, smaller
     * caveat is GPU clock-scaling behavior: some mobile GPU drivers'
     * DVFS heuristics lean on a predictable vsync-anchored work rhythm to
     * downclock confidently between frames, and MAILBOX's not-blocked-on-
     * vsync submission timing can be less predictable for those
     * heuristics — a real, driver-dependent effect, not something this
     * repo has instrumented directly (no on-device power/clock-frequency
     * measurement here, just Vulkan-level timing).
     */
    inline VkPresentModeKHR choosePresentMode(
        VkPhysicalDevice gpu, VkSurfaceKHR surface, VkPresentModeKHR preferred)
    {
        if (preferred == VK_PRESENT_MODE_FIFO_KHR) return preferred;

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr);
        if (count == 0) return VK_PRESENT_MODE_FIFO_KHR;

        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, modes.data());
        for (auto m : modes) {
            if (m == preferred) return preferred;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkInstance getInstance();

    /**
     * @brief Key for caching the transient offscreen VkRenderPass objects
     * built by buildRenderPass() — see DeviceData::offscreenRenderPassCache's
     * doc comment for why this cache exists.
     *
     * buildRenderPass() is a pure function of these four values (no
     * image-specific state), so any two calls with the same key produce
     * functionally-identical render passes — safe to reuse indefinitely.
     */
    struct RenderPassKey {
        VkFormat            colorFormat;
        VkFormat            depthFormat;
        VkAttachmentLoadOp  colorLoadOp;
        VkImageLayout       colorFinalLayout;

        bool operator==(const RenderPassKey& other) const noexcept {
            return colorFormat == other.colorFormat &&
                   depthFormat == other.depthFormat &&
                   colorLoadOp == other.colorLoadOp &&
                   colorFinalLayout == other.colorFinalLayout;
        }
    };

    struct RenderPassKeyHash {
        size_t operator()(const RenderPassKey& k) const noexcept {
            size_t h = std::hash<int>{}((int)k.colorFormat);
            h = h * 31 + std::hash<int>{}((int)k.depthFormat);
            h = h * 31 + std::hash<int>{}((int)k.colorLoadOp);
            h = h * 31 + std::hash<int>{}((int)k.colorFinalLayout);
            return h;
        }
    };

    struct DeviceData {
        VkDevice                  device;
        VkQueue                   graphicsQueue;
        VkPhysicalDevice          physicalDevice;
        // Populated once at device creation (see createDefaultDevice()) —
        // a physical device's set of memory types/heaps and their property
        // flags is fixed for its lifetime, so there is nothing to gain by
        // re-querying vkGetPhysicalDeviceMemoryProperties() on every single
        // Device::createBuffer() call. Found costing real per-draw-call
        // overhead on Android/Adreno: vulkan_draw_backend.cpp calls
        // createBuffer() fresh for every draw's vertex buffer (no pooling,
        // unlike the Metal backend's UniformBufferPool — see
        // campello_widgets' TODO.md for that follow-up), so this query ran
        // once per draw call, not once per frame.
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkSurfaceKHR              surface;
        VkSwapchainKHR            swapchain;
        VkExtent2D                imageExtent;
        std::vector<VkImage>      swapchainImages;
        std::vector<VkImageView>  swapchainImageViews;
        // Tracks, per swapchain image, which in-flight-frame fence last
        // claimed it (VK_NULL_HANDLE = free / never used). The swapchain is
        // created with more images (see recreateSwapchain()'s minImageCount)
        // than kFramesInFlight ring slots, so vkAcquireNextImageKHR can hand
        // back an image that's still logically owned by an OLDER frame than
        // the one beginFrameRing() just confirmed done -- the ring slot's
        // own fence isn't sufficient by itself. See the wait against this
        // array right after acquire in CommandEncoder::beginRenderPass()
        // (command_encoder.cpp) for the actual synchronization; this is the
        // standard "images in flight" extension to the basic frames-in-
        // flight pattern.
        std::vector<VkFence>      imagesInFlight;
        VkSurfaceFormatKHR        surfaceFormat;
        VkCommandPool             commandPool;
        // Separate from commandPool so Texture::upload()/download() (which
        // can run on a background thread, e.g. ImageLoader's worker pool)
        // never touches the same VkCommandPool the main raster thread's
        // per-frame Device::createCommandEncoder() uses. VkCommandPool
        // requires external synchronization for every operation that
        // touches it, including recording into any command buffer
        // allocated from it -- sharing one pool across threads meant the
        // (mostly unlocked, single-threaded-by-design) main render
        // recording could race a texture upload's own recording, caught
        // via Vulkan's validation layer as
        // UNASSIGNED-Threading-MultipleThreads-Write. gpu_mutex still
        // serializes concurrent texture uploads/downloads against each
        // other (see their own locking), just no longer needs to also
        // reason about the main render path.
        VkCommandPool             uploadCommandPool;

        // Frames-in-flight ring. Mirrors the DirectX12 backend's
        // DeviceData::beginFrameRing() design (src/directx/common.hpp) —
        // see Device::submit()'s doc comment in device.cpp for the full
        // rationale. Vulkan requires distinct semaphores per in-flight
        // frame: reusing one while a previous submission that used it
        // hasn't been waited on is invalid, hence per-slot arrays instead
        // of the single pair this used to be.
        // 3 (not 2): with the swapchain's own minImageCount also forced to
        // 3 (see recreateSwapchain()'s doc comment), matching depths lets
        // the CPU get a full swapchain-image's worth of slack ahead of the
        // GPU before beginFrameRing() ever blocks it — found reducing the
        // real, measured CPU-side wait in CommandEncoder::beginRenderPass()
        // (the "images in flight" fence wait — see its doc comment) on a
        // real device (Galaxy Tab S7 FE, Adreno 642L, Vulkan 1.1 traditional
        // render pass path): that wait is coupled to how far the GPU has
        // fallen behind the CPU's submission rate, and one more ring slot
        // gives the GPU a full extra frame to catch up before the CPU is
        // forced to stop and wait for it, at the cost of one more frame of
        // input-to-display latency and one more generation's worth of
        // descriptor pools/fences/semaphores/retained command buffers.
        static constexpr uint32_t kFramesInFlight = 3;
        VkSemaphore imageAvailableSemaphores[kFramesInFlight] = {};
        // Unlike imageAvailableSemaphores (needed *before* acquire tells us
        // which image we got, so it must be indexed by an independent
        // rotating slot), renderFinishedSemaphores is signaled by submit()
        // and waited on by present() -- both AFTER the image index is
        // known -- so it's indexed by the actual acquired swapchain image
        // instead of the frame-ring slot. That sidesteps the whole "does
        // kFramesInFlight evenly divide/relate to the swapchain's image
        // count" question entirely: each image gets its own semaphore, so
        // there is no ring-slot/image mismatch left to reason about. Sized
        // to swapchainImages.size() and (re)created alongside it.
        std::vector<VkSemaphore> renderFinishedSemaphores;
        // Signaled by Device::submit() when generation i's GPU work
        // completes. Created already-signaled (VK_FENCE_CREATE_SIGNALED_BIT)
        // so the first use of each slot needs no wait.
        VkFence     inFlightFences[kFramesInFlight] = {};
        uint32_t    currentFrameGen = 0;
        // Keeps each generation's CommandBuffer -- and therefore the GPU
        // resources it references (command buffer, staging buffers,
        // transient framebuffers; see CommandBuffer's destructor) -- alive
        // until inFlightFences[gen] confirms the GPU has finished with it.
        // Releasing a CommandBuffer while the GPU may still be executing
        // from it is undefined behavior; this is what makes it safe for
        // submit() to no longer call vkQueueWaitIdle every frame.
        std::shared_ptr<CommandBuffer> genCommandBuffer[kFramesInFlight];
        // One pool per frames-in-flight ring slot, NOT a single shared
        // pool -- Device::createBindGroup() allocates a fresh descriptor
        // set on essentially every draw call (only cached text draws
        // reuse one), and BindGroup's destructor deliberately does not
        // free individual sets (they're only reclaimed by resetting the
        // whole pool). A single fixed-size pool that's never reset fills
        // up permanently within the first frame or two of any real
        // content, and every draw call after that silently no-ops (its
        // createBindGroup() call fails, checked and skipped -- no crash,
        // no validation error) -- exactly the "black screen, zero CPU,
        // zero errors" symptom this was found chasing. Indexed by
        // currentFrameGen in createBindGroup(); beginFrameRing() resets
        // descriptorPools[gen] once it confirms that generation's GPU
        // work is done, mirroring genCommandBuffer[gen]'s lifetime and
        // the DirectX12 backend's per-generation SRV heap slot recycling
        // (see its DeviceData::freeSrvSlots()/recycleSrvSlots()).
        VkDescriptorPool descriptorPools[kFramesInFlight] = {};

#ifdef __ANDROID__
        ANativeWindow            *window               = nullptr;
#endif
        uint32_t                  queueFamilyIndex     = 0;
        bool                      rayTracingEnabled    = false;
        bool                      hasDynamicRendering  = true;
        uint32_t                  currentImageIndex    = 0;

        // Cooperative matrix (VK_KHR_cooperative_matrix): whether the extension was
        // enabled at device creation, and the (MSize,NSize,KSize,type) tuples the
        // physical device actually supports — queried once at device creation via
        // vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR since it is not a bare
        // yes/no capability. Not yet exposed through a public API.
        bool cooperativeMatrixEnabled = false;
        std::vector<VkCooperativeMatrixPropertiesKHR> cooperativeMatrixProperties;

        // shaderFloat16 (VK_KHR_shader_float16_int8 / core 1.2): whether the
        // VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16 feature was
        // actually enabled at device creation. Cached so Device::getFeatures()
        // reflects what was enabled, not just what the hardware could support.
        bool fp16Enabled = false;

#if defined(__linux__) && !defined(__ANDROID__)
        // Whether VK_KHR_external_memory_fd + VK_EXT_external_memory_dma_buf +
        // VK_EXT_image_drm_format_modifier were all enabled at device creation
        // -- see createDevice()'s dmaBufImportSupported. Gates both
        // Device::createTextureFromDmaBuf() and
        // Device::getSupportedDmaBufModifiers(): querying modifier support via
        // vkGetPhysicalDeviceFormatProperties2()'s VkDrmFormatModifierPropertiesListEXT
        // chain without the extension actually enabled is relying on
        // unspecified driver leniency, not something to build correctness on.
        bool dmaBufImportEnabled = false;

        // VK_EXT_physical_device_drm: which /dev/dri node this physical
        // device corresponds to. Queried once at device creation (fixed for
        // the physical device's lifetime, same reasoning as memoryProperties
        // above) -- see Device::getDrmDeviceNode().
        DrmDeviceNode drmDeviceNode;
#endif

        // Traditional render pass fallback (used when hasDynamicRendering == false)
        VkRenderPass               swapchainRenderPassClear = VK_NULL_HANDLE;
        VkRenderPass               swapchainRenderPassLoad  = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> swapchainFramebuffers;

        // Cache for buildRenderPass()'s output, keyed by its (pure-function)
        // input parameters — see RenderPassKey's doc comment. Without this,
        // command_encoder.cpp's offscreen/depth beginRenderPass() path
        // called vkCreateRenderPass() fresh on every single offscreen
        // composite (ClipRRect/ClipOval/ShaderMask/BoxShadow — everything
        // that isn't the traditional render pass fallback's swapchain
        // path, which already reuses swapchainRenderPassClear/Load). Found
        // costing real per-frame time on Android/Adreno (a tile-based GPU,
        // where render pass creation encodes tile/GMEM configuration, not
        // just a lightweight descriptor object): with ~15-20 offscreen
        // composites in a typical UI frame (campello_widgets' gallery app),
        // this was ~15-20 redundant vkCreateRenderPass calls/frame despite
        // only 1-2 *distinct* (format, loadOp) combinations ever actually
        // occurring — the parameters are pure, so every one of those calls
        // after the first was rebuilding an object functionally identical
        // to one already cached here. These render passes reference no
        // image/view state, so unlike swapchainFramebuffers (recreated on
        // resize) or a texture's own transient framebuffer, entries here
        // never need invalidating — only torn down at device destruction
        // (see Device::~Device()).
        std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> offscreenRenderPassCache;

        // Resource counters
        std::atomic<uint32_t> bufferCount{0};
        std::atomic<uint32_t> textureCount{0};
        std::atomic<uint32_t> renderPipelineCount{0};
        std::atomic<uint32_t> computePipelineCount{0};
        std::atomic<uint32_t> rayTracingPipelineCount{0};
        std::atomic<uint32_t> accelerationStructureCount{0};
        std::atomic<uint32_t> shaderModuleCount{0};
        std::atomic<uint32_t> samplerCount{0};
        std::atomic<uint32_t> bindGroupCount{0};
        std::atomic<uint32_t> bindGroupLayoutCount{0};
        std::atomic<uint32_t> pipelineLayoutCount{0};
        std::atomic<uint32_t> querySetCount{0};

        // Command stats
        std::atomic<uint64_t> commandsSubmitted{0};
        std::atomic<uint64_t> renderPasses{0};
        std::atomic<uint64_t> computePasses{0};
        std::atomic<uint64_t> rayTracingPasses{0};
        std::atomic<uint64_t> drawCalls{0};
        std::atomic<uint64_t> dispatchCalls{0};
        std::atomic<uint64_t> traceRaysCalls{0};
        std::atomic<uint64_t> copies{0};

        // Phase 2: Resource memory tracking (bytes)
        std::atomic<uint64_t> bufferBytes{0};
        std::atomic<uint64_t> textureBytes{0};
        std::atomic<uint64_t> accelerationStructureBytes{0};
        std::atomic<uint64_t> shaderModuleBytes{0};
        std::atomic<uint64_t> querySetBytes{0};

        // Phase 2: Peak memory tracking
        std::atomic<uint64_t> peakBufferBytes{0};
        std::atomic<uint64_t> peakTextureBytes{0};
        std::atomic<uint64_t> peakAccelerationStructureBytes{0};
        std::atomic<uint64_t> peakTotalBytes{0};

        // Phase 3: GPU pass timing (nanoseconds)
        std::atomic<uint64_t> renderPassTimeNs{0};
        std::atomic<uint64_t> computePassTimeNs{0};
        std::atomic<uint64_t> rayTracingPassTimeNs{0};
        std::atomic<uint32_t> renderPassSampleCount{0};
        std::atomic<uint32_t> computePassSampleCount{0};
        std::atomic<uint32_t> rayTracingPassSampleCount{0};

        // Phase 3: Memory budget and pressure management
        MemoryBudget memoryBudget;
        MemoryPressureCallback memoryPressureCallback;
        std::atomic<MemoryPressureLevel> lastPressureLevel{MemoryPressureLevel::Normal};

        // GPU timestamp query support
        VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
        float timestampPeriod = 1.0f;  // From VkPhysicalDeviceLimits, in nanoseconds per tick

        // Serializes access to commandPool and graphicsQueue across threads.
        // VkCommandPool and VkQueue are not thread-safe; all allocate/free/submit
        // calls must be guarded by this mutex.
        std::mutex gpu_mutex;

        // Advances the frames-in-flight ring and waits only for the ONE
        // slot about to be reused -- the generation that was `kFramesInFlight`
        // submits ago -- instead of Device::submit()'s old vkQueueWaitIdle,
        // which waited for literally everything on the queue every frame.
        // Must run before any GPU work for the new frame is recorded (called
        // from Device::createCommandEncoder()).
        //
        // Giving the CPU kFramesInFlight-1 frame(s) of slack this way is
        // what keeps a transient GPU-side slowdown (driver/compositor
        // contention, thermal throttling, ...) from stalling the thread
        // that's also servicing input/vsync -- see VulkanDrawBackend's
        // callers for why that coupling matters. The inFlightFences are
        // created already-signaled, so a slot's first-ever use waits
        // instantly instead of needing a "never used" sentinel.
        //
        // Bounded rather than UINT64_MAX: an indefinite wait here (or in
        // the acquire/images-in-flight waits around it — see
        // CommandEncoder::beginRenderPass() in command_encoder.cpp) turns
        // any GPU/driver/compositor-side stall into a permanent, zero-CPU
        // hang with no way back, which is worse than dropping a frame.
        // kWaitTimeoutNs bounds all three so a stuck frame degrades to
        // "skip this frame, try again next vsync" instead. Returns
        // kInvalidGen on timeout.
        static constexpr uint64_t kWaitTimeoutNs = 3'000'000'000ULL; // 3s
        static constexpr uint32_t kInvalidGen = UINT32_MAX;
        uint32_t beginFrameRing() {
            uint32_t nextGen = (currentFrameGen + 1) % kFramesInFlight;
            if (inFlightFences[nextGen] != VK_NULL_HANDLE) {
                VkResult waitResult = vkWaitForFences(device, 1, &inFlightFences[nextGen],
                                                       VK_TRUE, kWaitTimeoutNs);
                if (waitResult != VK_SUCCESS) {
                    return kInvalidGen;
                }
                // Deliberately NOT resetting inFlightFences[nextGen] here.
                // The swapchain can have more images than kFramesInFlight
                // ring slots (e.g. 4 images, 2 slots), so some
                // imagesInFlight[imageIndex] entry from a couple of frames
                // ago may alias this exact fence object. beginRenderPass()
                // (command_encoder.cpp) needs to observe it still in the
                // SIGNALED state — proven true by the wait just above — to
                // recognize that stale entry as already-safe rather than
                // waiting on it again. Resetting it here would destroy that
                // evidence before beginRenderPass() ever gets to check it,
                // turning a provably-safe image into a fence wait that can
                // never be satisfied (nothing will signal this generation's
                // fence again until Device::submit() runs for THIS frame).
                // Device::submit() resets it immediately before the
                // vkQueueSubmit call that will next signal it — see its
                // comment there.
            }
            // The GPU is now confirmed done with whatever this slot last
            // submitted -- safe to release (frees the command buffer and
            // any staging buffers/transient framebuffers it was keeping
            // alive; see CommandBuffer's destructor).
            genCommandBuffer[nextGen].reset();
            // Reclaim every descriptor set this slot's last frame
            // allocated — see descriptorPools' doc comment.
            if (descriptorPools[nextGen] != VK_NULL_HANDLE) {
                vkResetDescriptorPool(device, descriptorPools[nextGen], 0);
            }
            currentFrameGen = nextGen;
            return nextGen;
        }
    };

    /**
     * @brief Per-Fence Vulkan state.
     */
    struct VulkanFenceData {
        VkDevice device       = VK_NULL_HANDLE;
        VkFence  fence        = VK_NULL_HANDLE;
    };

    // Swapchain recreation helper (defined in device.cpp, used by command_encoder.cpp).
    void recreateSwapchain(DeviceData *deviceData);

} // namespace systems::leal::campello_gpu

// Render-pass builder helper (defined in device.cpp, used by command_encoder.cpp).
// Builds a single-subpass VkRenderPass with one color attachment and optional depth.
VkRenderPass buildRenderPass(VkDevice device,
                              VkFormat colorFormat,
                              VkFormat depthFormat,
                              VkAttachmentLoadOp colorLoadOp,
                              VkImageLayout colorFinalLayout);
