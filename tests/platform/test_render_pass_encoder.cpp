// Platform integration tests for RenderPassEncoder.
//
// Tests use an off-screen render-target texture so no window handle is needed.
// Commands are recorded and the command buffer is closed via finish() but
// never submitted — GPU execution is not the concern here; we verify the
// command-recording API does not crash and returns expected objects.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/constants/shader_stage.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__)
    return Device::createDefaultDevice(nullptr);
#elif defined(__APPLE__)
    return Device::createDefaultDevice(nullptr);
#elif defined(_WIN32)
    return Device::createDefaultDevice(nullptr);
#elif defined(__linux__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

// Create an off-screen RGBA render-target texture and return its view.
// outTexture must be kept alive by the caller for as long as the view is used
// -- a TextureView does not itself keep the source Texture alive (same
// contract as e.g. Vulkan's VkImageView/VkImage), so letting outTexture go
// out of scope while the view is still in use is a use-after-free.
static std::shared_ptr<TextureView> makeRTView(std::shared_ptr<Device>& device,
                                               std::shared_ptr<Texture>& outTexture,
                                               uint32_t w = 64, uint32_t h = 64) {
    outTexture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        w, h, 1, 1, 1, TextureUsage::renderTarget);
    if (!outTexture) return nullptr;
    return outTexture->createView(PixelFormat::rgba8unorm, 1);
}

// Build a minimal color-only BeginRenderPassDescriptor.
static BeginRenderPassDescriptor makeColorPassDesc(
    std::shared_ptr<TextureView> view,
    LoadOp loadOp = LoadOp::load)
{
    ColorAttachment ca{};
    ca.view         = view;
    ca.loadOp       = loadOp;
    ca.storeOp      = StoreOp::store;
    ca.clearValue[0] = 0.1f;
    ca.clearValue[1] = 0.2f;
    ca.clearValue[2] = 0.3f;
    ca.clearValue[3] = 1.0f;

    BeginRenderPassDescriptor desc{};
    desc.colorAttachments = { ca };
    return desc;
}

// ---------------------------------------------------------------------------
// beginRenderPass
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, BeginWithNoAttachmentsReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    BeginRenderPassDescriptor desc{};  // no color or depth attachments
    auto pass = encoder->beginRenderPass(desc);
    EXPECT_NE(pass, nullptr);
}

TEST(RenderPassEncoder, BeginWithLoadOpLoadReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    std::shared_ptr<Texture> tex;
    auto view = makeRTView(device, tex);
    if (!view) GTEST_SKIP() << "Could not create render-target view";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(makeColorPassDesc(view, LoadOp::load));
    EXPECT_NE(pass, nullptr);
}

TEST(RenderPassEncoder, BeginWithLoadOpClearReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    std::shared_ptr<Texture> tex;
    auto view = makeRTView(device, tex);
    if (!view) GTEST_SKIP() << "Could not create render-target view";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(makeColorPassDesc(view, LoadOp::clear));
    EXPECT_NE(pass, nullptr);
}

// ---------------------------------------------------------------------------
// setViewport / setScissorRect
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, SetViewportDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setViewport(0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f));
}

TEST(RenderPassEncoder, SetViewportWithNonZeroOriginDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setViewport(10.0f, 20.0f, 100.0f, 80.0f, 0.1f, 0.9f));
}

TEST(RenderPassEncoder, SetScissorRectDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setScissorRect(0.0f, 0.0f, 64.0f, 64.0f));
}

TEST(RenderPassEncoder, SetViewportAndScissorTogetherDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    pass->setViewport(0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f);
    EXPECT_NO_THROW(pass->setScissorRect(16.0f, 16.0f, 96.0f, 96.0f));
}

// ---------------------------------------------------------------------------
// setStencilReference
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, SetStencilReferenceDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setStencilReference(0xFF));
}

TEST(RenderPassEncoder, SetStencilReferenceZeroDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setStencilReference(0));
}

// ---------------------------------------------------------------------------
// setVertexBuffer / setIndexBuffer
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, SetVertexBufferDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 256;
    auto vbuf = device->createBuffer(size, BufferUsage::vertex);
    ASSERT_NE(vbuf, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setVertexBuffer(0, vbuf, 0, -1));
}

TEST(RenderPassEncoder, SetVertexBufferWithOffsetDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 512;
    auto vbuf = device->createBuffer(size, BufferUsage::vertex);
    ASSERT_NE(vbuf, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setVertexBuffer(0, vbuf, 64, -1));
}

