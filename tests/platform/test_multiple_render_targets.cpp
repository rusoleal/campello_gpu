// Platform integration tests for multiple render targets: one render pass
// with several color attachments, each cleared or drawn to separately (a
// G-buffer's shape). The draw test uses SPIR-V, so it runs on Vulkan only.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace systems::leal::campello_gpu;

namespace {

std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__) || defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

constexpr uint32_t kSize = 16;

struct Target {
    std::shared_ptr<Texture> texture;
    std::shared_ptr<TextureView> view;
    PixelFormat format;
};

Target makeTarget(const std::shared_ptr<Device>& device, PixelFormat format) {
    Target t;
    t.format = format;
    t.texture = device->createTexture(
        TextureType::tt2d, format, kSize, kSize, 1, 1, 1,
        static_cast<TextureUsage>(static_cast<int>(TextureUsage::renderTarget) |
                                  static_cast<int>(TextureUsage::textureBinding) |
                                  static_cast<int>(TextureUsage::copySrc)));
    if (t.texture) t.view = t.texture->createView(format, 1);
    return t;
}

ColorAttachment attachment(const Target& t, LoadOp loadOp, float r, float g, float b, float a) {
    ColorAttachment ca{};
    ca.view = t.view;
    ca.loadOp = loadOp;
    ca.storeOp = StoreOp::store;
    ca.clearValue[0] = r;
    ca.clearValue[1] = g;
    ca.clearValue[2] = b;
    ca.clearValue[3] = a;
    return ca;
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1fu, mantissa = h & 0x3ffu;
    float v;
    if (exponent == 0) v = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31) v = mantissa ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -v : v;
}

