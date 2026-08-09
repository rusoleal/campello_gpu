// Platform integration tests for BindGroupLayout and BindGroup.
//
// test_device.cpp already covers the minimal "empty layout / empty bind
// group returns non-null" cases. These tests exercise the actual resource
// binding paths: every EntryObjectType (buffer/texture/sampler), every
// buffer sub-type, multiple entries in one group/layout, dynamic-offset
// buffers, the `persistent` bind group flag, and sub-range buffer bindings.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
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

static std::shared_ptr<Sampler> makeSampler(std::shared_ptr<Device>& device) {
    SamplerDescriptor desc{};
    desc.minFilter = FilterMode::fmLinear;
    desc.magFilter = FilterMode::fmLinear;
    return device->createSampler(desc);
}

// ---------------------------------------------------------------------------
// BindGroupLayout — one entry per resource category.
// ---------------------------------------------------------------------------

TEST(BindGroupLayout, CreateWithUniformBufferEntryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor desc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::vertex;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    desc.entries.push_back(entry);

    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(BindGroupLayout, CreateWithStorageBufferEntryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor desc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::compute;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::storage;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    desc.entries.push_back(entry);

    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(BindGroupLayout, CreateWithReadOnlyStorageBufferEntryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor desc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::compute;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::readOnlyStorage;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    desc.entries.push_back(entry);

    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(BindGroupLayout, CreateWithDynamicOffsetBufferEntryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor desc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::vertex;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = true;
    entry.data.buffer.minBindingSize    = 0;
    desc.entries.push_back(entry);

    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(BindGroupLayout, CreateWithTextureEntryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor desc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::texture;
    entry.data.texture.multisampled  = false;
    entry.data.texture.sampleType    = EntryObjectTextureType::ttFloat;
    entry.data.texture.viewDimension = TextureType::tt2d;
    desc.entries.push_back(entry);

    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(BindGroupLayout, CreateWithSamplerEntryEveryTypeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const EntryObjectSamplerType types[] = {
        EntryObjectSamplerType::filtering,
        EntryObjectSamplerType::nonFiltering,
        EntryObjectSamplerType::comparison,
    };

    for (auto samplerType : types) {
        BindGroupLayoutDescriptor desc{};
        EntryObject entry{};
        entry.binding    = 0;
        entry.visibility = ShaderStage::fragment;
        entry.type       = EntryObjectType::sampler;
        entry.data.sampler.type = samplerType;
        desc.entries.push_back(entry);

        auto layout = device->createBindGroupLayout(desc);
        EXPECT_NE(layout, nullptr) << "samplerType = " << static_cast<int>(samplerType);
    }
}

TEST(BindGroupLayout, CreateWithMultipleEntriesReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor desc{};

    EntryObject bufEntry{};
    bufEntry.binding    = 0;
    bufEntry.visibility = ShaderStage::vertex;
    bufEntry.type       = EntryObjectType::buffer;
    bufEntry.data.buffer.type              = EntryObjectBufferType::uniform;
    bufEntry.data.buffer.hasDinamicOffaset = false;
    bufEntry.data.buffer.minBindingSize    = 0;
    desc.entries.push_back(bufEntry);

    EntryObject texEntry{};
    texEntry.binding    = 1;
    texEntry.visibility = ShaderStage::fragment;
    texEntry.type       = EntryObjectType::texture;
    texEntry.data.texture.multisampled  = false;
    texEntry.data.texture.sampleType    = EntryObjectTextureType::ttFloat;
    texEntry.data.texture.viewDimension = TextureType::tt2d;
    desc.entries.push_back(texEntry);

    EntryObject sampEntry{};
    sampEntry.binding    = 2;
    sampEntry.visibility = ShaderStage::fragment;
    sampEntry.type       = EntryObjectType::sampler;
    sampEntry.data.sampler.type = EntryObjectSamplerType::filtering;
    desc.entries.push_back(sampEntry);

    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

// ---------------------------------------------------------------------------
// BindGroup — concrete resource bindings.
// ---------------------------------------------------------------------------

TEST(BindGroup, CreateWithBufferBindingReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::vertex;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto buffer = device->createBuffer(256, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = { { 0, BufferBinding{ buffer, 0, 256 } } };

    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

TEST(BindGroup, CreateWithPartialBufferRangeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::vertex;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto buffer = device->createBuffer(1024, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    // Bind only the second half of the buffer.
    desc.entries = { { 0, BufferBinding{ buffer, 512, 512 } } };

    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

TEST(BindGroup, CreateWithTextureBindingReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::texture;
    entry.data.texture.multisampled  = false;
    entry.data.texture.sampleType    = EntryObjectTextureType::ttFloat;
    entry.data.texture.viewDimension = TextureType::tt2d;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = { { 0, texture } };

    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

// Regression test: binding a cube Texture directly (not through an explicit
// TextureView from Texture::createView()) exercises the auto-created
// "default view" every Texture carries — Device::createTexture()'s viewType
// selection used to fall through to VK_IMAGE_VIEW_TYPE_2D for TextureType::
// ttCube (only tt3d/tt1d were handled), which this call path never caught
// (createBindGroup() doesn't itself validate the resource's dimension
// against the layout's declared viewDimension). The real symptom only
// showed up under Vulkan validation layers at actual draw time:
// VUID-vkCmdDrawIndexed-viewType-07752, "VkImageViewType is
// VK_IMAGE_VIEW_TYPE_2D but the OpTypeImage has (Dim = Cube)". This test
// can't assert the underlying VkImageViewType directly (no public API
// exposes it), but keeps the exact code path — a raw cube Texture bound
// straight into a bind group — exercised, so anyone running this suite
// under validation layers (as this bug was originally found) still catches
// a regression here.
TEST(BindGroup, CreateWithCubeTextureBindingReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::texture;
    entry.data.texture.multisampled  = false;
    entry.data.texture.sampleType    = EntryObjectTextureType::ttFloat;
    entry.data.texture.viewDimension = TextureType::ttCube;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto texture = device->createTexture(
        TextureType::ttCube, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getDimension(), TextureType::ttCube);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = { { 0, texture } };

    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

TEST(BindGroup, CreateWithSamplerBindingReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::sampler;
    entry.data.sampler.type = EntryObjectSamplerType::filtering;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto sampler = makeSampler(device);
    ASSERT_NE(sampler, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = { { 0, sampler } };

    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

TEST(BindGroup, CreateWithMixedResourcesInOneGroupReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};

    EntryObject bufEntry{};
    bufEntry.binding    = 0;
    bufEntry.visibility = ShaderStage::vertex;
    bufEntry.type       = EntryObjectType::buffer;
    bufEntry.data.buffer.type              = EntryObjectBufferType::uniform;
    bufEntry.data.buffer.hasDinamicOffaset = false;
    bufEntry.data.buffer.minBindingSize    = 0;
    layoutDesc.entries.push_back(bufEntry);

    EntryObject texEntry{};
    texEntry.binding    = 1;
    texEntry.visibility = ShaderStage::fragment;
    texEntry.type       = EntryObjectType::texture;
    texEntry.data.texture.multisampled  = false;
    texEntry.data.texture.sampleType    = EntryObjectTextureType::ttFloat;
    texEntry.data.texture.viewDimension = TextureType::tt2d;
    layoutDesc.entries.push_back(texEntry);

    EntryObject sampEntry{};
    sampEntry.binding    = 2;
    sampEntry.visibility = ShaderStage::fragment;
    sampEntry.type       = EntryObjectType::sampler;
    sampEntry.data.sampler.type = EntryObjectSamplerType::filtering;
    layoutDesc.entries.push_back(sampEntry);

    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto buffer = device->createBuffer(256, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);
    auto sampler = makeSampler(device);
    ASSERT_NE(sampler, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = {
        { 0, BufferBinding{ buffer, 0, 256 } },
        { 1, texture },
        { 2, sampler },
    };

    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

TEST(BindGroup, CreatePersistentBindGroupReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::sampler;
    entry.data.sampler.type = EntryObjectSamplerType::filtering;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto sampler = makeSampler(device);
    ASSERT_NE(sampler, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = { { 0, sampler } };

    auto bindGroup = device->createBindGroup(desc, /*persistent=*/true);
    EXPECT_NE(bindGroup, nullptr);
}

// Regression test: the persistent descriptor pool (Vulkan backend) only
// reserved SAMPLED_IMAGE/SAMPLER capacity — CreatePersistentBindGroupReturnsNonNull
// above never exercised UNIFORM_BUFFER there, so the gap went unnoticed until a
// caller (campello_renderer's Vulkan material bind group) tried to write a
// real uniform buffer into a persistent=true bind group and hit
// vkAllocateDescriptorSets() failing with "binding N was created with
// VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER but VkDescriptorPool ... was not created
// with any VkDescriptorPoolSize::type with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER".
TEST(BindGroup, CreatePersistentBindGroupWithUniformBufferReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto buffer = device->createBuffer(256, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);

    BindGroupDescriptor desc{};
    desc.layout  = layout;
    desc.entries = { { 0, BufferBinding{ buffer, 0, 256 } } };

    auto bindGroup = device->createBindGroup(desc, /*persistent=*/true);
    EXPECT_NE(bindGroup, nullptr);
}

TEST(BindGroup, MultipleBindGroupsFromSameLayoutCanCoexist) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::vertex;
    entry.type       = EntryObjectType::buffer;
    entry.data.buffer.type              = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize    = 0;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    auto bufferA = device->createBuffer(256, BufferUsage::uniform);
    auto bufferB = device->createBuffer(256, BufferUsage::uniform);
    ASSERT_NE(bufferA, nullptr);
    ASSERT_NE(bufferB, nullptr);

    BindGroupDescriptor descA{};
    descA.layout  = layout;
    descA.entries = { { 0, BufferBinding{ bufferA, 0, 256 } } };

    BindGroupDescriptor descB{};
    descB.layout  = layout;
    descB.entries = { { 0, BufferBinding{ bufferB, 0, 256 } } };

    auto bindGroupA = device->createBindGroup(descA);
    auto bindGroupB = device->createBindGroup(descB);

    ASSERT_NE(bindGroupA, nullptr);
    ASSERT_NE(bindGroupB, nullptr);
    EXPECT_NE(bindGroupA.get(), bindGroupB.get());
}