TEST(RenderPassEncoder, SetIndexBufferUint16DoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 256;
    auto ibuf = device->createBuffer(size, BufferUsage::index);
    ASSERT_NE(ibuf, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setIndexBuffer(ibuf, IndexFormat::uint16, 0, -1));
}

TEST(RenderPassEncoder, SetIndexBufferUint32DoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 256;
    auto ibuf = device->createBuffer(size, BufferUsage::index);
    ASSERT_NE(ibuf, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setIndexBuffer(ibuf, IndexFormat::uint32, 0, -1));
}

// ---------------------------------------------------------------------------
// draw / drawIndexed
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, DrawDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    // No PSO set — command is recorded but not validated until submission.
    EXPECT_NO_THROW(pass->draw(3, 1, 0, 0));
}

TEST(RenderPassEncoder, DrawMultipleInstancesDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->draw(6, 4, 0, 0));
}

TEST(RenderPassEncoder, DrawIndexedDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t ibufSize = 256;
    auto ibuf = device->createBuffer(ibufSize, BufferUsage::index);
    ASSERT_NE(ibuf, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    pass->setIndexBuffer(ibuf, IndexFormat::uint16, 0, -1);
    EXPECT_NO_THROW(pass->drawIndexed(6, 1, 0, 0, 0));
}

// ---------------------------------------------------------------------------
// end + finish
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, EndAndFinishReturnCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);
    pass->end();

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(RenderPassEncoder, FullPassWithColorAttachmentProducesCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    std::shared_ptr<Texture> tex;
    auto view = makeRTView(device, tex);
    if (!view) GTEST_SKIP() << "Could not create render-target view";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(makeColorPassDesc(view, LoadOp::clear));
    ASSERT_NE(pass, nullptr);

    pass->setViewport(0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f);
    pass->setScissorRect(0.0f, 0.0f, 64.0f, 64.0f);
    pass->end();

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

// ---------------------------------------------------------------------------
// Offscreen subresource layout transitions.
//
// Regression coverage for beginRenderPass()/end(): their entry/exit image
// barriers used to hardcode subresourceRange = (mip 0, layer 0) regardless
// of which subresource the attachment view actually targeted. Rendering into
// any other subresource (a non-zero array layer, a non-zero mip level, one
// face of a cube map, ...) left that subresource's layout untransitioned,
// which read back as GPU-cache-incoherent content on affected drivers. See
// Texture::createView()'s `mipLevelCount` doc comment.
// ---------------------------------------------------------------------------

TEST(RenderPassEncoder, ClearingNonZeroArrayLayerProducesCorrectContent) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 32, H = 32, LAYERS = 4;
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, LAYERS, 1, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::renderTarget) |
            static_cast<int>(TextureUsage::copySrc)));
    ASSERT_NE(tex, nullptr);

    // A view onto array layer 2 only -- the shape required for a
    // render-pass attachment view into a non-default layer.
    constexpr uint32_t TARGET_LAYER = 2;
    auto view = tex->createView(
        PixelFormat::rgba8unorm, /*arrayLayerCount=*/1, Aspect::all,
        /*baseArrayLayer=*/TARGET_LAYER, /*baseMipLevel=*/0,
        TextureType::tt2d, /*mipLevelCount=*/1);
    ASSERT_NE(view, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(makeColorPassDesc(view, LoadOp::clear));
    ASSERT_NE(pass, nullptr);
    pass->end();

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);

    auto fence = device->createFence();
    device->submit(cmdBuf, fence);
    fence->wait();

    std::vector<uint8_t> readback(W * H * 4, 0);
    ASSERT_TRUE(tex->download(/*mipLevel=*/0, TARGET_LAYER, readback.data(), readback.size()));

    // makeColorPassDesc() clears to (0.1, 0.2, 0.3, 1.0) -> (25, 51, 76, 255)
    // in rgba8unorm. If the barriers had targeted (mip 0, layer 0) instead of
    // this view's actual (mip 0, layer 2), layer 2 would never have been
    // transitioned into COLOR_ATTACHMENT_OPTIMAL for the clear and this
    // readback would not reliably show the cleared color.
    for (uint32_t px = 0; px < W * H; ++px) {
        ASSERT_EQ(readback[px * 4 + 0], 25)  << "pixel " << px;
        ASSERT_EQ(readback[px * 4 + 1], 51)  << "pixel " << px;
        ASSERT_EQ(readback[px * 4 + 2], 76)  << "pixel " << px;
        ASSERT_EQ(readback[px * 4 + 3], 255) << "pixel " << px;
    }
}