// The centre pixel of a target as floats (rgba8unorm or rgba16float).
std::vector<float> centrePixel(const Target& t) {
    const uint32_t bpp = t.format == PixelFormat::rgba16float ? 8 : 4;
    std::vector<uint8_t> bytes(kSize * kSize * bpp);
    if (!t.texture->download(0, 0, bytes.data(), bytes.size())) return {};
    const size_t at = (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * bpp;
    std::vector<float> out(4);
    for (int c = 0; c < 4; c++) {
        if (bpp == 8) {
            uint16_t h;
            std::memcpy(&h, &bytes[at + c * 2], 2);
            out[c] = halfToFloat(h);
        } else {
            out[c] = bytes[at + c] / 255.0f;
        }
    }
    return out;
}

void expectPixel(const Target& t, float r, float g, float b, float a, const char* what) {
    const auto p = centrePixel(t);
    ASSERT_EQ(p.size(), 4u) << what << ": download failed";
    EXPECT_NEAR(p[0], r, 0.01f) << what;
    EXPECT_NEAR(p[1], g, 0.01f) << what;
    EXPECT_NEAR(p[2], b, 0.01f) << what;
    EXPECT_NEAR(p[3], a, 0.01f) << what;
}

void submitAndWait(const std::shared_ptr<Device>& device, const std::shared_ptr<CommandEncoder>& encoder) {
    auto commands = encoder->finish();
    ASSERT_NE(commands, nullptr);
    auto fence = device->createFence();
    device->submit(commands, fence);
    fence->wait();
}

#if defined(__ANDROID__) || (defined(__linux__) && !defined(__EMSCRIPTEN__))
// A full-screen triangle (gl_VertexIndex), and a fragment shader writing
// (1,0,0,1) to location 0, (0,1,0,1) to 1 and (0.25,0.5,0.75,1) to 2
// (glslangValidator -V).
const uint32_t kVertexSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x0000002b, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x0000001d, 0x00030003,
    0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009,
    0x00000070, 0x00060005, 0x0000000c, 0x565f6c67, 0x65747265, 0x646e4978, 0x00007865, 0x00060005,
    0x0000001b, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x0000001b, 0x00000000,
    0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006, 0x0000001b, 0x00000001, 0x505f6c67, 0x746e696f,
    0x657a6953, 0x00000000, 0x00070006, 0x0000001b, 0x00000002, 0x435f6c67, 0x4470696c, 0x61747369,
    0x0065636e, 0x00070006, 0x0000001b, 0x00000003, 0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e,
    0x00030005, 0x0000001d, 0x00000000, 0x00040047, 0x0000000c, 0x0000000b, 0x0000002a, 0x00030047,
    0x0000001b, 0x00000002, 0x00050048, 0x0000001b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
    0x0000001b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001b, 0x00000002, 0x0000000b,
    0x00000003, 0x00050048, 0x0000001b, 0x00000003, 0x0000000b, 0x00000004, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007,
    0x00000006, 0x00000002, 0x00040020, 0x00000008, 0x00000007, 0x00000007, 0x00040015, 0x0000000a,
    0x00000020, 0x00000001, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
    0x0000000c, 0x00000001, 0x0004002b, 0x0000000a, 0x0000000e, 0x00000001, 0x0004002b, 0x0000000a,
    0x00000010, 0x00000002, 0x00040017, 0x00000017, 0x00000006, 0x00000004, 0x00040015, 0x00000018,
    0x00000020, 0x00000000, 0x0004002b, 0x00000018, 0x00000019, 0x00000001, 0x0004001c, 0x0000001a,
    0x00000006, 0x00000019, 0x0006001e, 0x0000001b, 0x00000017, 0x00000006, 0x0000001a, 0x0000001a,
    0x00040020, 0x0000001c, 0x00000003, 0x0000001b, 0x0004003b, 0x0000001c, 0x0000001d, 0x00000003,
    0x0004002b, 0x0000000a, 0x0000001e, 0x00000000, 0x0004002b, 0x00000006, 0x00000020, 0x40000000,
    0x0004002b, 0x00000006, 0x00000022, 0x3f800000, 0x0004002b, 0x00000006, 0x00000025, 0x00000000,
    0x00040020, 0x00000029, 0x00000003, 0x00000017, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x0004003b, 0x00000008, 0x00000009, 0x00000007, 0x0004003d,
    0x0000000a, 0x0000000d, 0x0000000c, 0x000500c4, 0x0000000a, 0x0000000f, 0x0000000d, 0x0000000e,
    0x000500c7, 0x0000000a, 0x00000011, 0x0000000f, 0x00000010, 0x0004006f, 0x00000006, 0x00000012,
    0x00000011, 0x0004003d, 0x0000000a, 0x00000013, 0x0000000c, 0x000500c7, 0x0000000a, 0x00000014,
    0x00000013, 0x00000010, 0x0004006f, 0x00000006, 0x00000015, 0x00000014, 0x00050050, 0x00000007,
    0x00000016, 0x00000012, 0x00000015, 0x0003003e, 0x00000009, 0x00000016, 0x0004003d, 0x00000007,
    0x0000001f, 0x00000009, 0x0005008e, 0x00000007, 0x00000021, 0x0000001f, 0x00000020, 0x00050050,
    0x00000007, 0x00000023, 0x00000022, 0x00000022, 0x00050083, 0x00000007, 0x00000024, 0x00000021,
    0x00000023, 0x00050051, 0x00000006, 0x00000026, 0x00000024, 0x00000000, 0x00050051, 0x00000006,
    0x00000027, 0x00000024, 0x00000001, 0x00070050, 0x00000017, 0x00000028, 0x00000026, 0x00000027,
    0x00000025, 0x00000022, 0x00050041, 0x00000029, 0x0000002a, 0x0000001d, 0x0000001e, 0x0003003e,
    0x0000002a, 0x00000028, 0x000100fd, 0x00010038
};
const uint32_t kFragmentSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000014, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0008000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x0000000f,
    0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00040005, 0x00000009, 0x3074756f, 0x00000000, 0x00040005, 0x0000000d,
    0x3174756f, 0x00000000, 0x00040005, 0x0000000f, 0x3274756f, 0x00000000, 0x00040047, 0x00000009,
    0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x0000001e, 0x00000001, 0x00040047, 0x0000000f,
    0x0000001e, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x0004002b, 0x00000006,
    0x0000000a, 0x3f800000, 0x0004002b, 0x00000006, 0x0000000b, 0x00000000, 0x0007002c, 0x00000007,
    0x0000000c, 0x0000000a, 0x0000000b, 0x0000000b, 0x0000000a, 0x0004003b, 0x00000008, 0x0000000d,
    0x00000003, 0x0007002c, 0x00000007, 0x0000000e, 0x0000000b, 0x0000000a, 0x0000000b, 0x0000000a,
    0x0004003b, 0x00000008, 0x0000000f, 0x00000003, 0x0004002b, 0x00000006, 0x00000010, 0x3e800000,
    0x0004002b, 0x00000006, 0x00000011, 0x3f000000, 0x0004002b, 0x00000006, 0x00000012, 0x3f400000,
    0x0007002c, 0x00000007, 0x00000013, 0x00000010, 0x00000011, 0x00000012, 0x0000000a, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0003003e, 0x00000009,
    0x0000000c, 0x0003003e, 0x0000000d, 0x0000000e, 0x0003003e, 0x0000000f, 0x00000013, 0x000100fd,
    0x00010038
};
#endif

} // namespace

