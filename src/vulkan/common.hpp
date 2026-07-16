#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <vulkan/vulkan.h>
#ifdef __ANDROID__
#include <android/native_window.h>
#include <android/log.h>
#define LOG_DEBUG(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) (void)0
#endif
#include <campello_gpu/metrics.hpp>

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
        VkSurfaceFormatKHR        surfaceFormat;
        VkCommandPool             commandPool;
        VkDescriptorPool          descriptorPool;
        VkSemaphore               imageAvailableSemaphore;
        VkSemaphore               renderFinishedSemaphore;
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
