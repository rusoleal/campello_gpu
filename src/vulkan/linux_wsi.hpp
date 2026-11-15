#pragma once

// Linux window-system integration headers and the CAMPELLO_GPU_X11 /
// CAMPELLO_GPU_WAYLAND switches. Shared by every Vulkan translation unit that
// creates a VkSurfaceKHR (device.cpp's legacy swapchain, surface.cpp's
// Surface): a macro tested with `#if` in a file that never defined it is
// silently 0, which compiled both platforms out of surface.cpp.

#include <vulkan/vulkan.h>

#if !defined(__ANDROID__)
#include <campello_gpu/platform/linux_surface.hpp>

#if __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#define CAMPELLO_GPU_X11 1
#else
#define CAMPELLO_GPU_X11 0
#endif

#if __has_include(<wayland-client.h>)
#include <wayland-client.h>
#include <vulkan/vulkan_wayland.h>
#define CAMPELLO_GPU_WAYLAND 1
#else
#define CAMPELLO_GPU_WAYLAND 0
struct wl_display;
struct wl_surface;
#endif

#endif  // !__ANDROID__
