#include <campello_gpu/surface.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/platform/linux_surface.hpp>
#include "common.hpp"
#include "surface_handle.hpp"
#include "texture_view_handle.hpp"
#include "command_buffer_handle.hpp"

#include <algorithm>

#ifdef __ANDROID__
#include <vulkan/vulkan_android.h>
#endif

using namespace systems::leal::campello_gpu;

PixelFormat nativeToPixelFormat(VkFormat format);  // device.cpp

// Surface::native points at a heap-allocated shared_ptr<SurfaceData>: command
// buffers that carry a present (CommandBufferHandle::surfacePresents) share
// ownership, so a window closing between schedulePresent() and submit() can't
// free the swapchain under them.
namespace {

std::shared_ptr<SurfaceData> &surfaceRef(void *native) {
    return *static_cast<std::shared_ptr<SurfaceData> *>(native);
}

VkSurfaceKHR createPlatformSurface(void *nativeWindow) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef __ANDROID__
    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = static_cast<ANativeWindow *>(nativeWindow);
    if (vkCreateAndroidSurfaceKHR(getInstance(), &info, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
#else
    auto *surfInfo = static_cast<LinuxSurfaceInfo *>(nativeWindow);
#if CAMPELLO_GPU_X11
    if (surfInfo->api == LinuxWindowApi::x11) {
        VkXlibSurfaceCreateInfoKHR xinfo{};
        xinfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        xinfo.dpy    = (Display *)surfInfo->display;
        xinfo.window = (Window)(uintptr_t)surfInfo->window;
        if (vkCreateXlibSurfaceKHR(getInstance(), &xinfo, nullptr, &surface) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return surface;
    }
#endif
#if CAMPELLO_GPU_WAYLAND
    if (surfInfo->api == LinuxWindowApi::wayland) {
        VkWaylandSurfaceCreateInfoKHR winfo{};
        winfo.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        winfo.display = (wl_display *)surfInfo->display;
        winfo.surface = (wl_surface *)surfInfo->window;
        if (vkCreateWaylandSurfaceKHR(getInstance(), &winfo, nullptr, &surface) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return surface;
    }
#endif
    (void)surfInfo;
#endif
    return surface;
}

// Same preference order as the legacy swapchain in Device creation (BGRA8,
// then RGBA8, both sRGB-nonlinear), so render pipelines built for one are
// compatible with the other and with every other surface on this device.
VkSurfaceFormatKHR chooseFormat(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (count) vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());
    if (formats.empty()) return VkSurfaceFormatKHR{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (VkFormat want : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
        for (const auto &f : formats) {
            if (f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
        }
    }
    return formats[0];
}

void destroySwapchainObjects(SurfaceData *s, bool destroySwapchain) {
    for (auto v : s->views)          vkDestroyImageView(s->device, v, nullptr);
    for (auto sem : s->renderFinished) vkDestroySemaphore(s->device, sem, nullptr);
    s->views.clear();
    s->renderFinished.clear();
    s->images.clear();
    for (uint32_t i = 0; i < SurfaceData::kSlots; ++i) {
        if (s->acquireSemaphores[i]) vkDestroySemaphore(s->device, s->acquireSemaphores[i], nullptr);
        if (s->slotDone[i])          vkDestroySemaphore(s->device, s->slotDone[i], nullptr);
        if (s->slotFences[i])        vkDestroyFence(s->device, s->slotFences[i], nullptr);
        s->acquireSemaphores[i] = VK_NULL_HANDLE;
        s->slotDone[i]          = VK_NULL_HANDLE;
        s->slotFences[i]        = VK_NULL_HANDLE;
    }
    if (destroySwapchain && s->swapchain) {
        vkDestroySwapchainKHR(s->device, s->swapchain, nullptr);
        s->swapchain = VK_NULL_HANDLE;
    }
}

// (Re)builds the swapchain and every per-swapchain/per-slot sync object.
// Caller holds s->mutex. The queue is drained first (under gpu_mutex), so no
// submission still references the old images or semaphores; this is the same
// "only on resize/out-of-date" cost the legacy recreateSwapchain() pays with
// vkDeviceWaitIdle. Recreating the acquire semaphores also discards the
// signal of an abandoned acquisition, which could otherwise never be waited.
bool buildSwapchainImpl(SurfaceData *s) {
    DeviceData *dd = s->deviceData;
    {
        std::lock_guard<std::mutex> lock(dd->gpu_mutex);
        vkQueueWaitIdle(dd->graphicsQueue);
    }
    s->acquired = false;
    s->acquiredView.reset();

    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dd->physicalDevice, s->surface, &caps) != VK_SUCCESS)
        return false;

    VkExtent2D extent = caps.currentExtent;
#ifdef __ANDROID__
    // Android: the window's own size is authoritative. currentExtent can
    // still report the old size when a resize or rotation is handled (it
    // follows the buffers already queued), and a swapchain left at the old
    // size is never reported out of date there: the compositor just scales
    // it, so the app's frames came out cropped after rotating. Any buffer
    // size is valid (it is scaled to the window), so take the window's.
    if (s->window) {
        const int32_t w = ANativeWindow_getWidth(s->window);
        const int32_t h = ANativeWindow_getHeight(s->window);
        if (w > 0 && h > 0) extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
#endif
    if (extent.width == UINT32_MAX) {
        extent = s->requestedExtent;
    }
    extent.width  = std::clamp(extent.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    VkSwapchainKHR oldSwapchain = s->swapchain;
    destroySwapchainObjects(s, /*destroySwapchain=*/false);
    s->extent = extent;
    if (extent.width == 0 || extent.height == 0) {
        // Minimised: keep no swapchain; acquire returns nullptr until resized.
        if (oldSwapchain) vkDestroySwapchainKHR(s->device, oldSwapchain, nullptr);
        s->swapchain = VK_NULL_HANDLE;
        return false;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = s->surface;
    {
        // Same image count as the legacy swapchain: see recreateSwapchain().
        uint32_t count = std::max(caps.minImageCount, 3u);
        if (caps.maxImageCount > 0) count = std::min(count, caps.maxImageCount);
        sci.minImageCount = count;
    }
    sci.imageFormat      = s->format.format;
    sci.imageColorSpace  = s->format.colorSpace;
    sci.imageExtent      = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // IDENTITY whenever supported, never pre-rotated: see the long comment on
    // VK_SUBOPTIMAL_KHR in Device::submit() for why SUBOPTIMAL is ignored.
    sci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                           ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                           : caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto flag : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if (caps.supportedCompositeAlpha & flag) { sci.compositeAlpha = flag; break; }
    }
    sci.presentMode  = choosePresentMode(dd->physicalDevice, s->surface, VK_PRESENT_MODE_MAILBOX_KHR);
    sci.clipped      = VK_TRUE;
    sci.oldSwapchain = oldSwapchain;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult r = vkCreateSwapchainKHR(s->device, &sci, nullptr, &swapchain);
    if (oldSwapchain) vkDestroySwapchainKHR(s->device, oldSwapchain, nullptr);
    s->swapchain = VK_NULL_HANDLE;
    if (r != VK_SUCCESS) return false;
    s->swapchain = swapchain;

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(s->device, swapchain, &imageCount, nullptr);
    s->images.resize(imageCount);
    vkGetSwapchainImagesKHR(s->device, swapchain, &imageCount, s->images.data());

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    s->views.assign(imageCount, VK_NULL_HANDLE);
    s->renderFinished.assign(imageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = s->images[i];
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = s->format.format;
        vi.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(s->device, &vi, nullptr, &s->views[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(s->device, &semInfo, nullptr, &s->renderFinished[i]) != VK_SUCCESS) return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < SurfaceData::kSlots; ++i) {
        if (vkCreateSemaphore(s->device, &semInfo, nullptr, &s->acquireSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(s->device, &semInfo, nullptr, &s->slotDone[i]) != VK_SUCCESS ||
            vkCreateFence(s->device, &fenceInfo, nullptr, &s->slotFences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    s->nextSlot = 0;
    return true;
}

// All-or-nothing: on failure no swapchain is left (acquire returns nullptr
// until the next resize()/out-of-date rebuild), never a half-built one.
bool buildSwapchain(SurfaceData *s) {
    if (buildSwapchainImpl(s)) return true;
    destroySwapchainObjects(s, /*destroySwapchain=*/true);
    return false;
}

} // namespace

SurfaceData::~SurfaceData() {
    if (deviceData) {
        std::lock_guard<std::mutex> lock(deviceData->gpu_mutex);
        vkQueueWaitIdle(deviceData->graphicsQueue);
    }
    acquiredView.reset();
    destroySwapchainObjects(this, /*destroySwapchain=*/true);
    if (surface) vkDestroySurfaceKHR(getInstance(), surface, nullptr);
#ifdef __ANDROID__
    if (window) ANativeWindow_release(window);
#endif
}

// ── Device::submit() helpers ────────────────────────────────────────────────

void systems::leal::campello_gpu::appendSurfaceSubmitSync(
        const std::vector<SurfacePresent> &presents,
        std::vector<VkSemaphore> &waits,
        std::vector<VkPipelineStageFlags> &waitStages,
        std::vector<VkSemaphore> &signals) {
    for (const auto &p : presents) {
        waits.push_back(p.surface->acquireSemaphores[p.slot]);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        signals.push_back(p.surface->renderFinished[p.image]);
        signals.push_back(p.surface->slotDone[p.slot]);
    }
}

void systems::leal::campello_gpu::completeSurfacePresents(
        VkQueue queue, const std::vector<SurfacePresent> &presents, bool submitSucceeded) {
    if (presents.empty()) return;
    if (!submitSucceeded) {
        // The acquire semaphores were signaled but never waited, and the
        // images stay acquired: rebuild those swapchains from scratch.
        for (const auto &p : presents) p.surface->needsRecreate = true;
        return;
    }

    std::vector<VkSwapchainKHR> swapchains;
    std::vector<uint32_t>       indices;
    std::vector<VkSemaphore>    waits;
    for (const auto &p : presents) {
        SurfaceData *s = p.surface.get();
        // Chained empty submission: signals this slot's fence once the real
        // submission (which signals slotDone) has finished on the GPU.
        vkResetFences(s->device, 1, &s->slotFences[p.slot]);
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo chained{};
        chained.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        chained.waitSemaphoreCount = 1;
        chained.pWaitSemaphores    = &s->slotDone[p.slot];
        chained.pWaitDstStageMask  = &stage;
        vkQueueSubmit(queue, 1, &chained, s->slotFences[p.slot]);

        swapchains.push_back(s->swapchain);
        indices.push_back(p.image);
        waits.push_back(s->renderFinished[p.image]);
    }

    // One present for every window this command buffer drew.
    std::vector<VkResult> results(presents.size(), VK_SUCCESS);
    VkPresentInfoKHR info{};
    info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = (uint32_t)waits.size();
    info.pWaitSemaphores    = waits.data();
    info.swapchainCount     = (uint32_t)swapchains.size();
    info.pSwapchains        = swapchains.data();
    info.pImageIndices      = indices.data();
    info.pResults           = results.data();
    vkQueuePresentKHR(queue, &info);
    for (size_t i = 0; i < presents.size(); ++i) {
        // SUBOPTIMAL is advisory and permanent on rotated displays: see the
        // comment in Device::submit(). Only OUT_OF_DATE needs a rebuild.
        if (results[i] == VK_ERROR_OUT_OF_DATE_KHR || results[i] == VK_ERROR_SURFACE_LOST_KHR) {
            presents[i].surface->needsRecreate = true;
        }
    }
}

// ── Surface ─────────────────────────────────────────────────────────────────

Surface::Surface(void *pd) : native(pd) {}

Surface::~Surface() {
    delete static_cast<std::shared_ptr<SurfaceData> *>(native);
}

std::shared_ptr<Surface> Device::createSurface(void *nativeWindow, uint32_t width, uint32_t height) {
    auto *dd = static_cast<DeviceData *>(native);
    if (!nativeWindow || !dd) return nullptr;

    VkSurfaceKHR vkSurface = createPlatformSurface(nativeWindow);
    if (vkSurface == VK_NULL_HANDLE) return nullptr;

    // A headless device (createDefaultDevice(nullptr)) picked its queue
    // without a surface to check against.
    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(dd->physicalDevice, dd->queueFamilyIndex, vkSurface,
                                         &presentSupported);
    if (!presentSupported) {
        vkDestroySurfaceKHR(getInstance(), vkSurface, nullptr);
        return nullptr;
    }

    auto data = std::make_shared<SurfaceData>();
    data->deviceData = dd;
    data->device     = dd->device;
    data->surface    = vkSurface;
    data->format     = chooseFormat(dd->physicalDevice, vkSurface);
#ifdef __ANDROID__
    data->window = static_cast<ANativeWindow *>(nativeWindow);
    ANativeWindow_acquire(data->window);
    if (width == 0 || height == 0) {
        width  = (uint32_t)std::max(0, ANativeWindow_getWidth(data->window));
        height = (uint32_t)std::max(0, ANativeWindow_getHeight(data->window));
    }
#else
    if (width == 0 || height == 0) {
        auto *info = static_cast<LinuxSurfaceInfo *>(nativeWindow);
        width  = info->width;
        height = info->height;
    }
#endif
    data->requestedExtent = {width, height};
    if (data->format.format == VK_FORMAT_UNDEFINED) return nullptr;
    {
        std::lock_guard<std::mutex> lock(data->mutex);
        buildSwapchain(data.get());  // a 0x0 window builds later, on resize
    }
    return std::shared_ptr<Surface>(new Surface(new std::shared_ptr<SurfaceData>(std::move(data))));
}

std::shared_ptr<TextureView> Surface::acquireNextTexture() {
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);

    // Rebuild only when asked to (resize, OUT_OF_DATE, failed submit). A
    // window with no swapchain (0x0 / minimised) just skips frames here
    // instead of draining the queue on every attempt.
    if (s->needsRecreate.exchange(false)) buildSwapchain(s);
    if (s->swapchain == VK_NULL_HANDLE) return nullptr;
    if (s->acquired) return s->acquiredView;  // abandoned last frame: reuse it

    const uint32_t slot = s->nextSlot;
    if (vkWaitForFences(s->device, 1, &s->slotFences[slot], VK_TRUE,
                        DeviceData::kWaitTimeoutNs) != VK_SUCCESS) {
        return nullptr;  // GPU too far behind: skip this frame
    }

    uint32_t image = 0;
    VkResult r = vkAcquireNextImageKHR(s->device, s->swapchain, DeviceData::kWaitTimeoutNs,
                                       s->acquireSemaphores[slot], VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        s->needsRecreate = true;
        return nullptr;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return nullptr;  // incl. VK_TIMEOUT / NOT_READY

    auto *vh = new TextureViewHandle();
    vh->device     = s->device;
    vh->imageView  = s->views[image];
    vh->image      = s->images[image];
    vh->format     = s->format.format;
    vh->width      = s->extent.width;
    vh->height     = s->extent.height;
    vh->owned      = false;  // the surface owns its swapchain views
    vh->deviceData = s->deviceData;
    vh->surface    = s;

    s->acquired      = true;
    s->acquiredImage = image;
    s->acquiredSlot  = slot;
    s->nextSlot      = (slot + 1) % SurfaceData::kSlots;
    s->acquiredView  = std::shared_ptr<TextureView>(new TextureView(vh));
    return s->acquiredView;
}

bool Surface::schedulePresent(const std::shared_ptr<CommandBuffer> &commandBuffer) {
    if (!commandBuffer || !commandBuffer->native) return false;
    auto &ref = surfaceRef(native);
    std::lock_guard<std::mutex> lock(ref->mutex);
    if (!ref->acquired) return false;

    auto *cb = static_cast<CommandBufferHandle *>(commandBuffer->native);
    cb->surfacePresents.push_back(SurfacePresent{ref, ref->acquiredImage, ref->acquiredSlot});
    ref->acquired = false;
    ref->acquiredView.reset();
    return true;
}

void Surface::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->requestedExtent.width == width && s->requestedExtent.height == height) return;
    s->requestedExtent = {width, height};
    s->needsRecreate   = true;
}

uint32_t Surface::getWidth() const noexcept {
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    return s->swapchain ? s->extent.width : s->requestedExtent.width;
}

uint32_t Surface::getHeight() const noexcept {
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    return s->swapchain ? s->extent.height : s->requestedExtent.height;
}

PixelFormat Surface::getPixelFormat() const noexcept {
    SurfaceData *s = surfaceRef(native).get();
    return nativeToPixelFormat(s->format.format);
}