TEST(MultipleRenderTargets, ClearsEachAttachment) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";
    Target a = makeTarget(device, PixelFormat::rgba8unorm);
    Target b = makeTarget(device, PixelFormat::rgba16float);
    Target c = makeTarget(device, PixelFormat::rgba8unorm);
    if (!a.view || !b.view || !c.view) GTEST_SKIP() << "Could not create render targets";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    BeginRenderPassDescriptor desc{};
    desc.colorAttachments = {attachment(a, LoadOp::clear, 1, 0, 0, 1),
                             attachment(b, LoadOp::clear, 0, 2, 0, 1),
                             attachment(c, LoadOp::clear, 0, 0, 1, 0.5f)};
    auto pass = encoder->beginRenderPass(desc);
    ASSERT_NE(pass, nullptr);
    pass->end();
    submitAndWait(device, encoder);

    expectPixel(a, 1, 0, 0, 1, "attachment 0");
    expectPixel(b, 0, 2, 0, 1, "attachment 1");
    expectPixel(c, 0, 0, 1, 0.5f, "attachment 2");
}

TEST(MultipleRenderTargets, LoadKeepsEachAttachment) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";
    Target a = makeTarget(device, PixelFormat::rgba8unorm);
    Target b = makeTarget(device, PixelFormat::rgba8unorm);
    if (!a.view || !b.view) GTEST_SKIP() << "Could not create render targets";

    for (int frame = 0; frame < 2; frame++) {
        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        BeginRenderPassDescriptor desc{};
        const LoadOp op = frame == 0 ? LoadOp::clear : LoadOp::load;
        desc.colorAttachments = {attachment(a, op, 0, 1, 0, 1), attachment(b, op, 0, 0, 1, 1)};
        auto pass = encoder->beginRenderPass(desc);
        ASSERT_NE(pass, nullptr);
        pass->end();
        submitAndWait(device, encoder);
    }
    expectPixel(a, 0, 1, 0, 1, "attachment 0");
    expectPixel(b, 0, 0, 1, 1, "attachment 1");
}

#if defined(__ANDROID__) || (defined(__linux__) && !defined(__EMSCRIPTEN__))
TEST(MultipleRenderTargets, DrawWritesEveryTarget) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";
    Target a = makeTarget(device, PixelFormat::rgba8unorm);
    Target b = makeTarget(device, PixelFormat::rgba16float);
    Target c = makeTarget(device, PixelFormat::rgba8unorm);
    if (!a.view || !b.view || !c.view) GTEST_SKIP() << "Could not create render targets";

    auto vs = device->createShaderModule(reinterpret_cast<const uint8_t*>(kVertexSpirv), sizeof(kVertexSpirv));
    auto fs = device->createShaderModule(reinterpret_cast<const uint8_t*>(kFragmentSpirv), sizeof(kFragmentSpirv));
    ASSERT_NE(vs, nullptr);
    ASSERT_NE(fs, nullptr);

    FragmentDescriptor fragment{};
    fragment.module = fs;
    fragment.entryPoint = "main";
    fragment.targets = {ColorState{PixelFormat::rgba8unorm, ColorWrite::all},
                        ColorState{PixelFormat::rgba16float, ColorWrite::all},
                        ColorState{PixelFormat::rgba8unorm, ColorWrite::all}};
    RenderPipelineDescriptor desc{};
    desc.vertex.module = vs;
    desc.vertex.entryPoint = "main";
    desc.fragment = fragment;
    desc.topology = PrimitiveTopology::triangleList;
    desc.cullMode = CullMode::none;
    desc.frontFace = FrontFace::ccw;
    auto pipeline = device->createRenderPipeline(desc);
    ASSERT_NE(pipeline, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    BeginRenderPassDescriptor pass{};
    pass.colorAttachments = {attachment(a, LoadOp::clear, 0, 0, 0, 0),
                             attachment(b, LoadOp::clear, 0, 0, 0, 0),
                             attachment(c, LoadOp::clear, 0, 0, 0, 0)};
    auto encoderPass = encoder->beginRenderPass(pass);
    ASSERT_NE(encoderPass, nullptr);
    encoderPass->setPipeline(pipeline);
    encoderPass->setViewport(0, 0, kSize, kSize, 0, 1);
    encoderPass->setScissorRect(0, 0, kSize, kSize);
    encoderPass->draw(3, 1, 0, 0);
    encoderPass->end();
    submitAndWait(device, encoder);

    expectPixel(a, 1, 0, 0, 1, "target 0");
    expectPixel(b, 0, 1, 0, 1, "target 1");
    expectPixel(c, 0.25f, 0.5f, 0.75f, 1, "target 2");
}
#endif
