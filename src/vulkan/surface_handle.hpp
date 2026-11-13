#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

#ifdef __ANDROID__
#include <android/native_window.h>
#endif

namespace systems::leal::campello_gpu {

    struct DeviceData;
    class TextureView;

    /**
     * @brief Per-Surface Vulkan state: one VkSurfaceKHR + its own swapchain.
     *
     * Completely independent from the device's legacy implicit swapchain
     * (DeviceData::swapchain, created from createDefaultDevice(pd)). Nothing
     * here touches DeviceData's swapchain fields or its frames-in-flight ring
     * semaphores, so apps that use the single-swapchain path behave exactly
     * as before.
     *
     * Synchronization, per frame:
     *   acquireNextTexture(): waits slotFence[slot] (the submission that last
     *     waited on acquireSemaphores[slot] has completed, so the semaphore
     *     can be reused), then vkAcquireNextImageKHR(acquireSemaphores[slot]).
     *   schedulePresent(cmd): moves the acquisition into the command buffer
     *     (CommandBufferHandle::surfacePresents).
     *   Device::submit(cmd): the real submission waits acquireSemaphores[slot]
     *     and signals renderFinished[image] + slotDone[slot]; a chained empty
     *     submission waits slotDone[slot] and signals slotFence[slot] (the
     *     same chaining Device::submit(cmd, fence) uses for genCompleteSemaphores,
     *     because the caller's own fence can't be used for our bookkeeping);
     *     then vkQueuePresentKHR waits renderFinished[image].
     *
     * renderFinished is per swapchain image (not per slot), for the same
     * reason as DeviceData::renderFinishedSemaphores: it's signaled and waited
     * after the image index is known, and an image can only be re-acquired once
     * the present that waited on it has been processed.
     *
     * Threading: one thread drives a surface (its window's raster thread).
     * `mutex` guards the swapchain/acquisition state against Device::submit()
     * marking it out of date and against destruction from another thread.
     * Queue access (submit, present, wait-idle) goes through DeviceData::gpu_mutex
     * as everywhere else in this backend; never take gpu_mutex and then `mutex`
     * in the opposite order of submit() (which takes gpu_mutex only).
     */
    struct SurfaceData {
        static constexpr uint32_t kSlots = 3;

        DeviceData*        deviceData = nullptr;
        VkDevice           device     = VK_NULL_HANDLE;
        VkSurfaceKHR       surface    = VK_NULL_HANDLE;
        VkSwapchainKHR     swapchain  = VK_NULL_HANDLE;
        VkSurfaceFormatKHR format{};
        VkExtent2D         extent{};
        // Size requested via createSurface()/resize(); used when the surface
        // reports currentExtent = UINT32_MAX (Wayland) and to detect resizes.
        VkExtent2D         requestedExtent{};

        std::vector<VkImage>     images;
        std::vector<VkImageView> views;
        std::vector<VkSemaphore> renderFinished;   ///< One per swapchain image.

        VkSemaphore acquireSemaphores[kSlots] = {};
        VkSemaphore slotDone[kSlots]          = {};
        VkFence     slotFences[kSlots]        = {}; ///< Created signaled.
        uint32_t    nextSlot = 0;

        // The current acquisition, until schedulePresent() hands it to a
        // command buffer. Vulkan has no way to give an acquired image back
        // without presenting it, so an abandoned acquisition is simply
        // returned again by the next acquireNextTexture().
        bool        acquired      = false;
        uint32_t    acquiredImage = 0;
        uint32_t    acquiredSlot  = 0;
        std::shared_ptr<TextureView> acquiredView;

        // Set when present/acquire reports OUT_OF_DATE (or the requested size
        // changed); the swapchain is rebuilt at the start of the next
        // acquireNextTexture(), on the surface's own thread.
        std::atomic<bool> needsRecreate{false};

#ifdef __ANDROID__
        ANativeWindow* window = nullptr;  ///< ANativeWindow_acquire()'d.
#endif

        std::mutex mutex;

        ~SurfaceData();
    };

    /** One surface image to present after a command buffer's submission. */
    struct SurfacePresent {
        std::shared_ptr<SurfaceData> surface;
        uint32_t image = 0;
        uint32_t slot  = 0;
    };

    /**
     * @brief Device::submit() helpers for command buffers that carry
     *        Surface presents (see SurfaceData's doc comment).
     *
     * appendSurfaceSubmitSync() adds the wait/signal semaphores to the real
     * submission. completeSurfacePresents() must be called right after that
     * vkQueueSubmit, still under gpu_mutex: it chains the slot-fence
     * submissions and presents every surface in one vkQueuePresentKHR call.
     */
    void appendSurfaceSubmitSync(const std::vector<SurfacePresent>& presents,
                                 std::vector<VkSemaphore>& waits,
                                 std::vector<VkPipelineStageFlags>& waitStages,
                                 std::vector<VkSemaphore>& signals);
    void completeSurfacePresents(VkQueue queue,
                                 const std::vector<SurfacePresent>& presents,
                                 bool submitSucceeded);
}
