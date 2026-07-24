#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
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

namespace systems::leal::campello_gpu {

    VkInstance getInstance();

    struct DeviceData {
        VkDevice                  device;
        VkQueue                   graphicsQueue;
        VkPhysicalDevice          physicalDevice;
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
        static constexpr uint32_t kFramesInFlight = 2;
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

        // Traditional render pass fallback (used when hasDynamicRendering == false)
        VkRenderPass               swapchainRenderPassClear = VK_NULL_HANDLE;
        VkRenderPass               swapchainRenderPassLoad  = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> swapchainFramebuffers;

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
