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
static std::shared_ptr<TextureView> makeRTView(std::shared_ptr<Device>& device,
                                               uint32_t w = 64, uint32_t h = 64) {
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        w, h, 1, 1, 1, TextureUsage::renderTarget);
    if (!tex) return nullptr;
    return tex->createView(PixelFormat::rgba8unorm, 1);
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

    auto view = makeRTView(device);
    if (!view) GTEST_SKIP() << "Could not create render-target view";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginRenderPass(makeColorPassDesc(view, LoadOp::load));
    EXPECT_NE(pass, nullptr);
}

TEST(RenderPassEncoder, BeginWithLoadOpClearReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto view = makeRTView(device);
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

    auto view = makeRTView(device);
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
// setBindGroup
// ---------------------------------------------------------------------------

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
