#pragma once

#include <cstdint>
#include <memory>
#include <campello_gpu/constants/pixel_format.hpp>

namespace systems::leal::campello_gpu
{
    class Device;
    class TextureView;
    class CommandBuffer;

    /**
     * @brief A presentable render target bound to one native window, with its
     *        own swapchain.
     *
     * One `Device` can own any number of surfaces, one per window, and they
     * all share the device's queue, pipelines and resources. Create them with
     * `Device::createSurface()`. This replaces the one-swapchain-per-device
     * model of `Device::createDefaultDevice(pd)` / `getSwapchainTextureView()`
     * / `scheduleNextPresent()`, which stays available for single-window apps.
     *
     * Per-frame protocol (all calls for a given surface happen on one thread,
     * usually that window's raster thread; different surfaces can be driven
     * from different threads at the same time):
     *
     * @code
     * auto view = surface->acquireNextTexture();
     * if (!view) return;                         // not available: skip this frame
     * auto encoder = device->createCommandEncoder();
     * // ... beginRenderPass({.colorAttachments = {{.view = view, ...}}}) ...
     * auto cmd = encoder->finish();
     * surface->schedulePresent(cmd);             // must come before submit()
     * device->submit(cmd);
     * @endcode
     *
     * `schedulePresent()` ties presentation to *that* command buffer, so the
     * image is shown only after the GPU finishes rendering it. There is no
     * device-wide "pending present" slot, so two windows can never present
     * each other's images.
     *
     * Native handle accepted by `Device::createSurface()`, per backend:
     *   - Metal (macOS/iOS): `CA::MetalLayer*` / `(__bridge void*)CAMetalLayer`
     *     (for an `MTKView`, pass `view.layer`; then stop using
     *     `view.currentDrawable`).
     *   - Vulkan Android: `ANativeWindow*`.  Vulkan Linux: `LinuxSurfaceInfo*`.
     *   - DirectX 12: `HWND`.
     *   - WebGPU: a `<canvas>` CSS selector string such as `"#canvas"`.
     *
     * Backend status: implemented on Metal and Vulkan (Android and Linux; a
     * headless `createDefaultDevice(nullptr)` device can present through
     * surfaces). DirectX 12 and WebGPU return `nullptr` from
     * `Device::createSurface()` until their implementations land.
     *
     * Destroy every surface before its `Device`.
     */
    class Surface
    {
    private:
        friend class Device;
        void *native;
        Surface(void *pd);

    public:
        ~Surface();

        Surface(const Surface &)            = delete;
        Surface &operator=(const Surface &) = delete;

        /**
         * @brief Acquires the next image to render into this frame.
         *
         * Returns `nullptr` when no image is available (window minimised or
         * zero-sized, drawable pool exhausted, swapchain out of date, GPU too
         * far behind). In that case skip the frame; an out-of-date swapchain
         * is rebuilt on the next call.
         *
         * An image acquired but never passed to `schedulePresent()` (an
         * abandoned frame) is recycled: on Metal the next call releases it and
         * takes a fresh drawable; on Vulkan, which can't give an image back
         * without presenting it, the next call returns the same image again.
         *
         * The returned view stays valid until the command buffer that presents
         * it has been submitted. Keep it no longer than that.
         */
        std::shared_ptr<TextureView> acquireNextTexture();

        /**
         * @brief Presents the image from the last `acquireNextTexture()` once
         *        `commandBuffer` has finished executing on the GPU.
         *
         * Call it after `CommandEncoder::finish()` and before
         * `Device::submit(commandBuffer)`. It does nothing (and returns
         * `false`) when no image is currently acquired.
         *
         * @return `true` if a present was scheduled.
         */
        bool schedulePresent(const std::shared_ptr<CommandBuffer> &commandBuffer);

        /**
         * @brief Resizes the swapchain images, in physical pixels.
         *
         * Call it when the window's backing size changes. Images acquired after
         * this call have the new size. A size of 0 in either dimension is
         * ignored.
         */
        void resize(uint32_t width, uint32_t height);

        /** @brief Current swapchain image width in physical pixels. */
        uint32_t getWidth() const noexcept;

        /** @brief Current swapchain image height in physical pixels. */
        uint32_t getHeight() const noexcept;

        /**
         * @brief The swapchain's pixel format.
         *
         * Use it for render pipelines and offscreen textures that must be
         * render-pass compatible with this surface (see
         * `Device::getSwapchainPixelFormat()` for why that matters).
         */
        PixelFormat getPixelFormat() const noexcept;
    };
}
