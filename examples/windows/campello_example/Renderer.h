#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <campello_gpu/device.hpp>
#include <campello_gpu/render_pipeline.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Renderer
//
// Owns the campello_gpu Device and RenderPipeline.
// Call render() once per frame to draw the triangle to the window.
// ---------------------------------------------------------------------------
class Renderer
{
public:
    Renderer(HWND hwnd, int width, int height);

    void render();
    void resize(int width, int height);

private:
    HWND                            _hwnd     = nullptr;
    int                             _width    = 0;
    int                             _height   = 0;
    std::shared_ptr<Device>         _device;
    std::shared_ptr<RenderPipeline> _pipeline;
};
