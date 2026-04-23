#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu
{

    /**
     * @brief Platform-specific surface descriptor for Linux windowed applications.
     *
     * Pass a pointer to this struct as the `pd` argument to
     * `Device::createDefaultDevice()` or `Device::createDevice()`.
     *
     * @code
     * // X11 example
     * campello_gpu::LinuxSurfaceInfo surfaceInfo{};
     * surfaceInfo.display   = (void*)display;   // Display*
     * surfaceInfo.window    = (void*)(uintptr_t)window; // Window (unsigned long)
     * surfaceInfo.api       = campello_gpu::LinuxWindowApi::x11;
     * auto device = campello_gpu::Device::createDefaultDevice(&surfaceInfo);
     *
     * // Wayland example
     * campello_gpu::LinuxSurfaceInfo surfaceInfo{};
     * surfaceInfo.display   = (void*)wl_display;
     * surfaceInfo.window    = (void*)wl_surface;
     * surfaceInfo.api       = campello_gpu::LinuxWindowApi::wayland;
     * auto device = campello_gpu::Device::createDefaultDevice(&surfaceInfo);
     * @endcode
     */
    enum class LinuxWindowApi
    {
        x11,
        wayland
    };

    struct LinuxSurfaceInfo
    {
        void *display = nullptr; // Display* (X11) or wl_display* (Wayland)
        void *window  = nullptr; // Window cast via uintptr_t (X11) or wl_surface* (Wayland)
        LinuxWindowApi api = LinuxWindowApi::x11;
    };

}
