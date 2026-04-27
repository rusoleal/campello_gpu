//
//  Minimal WASM / WebGPU example — campello_gpu + Emscripten.
//
//  Renders a colored triangle to an HTML5 canvas.
//
//  Build:
//    mkdir build && cd build
//    emcmake cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON
//    cmake --build .
//    emrun campello_wasm_example.html
//

#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>

#include <emscripten.h>
#include <cstdio>
#include <memory>
#include <vector>

using namespace systems::leal::campello_gpu;

static std::shared_ptr<Device> g_device;
static std::shared_ptr<RenderPipeline> g_pipeline;
static std::shared_ptr<Buffer> g_vertexBuffer;

// Simple triangle vertices: position (vec2) + color (vec3)
static const float vertices[] = {
    // x,    y,     r,   g,   b
     0.0f,  0.5f,  1.0f, 0.0f, 0.0f,
    -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
     0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
};

static const char* vertexWGSL = R"(
    struct VertexOutput {
        @builtin(position) position: vec4f,
        @location(0) color: vec3f,
    };

    @vertex
    fn main(@location(0) pos: vec2f, @location(1) color: vec3f) -> VertexOutput {
        var output: VertexOutput;
        output.position = vec4f(pos, 0.0, 1.0);
        output.color = color;
        return output;
    }
)";

static const char* fragmentWGSL = R"(
    @fragment
    fn main(@location(0) color: vec3f) -> @location(0) vec4f {
        return vec4f(color, 1.0);
    }
)";

static void init() {
    g_device = Device::createDefaultDevice(const_cast<char*>("#canvas"));
    if (!g_device) {
        fprintf(stderr, "Failed to create campello_gpu device\n");
        return;
    }

    printf("Device: %s\n", g_device->getName().c_str());
    printf("Engine: %s\n", Device::getEngineVersion().c_str());

    // Create vertex buffer
    g_vertexBuffer = g_device->createBuffer(sizeof(vertices), BufferUsage::vertex, (void*)vertices);

    // Create shader modules from WGSL
    auto vertShader = g_device->createShaderModule(vertexWGSL);
    auto fragShader = g_device->createShaderModule(fragmentWGSL);
    if (!vertShader || !fragShader) {
        fprintf(stderr, "Failed to compile WGSL shaders\n");
        return;
    }

    // Build render pipeline
    RenderPipelineDescriptor desc{};
    desc.vertex.module = vertShader;
    desc.vertex.entryPoint = "main";

    VertexLayout layout{};
    layout.arrayStride = 5 * sizeof(float);
    layout.stepMode = StepMode::vertex;
    layout.attributes = {
        { ComponentType::ctFloat, AccessorType::acVec2, 0, 0, false },
        { ComponentType::ctFloat, AccessorType::acVec3, 2 * sizeof(float), 1, false },
    };
    desc.vertex.buffers = { layout };

    desc.fragment = FragmentDescriptor{};
    desc.fragment->module = fragShader;
    desc.fragment->entryPoint = "main";
    desc.fragment->targets = {
        ColorState{ PixelFormat::bgra8unorm, ColorWrite::all, std::nullopt }
    };

    desc.topology = PrimitiveTopology::triangleList;
    desc.cullMode = CullMode::none;
    desc.frontFace = FrontFace::ccw;

    g_pipeline = g_device->createRenderPipeline(desc);
    if (!g_pipeline) {
        fprintf(stderr, "Failed to create render pipeline\n");
        return;
    }
}

static void render() {
    if (!g_device || !g_pipeline) return;

    auto swapchainView = g_device->getSwapchainTextureView();
    if (!swapchainView) return;

    auto encoder = g_device->createCommandEncoder();
    if (!encoder) return;

    ColorAttachment attachment{};
    attachment.view = swapchainView;
    attachment.loadOp = LoadOp::clear;
    attachment.storeOp = StoreOp::store;
    attachment.clearValue[0] = 0.1f;
    attachment.clearValue[1] = 0.1f;
    attachment.clearValue[2] = 0.1f;
    attachment.clearValue[3] = 1.0f;

    BeginRenderPassDescriptor passDesc{};
    passDesc.colorAttachments = { attachment };

    auto pass = encoder->beginRenderPass(passDesc);
    if (pass) {
        pass->setPipeline(g_pipeline);
        pass->setVertexBuffer(0, g_vertexBuffer, 0, -1);
        pass->draw(3, 1, 0, 0);
        pass->end();
    }

    auto commandBuffer = encoder->finish();
    if (commandBuffer) {
        g_device->submit(commandBuffer);
    }
}

extern "C" void emscripten_main_loop() {
    render();
}

int main() {
    init();
    emscripten_set_main_loop(emscripten_main_loop, 0, 1);
    return 0;
}
