#pragma once

#include <vector>
#include <atomic>
#include <vulkan/vulkan.h>
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
        uint32_t                  queueFamilyIndex     = 0;
        bool                      rayTracingEnabled    = false;
        
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

        // Current swapchain image index (valid after beginRenderPass acquires an image).
        uint32_t currentImageIndex = 0;
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

}