TEST(RenderPassEncoder, ClearingNonZeroMipLevelProducesCorrectContent) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 32, H = 32, MIPS = 3;
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, MIPS, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::renderTarget) |
            static_cast<int>(TextureUsage::copySrc)));
    ASSERT_NE(tex, nullptr);

    // A view onto mip level 1 only.
    constexpr uint32_t TARGET_MIP = 1;
    auto view = tex->createView(
        PixelFormat::rgba8unorm, /*arrayLayerCount=*/1, Aspect::all,
        /*baseArrayLayer=*/0, /*baseMipLevel=*/TARGET_MIP,
        TextureType::tt2d, /*mipLevelCount=*/1);
    ASSERT_NE(view, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(makeColorPassDesc(view, LoadOp::clear));
    ASSERT_NE(pass, nullptr);
    pass->end();

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);

    auto fence = device->createFence();
    device->submit(cmdBuf, fence);
    fence->wait();

    constexpr uint32_t MIP1_W = W / 2, MIP1_H = H / 2;
    std::vector<uint8_t> readback(MIP1_W * MIP1_H * 4, 0);
    ASSERT_TRUE(tex->download(TARGET_MIP, /*arrayLayer=*/0, readback.data(), readback.size()));

    // Same rationale as ClearingNonZeroArrayLayerProducesCorrectContent, but
    // for a non-zero mip level rather than a non-zero array layer.
    for (uint32_t px = 0; px < MIP1_W * MIP1_H; ++px) {
        ASSERT_EQ(readback[px * 4 + 0], 25)  << "pixel " << px;
        ASSERT_EQ(readback[px * 4 + 1], 51)  << "pixel " << px;
        ASSERT_EQ(readback[px * 4 + 2], 76)  << "pixel " << px;
        ASSERT_EQ(readback[px * 4 + 3], 255) << "pixel " << px;
    }
}

// ---------------------------------------------------------------------------
// setBindGroup
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// setPushConstants
// ---------------------------------------------------------------------------
//
// Real render pipelines need backend-specific shader bytecode (MSL/SPIR-V/
// HLSL) that this cross-platform suite cannot compile, so — matching the
// Draw*DoesNotCrash tests above — these exercise setPushConstants() without
// a bound pipeline. On Vulkan this hits the `pipelineLayout == VK_NULL_HANDLE`
// guard in RenderPassEncoder::setPushConstants(); on Metal/D3D12/WebGPU it
// hits their no-op stubs. Either way, the call must never crash.

TEST(RenderPassEncoder, SetPushConstantsDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_NO_THROW(pass->setPushConstants(ShaderStage::vertex, 0, sizeof(values), values));
}

TEST(RenderPassEncoder, SetPushConstantsWithNonZeroOffsetDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    uint32_t value = 42;
    EXPECT_NO_THROW(pass->setPushConstants(ShaderStage::fragment, 16, sizeof(value), &value));
}

TEST(RenderPassEncoder, SetPushConstantsWithNullDataDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    // Guarded in every backend implementation: null data or zero size is a no-op.
    EXPECT_NO_THROW(pass->setPushConstants(ShaderStage::vertex, 0, 0, nullptr));
}

TEST(RenderPassEncoder, SetBindGroupWithNullDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    // Passing a null BindGroup; the implementation guards against this.
    EXPECT_NO_THROW(pass->setBindGroup(0, nullptr, {}, 0, 0));
}

TEST(RenderPassEncoder, SetBindGroupWithEmptyGroupDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    BindGroupDescriptor bgDesc{};
    bgDesc.layout = layout;
    auto bindGroup = device->createBindGroup(bgDesc);
    ASSERT_NE(bindGroup, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setBindGroup(0, bindGroup, {}, 0, 0));
}

