// Platform integration tests for TemporalUpscaler (MetalFX on Metal).
//
// Skip where the device doesn't support temporal upscaling (other backends,
// older systems). Where it does: a uniform frame upscaled to twice its size
// stays that colour everywhere, and invalid descriptors are refused.

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/temporal_upscaler.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>

using namespace systems::leal::campello_gpu;

namespace {

std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__) || defined(__APPLE__) || defined(_WIN32) || defined(__linux__) || defined(__EMSCRIPTEN__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

uint16_t toHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000;
    const int exponent = int((x >> 23) & 0xff) - 127 + 15;
    if (exponent <= 0) return uint16_t(sign);
    return uint16_t(sign | (uint32_t(exponent) << 10) | ((x >> 13) & 0x3ff));
}

float fromHalf(uint16_t h) {
    const uint32_t exponent = (h >> 10) & 31, mantissa = h & 1023;
    const float v = exponent == 0 ? std::ldexp(float(mantissa), -24) : std::ldexp(float(mantissa | 1024), int(exponent) - 25);
    return (h & 0x8000) ? -v : v;
}

} // namespace

TEST(TemporalUpscaler, RefusesInvalidDescriptors) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No GPU device";
    if (!device->supportsTemporalUpscaler()) {
        TemporalUpscalerDescriptor d;
        d.inputWidth = d.inputHeight = 32;
        d.outputWidth = d.outputHeight = 64;
        EXPECT_EQ(device->createTemporalUpscaler(d), nullptr) << "unsupported: none created";
        EXPECT_EQ(device->getTemporalUpscalerMaxScale(), 0.0f);
        GTEST_SKIP() << "No temporal upscaling on this device";
    }
    TemporalUpscalerDescriptor empty;
    EXPECT_EQ(device->createTemporalUpscaler(empty), nullptr) << "zero sizes";
    const float maxScale = device->getTemporalUpscalerMaxScale();
    EXPECT_GE(maxScale, 1.0f);
    TemporalUpscalerDescriptor smaller;
    smaller.inputWidth = smaller.inputHeight = 64;
    smaller.outputWidth = smaller.outputHeight = 32;
    EXPECT_EQ(device->createTemporalUpscaler(smaller), nullptr) << "output smaller than the input";
    // Past the limit: refused (MetalFX itself would assert).
    TemporalUpscalerDescriptor tooFar;
    tooFar.inputWidth = tooFar.inputHeight = 16;
    tooFar.outputWidth = tooFar.outputHeight = uint32_t(16.0f * maxScale) + 16;
    EXPECT_EQ(device->createTemporalUpscaler(tooFar), nullptr) << "past the maximum scale";
    TemporalUpscalerDescriptor atLimit;
    atLimit.inputWidth = atLimit.inputHeight = 32;
    atLimit.outputWidth = atLimit.outputHeight = uint32_t(32.0f * maxScale);
    EXPECT_NE(device->createTemporalUpscaler(atLimit), nullptr) << "at the maximum scale";
}

TEST(TemporalUpscaler, UpscalesAUniformFrame) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No GPU device";
    if (!device->supportsTemporalUpscaler()) GTEST_SKIP() << "No temporal upscaling on this device";
    constexpr uint32_t in = 32, out = 64;
    TemporalUpscalerDescriptor d;
    d.inputWidth = d.inputHeight = in;
    d.outputWidth = d.outputHeight = out;
    auto upscaler = device->createTemporalUpscaler(d);
    ASSERT_NE(upscaler, nullptr);
    EXPECT_EQ(upscaler->getDescriptor().outputWidth, out);
    EXPECT_NE(static_cast<uint32_t>(upscaler->outputUsage()), 0u);

    auto color = device->createTexture(TextureType::tt2d, PixelFormat::rgba16float, in, in, 1, 1, 1,
                                       TextureUsage::textureBinding | TextureUsage::copyDst);
    auto motion = device->createTexture(TextureType::tt2d, PixelFormat::rg16float, in, in, 1, 1, 1,
                                        TextureUsage::textureBinding | TextureUsage::copyDst);
    auto depth = device->createTexture(TextureType::tt2d, PixelFormat::depth32float, in, in, 1, 1, 1,
                                       TextureUsage::textureBinding | TextureUsage::renderTarget);
    auto output = device->createTexture(TextureType::tt2d, PixelFormat::rgba16float, out, out, 1, 1, 1,
                                        upscaler->outputUsage() | TextureUsage::copySrc);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(motion, nullptr);
    ASSERT_NE(depth, nullptr);
    ASSERT_NE(output, nullptr);
    std::vector<uint16_t> pixels(size_t(in) * in * 4, toHalf(0.5f));
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = toHalf(1.0f);
    ASSERT_TRUE(color->upload(0, pixels.size() * 2, pixels.data()));
    std::vector<uint16_t> still(size_t(in) * in * 2, 0);
    ASSERT_TRUE(motion->upload(0, still.size() * 2, still.data()));

    for (int frame = 0; frame < 4; ++frame) {
        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        // Depth cleared to the far plane (nothing in front).
        BeginRenderPassDescriptor rp{};
        DepthStencilAttachment ds{};
        ds.view = depth->createView(PixelFormat::depth32float, 1, Aspect::depthOnly);
        ds.depthClearValue = 1.0f;
        ds.depthLoadOp = LoadOp::clear;
        ds.depthStoreOp = StoreOp::store;
        ds.stencilLoadOp = LoadOp::clear;
        ds.stencilStoreOp = StoreOp::discard;
        rp.depthStencilAttachment = ds;
        if (auto pass = encoder->beginRenderPass(rp)) pass->end();
        TemporalUpscaleInputs inputs;
        inputs.color = color;
        inputs.depth = depth;
        inputs.motion = motion;
        inputs.output = output;
        inputs.reset = frame == 0;
        encoder->encodeTemporalUpscale(upscaler, inputs);
        auto fence = device->createFence();
        device->submit(encoder->finish(), fence);
        ASSERT_TRUE(fence->wait());
    }
    std::vector<uint16_t> result(size_t(out) * out * 4);
    ASSERT_TRUE(output->download(0, 0, result.data(), result.size() * 2));
    float lo = 1e9f, hi = -1e9f;
    for (size_t i = 0; i < result.size(); i += 4) {
        lo = std::min(lo, fromHalf(result[i]));
        hi = std::max(hi, fromHalf(result[i]));
    }
    EXPECT_NEAR(lo, 0.5f, 0.02f) << "the frame's colour everywhere";
    EXPECT_NEAR(hi, 0.5f, 0.02f);
}
