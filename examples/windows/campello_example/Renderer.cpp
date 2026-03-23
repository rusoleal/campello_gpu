//
// Renderer.cpp
// campello_example — Windows
//
// Triangle demo: all GPU work goes through campello_gpu.
// The only platform-specific call is getSwapchainTextureView() to obtain
// the current back buffer, mirroring the Metal example's
//   TextureView::fromNative((__bridge void*)drawable.texture)
//

#define NOMINMAX
#include "Renderer.h"

#include <d3dcompiler.h>
#include <stdexcept>
#include <string>

#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>

// ---------------------------------------------------------------------------
// HLSL source — vertex positions and colours are hard-coded, no vertex buffer.
// Matches the triangle in Shaders.metal.
// ---------------------------------------------------------------------------
static const char* kShaderSource = R"HLSL(

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
};

static const float2 kPositions[3] =
{
    float2( 0.0,  0.6),
    float2(-0.6, -0.6),
    float2( 0.6, -0.6)
};

static const float3 kColors[3] =
{
    float3(1.0, 0.2, 0.2),   // red   (top)
    float3(0.2, 1.0, 0.2),   // green (bottom-left)
    float3(0.2, 0.4, 1.0)    // blue  (bottom-right)
};

VSOutput VSMain(uint vertId : SV_VertexID)
{
    VSOutput output;
    output.position = float4(kPositions[vertId], 0.0, 1.0);
    output.color    = kColors[vertId];
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}

)HLSL";

// ---------------------------------------------------------------------------
// Helper: compile an HLSL string to DXBC bytecode via D3DCompile.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> compileShader(const char* source,
                                          const char* entry,
                                          const char* target)
{
    ID3DBlob* code  = nullptr;
    ID3DBlob* error = nullptr;

    HRESULT hr = D3DCompile(source, strlen(source),
                            nullptr, nullptr, nullptr,
                            entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            &code, &error);
    if (FAILED(hr)) {
        std::string msg = "Shader compile error";
        if (error) {
            msg += ": ";
            msg += static_cast<const char*>(error->GetBufferPointer());
            error->Release();
        }
        throw std::runtime_error(msg);
    }
    if (error) error->Release();

    std::vector<uint8_t> bytecode(
        static_cast<uint8_t*>(code->GetBufferPointer()),
        static_cast<uint8_t*>(code->GetBufferPointer()) + code->GetBufferSize());
    code->Release();
    return bytecode;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

Renderer::Renderer(HWND hwnd, int width, int height)
    : _hwnd(hwnd), _width(width), _height(height)
{
    // Create a campello_gpu device with the window handle so the library
    // creates a swapchain for us.
    _device = Device::createDefaultDevice(hwnd);
    if (!_device)
        throw std::runtime_error("campello_gpu: failed to create device");

    OutputDebugStringA(("campello_gpu device: " + _device->getName()
                        + "  engine: " + Device::getEngineVersion() + "\n").c_str());

    // -----------------------------------------------------------------------
    // Shader modules — compiled from embedded HLSL at startup.
    // -----------------------------------------------------------------------
    auto vsBytecode = compileShader(kShaderSource, "VSMain", "vs_5_0");
    auto psBytecode = compileShader(kShaderSource, "PSMain", "ps_5_0");

    auto vsModule = _device->createShaderModule(vsBytecode.data(), vsBytecode.size());
    auto psModule = _device->createShaderModule(psBytecode.data(), psBytecode.size());
    if (!vsModule || !psModule)
        throw std::runtime_error("campello_gpu: createShaderModule failed");

    // -----------------------------------------------------------------------
    // Render pipeline
    // Positions and colours are hard-coded in the shader — no vertex buffer.
    // -----------------------------------------------------------------------
    VertexDescriptor vertDesc;
    vertDesc.module     = vsModule;
    vertDesc.entryPoint = "VSMain";
    // vertDesc.buffers intentionally left empty (no vertex input)

    FragmentDescriptor fragDesc;
    fragDesc.module     = psModule;
    fragDesc.entryPoint = "PSMain";
    fragDesc.targets    = {{ PixelFormat::rgba8unorm, ColorWrite::all }};

    RenderPipelineDescriptor pipeDesc;
    pipeDesc.vertex    = vertDesc;
    pipeDesc.fragment  = fragDesc;
    pipeDesc.topology  = PrimitiveTopology::triangleList;
    pipeDesc.cullMode  = CullMode::none;
    pipeDesc.frontFace = FrontFace::ccw;

    _pipeline = _device->createRenderPipeline(pipeDesc);
    if (!_pipeline)
        throw std::runtime_error("campello_gpu: createRenderPipeline failed");
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------

void Renderer::render()
{
    if (!_device || !_pipeline) return;

    // Get the current swapchain back buffer as a campello_gpu TextureView.
    // This mirrors the Metal example's:
    //   TextureView::fromNative((__bridge void*)drawable.texture)
    auto colorView = _device->getSwapchainTextureView();
    if (!colorView) return;

    // Build the render pass descriptor.
    ColorAttachment colorAttach;
    colorAttach.clearValue[0] = 0.05f;
    colorAttach.clearValue[1] = 0.05f;
    colorAttach.clearValue[2] = 0.05f;
    colorAttach.clearValue[3] = 1.00f;
    colorAttach.loadOp        = LoadOp::clear;
    colorAttach.storeOp       = StoreOp::store;
    colorAttach.view          = colorView;

    BeginRenderPassDescriptor rpDesc;
    rpDesc.colorAttachments = { colorAttach };

    // Record commands via campello_gpu.
    auto encoder   = _device->createCommandEncoder();
    auto renderEnc = encoder->beginRenderPass(rpDesc);
    renderEnc->setPipeline(_pipeline);
    renderEnc->setViewport(0.0f, 0.0f,
                           static_cast<float>(_width),
                           static_cast<float>(_height),
                           0.0f, 1.0f);
    renderEnc->setScissorRect(0.0f, 0.0f,
                              static_cast<float>(_width),
                              static_cast<float>(_height));
    renderEnc->draw(3, 1, 0, 0);   // 3 vertices, 1 instance
    renderEnc->end();

    auto cmdBuffer = encoder->finish();

    // Submit (executes the command list and presents the swapchain).
    _device->submit(cmdBuffer);
}

void Renderer::resize(int width, int height)
{
    _width  = width;
    _height = height;
}
