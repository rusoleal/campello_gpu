//
//  Minimal Linux windowed example — campello_gpu + raw X11.
//
//  Opens a 640×480 X11 window, creates a Vulkan swapchain, and renders a
//  colored triangle every frame against an animated background.
//
//  Build:
//    cmake -B build -DBUILD_EXAMPLES=ON
//    make -C build campello_linux_example
//    ./build/examples/linux/campello_linux_example
//

#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/platform/linux_surface.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>

#include "triangle_shader.h"

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
    // 3. Create the triangle pipeline from embedded SPIR-V.
    //    The shader has entry points "vertMain" (vertex) and "fragMain"
    //    (fragment) with hardcoded positions and per-vertex RGB colors.
    // ------------------------------------------------------------------
    auto shaderModule = device->createShaderModule(kTriangleSpv, kTriangleSpvSize);
    if (!shaderModule) {
        fprintf(stderr, "Failed to create shader module\n");
        device.reset();
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 1;
    }

    // pipelineDesc is scoped so its shared_ptr<ShaderModule> refs are released
    // immediately after pipeline creation — before shaderModule.reset() in teardown.
    std::shared_ptr<RenderPipeline> pipeline;
    {
        RenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.vertex = VertexDescriptor{
            .module     = shaderModule,
            .entryPoint = "vertMain",
            .buffers    = {}
        };
        pipelineDesc.fragment = FragmentDescriptor{
            .module     = shaderModule,
            .entryPoint = "fragMain"
        };
        pipelineDesc.cullMode  = CullMode::none;
        pipelineDesc.frontFace = FrontFace::ccw;
        pipelineDesc.topology  = PrimitiveTopology::triangleList;
        pipeline = device->createRenderPipeline(pipelineDesc);
    }
    if (!pipeline) {
        fprintf(stderr, "Failed to create render pipeline\n");
        device.reset();
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 1;
    }

    // ------------------------------------------------------------------
    // 4. Render loop — animated background + colored triangle.
    // ------------------------------------------------------------------
    bool running = true;
    int frame = 0;

    while (running) {

        // Pump X11 events.
        while (XPending(display)) {
            XEvent ev;
            XNextEvent(display, &ev);
            if (ev.type == KeyPress || ev.type == DestroyNotify) {
                running = false;
            }
        }

        auto encoder = device->createCommandEncoder();
        if (!encoder) {
            fprintf(stderr, "Failed to create command encoder\n");
            break;
        }

        ColorAttachment attachment{};
        attachment.view          = nullptr;
        attachment.loadOp        = LoadOp::clear;
        attachment.storeOp       = StoreOp::store;
        attachment.clearValue[0] = (std::sin(frame * 0.02f) + 1.0f) * 0.5f * 0.3f;
        attachment.clearValue[1] = (std::cos(frame * 0.03f) + 1.0f) * 0.5f * 0.3f;
        attachment.clearValue[2] = (std::sin(frame * 0.01f) + 1.0f) * 0.5f * 0.3f;
        attachment.clearValue[3] = 1.0f;

        BeginRenderPassDescriptor passDesc{};
        passDesc.colorAttachments = { attachment };

        auto pass = encoder->beginRenderPass(passDesc);
        if (pass) {
            pass->setPipeline(pipeline);
            pass->draw(3);
            pass->end();
        }

        auto commandBuffer = encoder->finish();
        if (commandBuffer) {
            device->submit(commandBuffer);
        }

        ++frame;
    }

    // ------------------------------------------------------------------
    // 5. Tear-down (Device destructor handles all Vulkan cleanup).
    // ------------------------------------------------------------------
    pipeline.reset();
    shaderModule.reset();
    device.reset();
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}