TEST(RenderPassEncoder, SetBindGroupWithTextureDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Create a texture for binding
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    // Create bind group layout with texture entry
    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject texEntry{};
    texEntry.binding = 0;
    texEntry.visibility = ShaderStage::fragment;
    texEntry.type = EntryObjectType::texture;
    texEntry.data.texture.sampleType = EntryObjectTextureType::ttFloat;
    texEntry.data.texture.viewDimension = TextureType::tt2d;
    texEntry.data.texture.multisampled = false;
    layoutDesc.entries.push_back(texEntry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    // Create bind group with texture entry
    BindGroupDescriptor bgDesc{};
    bgDesc.layout = layout;
    bgDesc.entries = { {0, texture} };
    auto bindGroup = device->createBindGroup(bgDesc);
    ASSERT_NE(bindGroup, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    // This would have crashed before the fix due to incorrect texture handle casting
    EXPECT_NO_THROW(pass->setBindGroup(0, bindGroup, {}, 0, 0));
}

TEST(RenderPassEncoder, SetBindGroupWithSamplerDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Create a sampler for binding
    SamplerDescriptor sampDesc{};
    auto sampler = device->createSampler(sampDesc);
    ASSERT_NE(sampler, nullptr);

    // Create bind group layout with sampler entry
    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject sampEntry{};
    sampEntry.binding = 1;
    sampEntry.visibility = ShaderStage::fragment;
    sampEntry.type = EntryObjectType::sampler;
    sampEntry.data.sampler.type = EntryObjectSamplerType::filtering;
    layoutDesc.entries.push_back(sampEntry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    // Create bind group with sampler entry
    BindGroupDescriptor bgDesc{};
    bgDesc.layout = layout;
    bgDesc.entries = { {1, sampler} };
    auto bindGroup = device->createBindGroup(bgDesc);
    ASSERT_NE(bindGroup, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setBindGroup(0, bindGroup, {}, 0, 0));
}

TEST(RenderPassEncoder, SetBindGroupWithBufferDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Create a uniform buffer for binding
    auto buffer = device->createBuffer(256, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);

    // Create bind group layout with buffer entry
    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject bufferEntry{};
    bufferEntry.binding = 2;
    bufferEntry.visibility = ShaderStage::vertex;
    bufferEntry.type = EntryObjectType::buffer;
    bufferEntry.data.buffer.type = EntryObjectBufferType::uniform;
    bufferEntry.data.buffer.hasDinamicOffaset = false;
    bufferEntry.data.buffer.minBindingSize = 256;
    layoutDesc.entries.push_back(bufferEntry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    // Create bind group with buffer entry
    BindGroupDescriptor bgDesc{};
    bgDesc.layout = layout;
    bgDesc.entries = { {2, BufferBinding{buffer, 0, 256}} };
    auto bindGroup = device->createBindGroup(bgDesc);
    ASSERT_NE(bindGroup, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setBindGroup(0, bindGroup, {}, 0, 0));
}

TEST(RenderPassEncoder, SetBindGroupWithMixedResourcesDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Create resources
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    SamplerDescriptor sampDesc{};
    auto sampler = device->createSampler(sampDesc);
    ASSERT_NE(sampler, nullptr);

    auto buffer = device->createBuffer(256, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);

    // Create bind group layout with multiple entries
    BindGroupLayoutDescriptor layoutDesc{};
    // Texture at binding 0
    EntryObject texEntry{};
    texEntry.binding = 0;
    texEntry.visibility = ShaderStage::fragment;
    texEntry.type = EntryObjectType::texture;
    texEntry.data.texture.sampleType = EntryObjectTextureType::ttFloat;
    texEntry.data.texture.viewDimension = TextureType::tt2d;
    texEntry.data.texture.multisampled = false;
    layoutDesc.entries.push_back(texEntry);
    // Sampler at binding 1
    EntryObject sampEntry{};
    sampEntry.binding = 1;
    sampEntry.visibility = ShaderStage::fragment;
    sampEntry.type = EntryObjectType::sampler;
    sampEntry.data.sampler.type = EntryObjectSamplerType::filtering;
    layoutDesc.entries.push_back(sampEntry);
    // Buffer at binding 2
    EntryObject bufferEntry{};
    bufferEntry.binding = 2;
    bufferEntry.visibility = ShaderStage::vertex;
    bufferEntry.type = EntryObjectType::buffer;
    bufferEntry.data.buffer.type = EntryObjectBufferType::uniform;
    bufferEntry.data.buffer.hasDinamicOffaset = false;
    bufferEntry.data.buffer.minBindingSize = 256;
    layoutDesc.entries.push_back(bufferEntry);

    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    // Create bind group with all resource types
    BindGroupDescriptor bgDesc{};
    bgDesc.layout = layout;
    bgDesc.entries = {
        {0, texture},
        {1, sampler},
        {2, BufferBinding{buffer, 0, 256}}
    };
    auto bindGroup = device->createBindGroup(bgDesc);
    ASSERT_NE(bindGroup, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginRenderPass(BeginRenderPassDescriptor{});
    ASSERT_NE(pass, nullptr);

    // This test exercises all resource binding paths (texture, sampler, buffer)
    EXPECT_NO_THROW(pass->setBindGroup(0, bindGroup, {}, 0, 0));
}
