//
//  Minimal Linux windowed example — campello_gpu + raw X11.
//
//  Opens a 640×480 X11 window, creates a Vulkan swapchain, and clears
//  the screen to a rotating color.
//
//  Build:
//    mkdir build && cd build
//    cmake .. -DCMAKE_BUILD_TYPE=Release
//    cmake --build .
//    ./campello_linux_example
//

#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/platform/linux_surface.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/load_op.hpp>
#include <campello_gpu/constants/store_op.hpp>

#include <X11/Xlib.h>
#include <cstdio>
#include <cmath>
#include <memory>

using namespace systems::leal::campello_gpu;

int main()
{
    // ------------------------------------------------------------------
    // 1. Open an X11 display and create a simple window.
    // ------------------------------------------------------------------
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        fprintf(stderr, "Failed to open X display\n");
        return 1;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(
        display, root,
        0, 0, 640, 480,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(display, window);
    XStoreName(display, window, "campello_gpu — Linux / Vulkan");

    // ------------------------------------------------------------------
    // 2. Create campello_gpu device with the X11 surface.
    // ------------------------------------------------------------------
    LinuxSurfaceInfo surfaceInfo{};
    surfaceInfo.display = display;
    surfaceInfo.window  = reinterpret_cast<void *>(window);
    surfaceInfo.api     = LinuxWindowApi::x11;

    auto device = Device::createDefaultDevice(&surfaceInfo);
    if (!device) {
        fprintf(stderr, "Failed to create campello_gpu device\n");
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 1;
    }

    printf("Device: %s\n", device->getName().c_str());
    printf("Engine: %s\n", Device::getEngineVersion().c_str());

    // ------------------------------------------------------------------
    // 3. Render loop — clear the swapchain to a rotating color.
    // ------------------------------------------------------------------
    bool running = true;
    int frame = 0;
    int windowWidth = 640;
    int windowHeight = 480;

    while (running) {
        // Pump X11 events.
        while (XPending(display)) {
            XEvent ev;
            XNextEvent(display, &ev);
            if (ev.type == KeyPress || ev.type == DestroyNotify) {
                running = false;
            }
            if (ev.type == ConfigureNotify) {
                // Track the new window size. The Vulkan backend automatically
                // recreates the swapchain when vkAcquireNextImageKHR or
                // vkQueuePresentKHR reports OUT_OF_DATE.
                windowWidth  = ev.xconfigure.width;
                windowHeight = ev.xconfigure.height;
            }
        }

        // Build a simple render pass that clears the swapchain.
        auto encoder = device->createCommandEncoder();
        if (!encoder) {
            fprintf(stderr, "Failed to create command encoder\n");
            break;
        }

        ColorAttachment attachment{};
        attachment.view       = nullptr;               // nullptr → use swapchain
        attachment.loadOp     = LoadOp::clear;
        attachment.storeOp    = StoreOp::store;
        attachment.clearValue[0] = (std::sin(frame * 0.02f) + 1.0f) * 0.5f;  // R
        attachment.clearValue[1] = (std::cos(frame * 0.03f) + 1.0f) * 0.5f;  // G
        attachment.clearValue[2] = (std::sin(frame * 0.01f) + 1.0f) * 0.5f;  // B
        attachment.clearValue[3] = 1.0f;                                        // A

        BeginRenderPassDescriptor passDesc{};
        passDesc.colorAttachments = { attachment };

        auto pass = encoder->beginRenderPass(passDesc);
        if (pass) {
            pass->end();
        }

        auto commandBuffer = encoder->finish();
        if (commandBuffer) {
            device->submit(commandBuffer);
        }

        ++frame;
    }

    // ------------------------------------------------------------------
    // 4. Tear-down (Device destructor handles all Vulkan cleanup).
    // ------------------------------------------------------------------
    device.reset();
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}
