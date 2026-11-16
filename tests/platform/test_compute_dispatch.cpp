// Platform integration tests that run a compute dispatch and read its output
// back: a uniform, a read-only storage buffer and a storage buffer (the shape
// of a GPU bake or a simulation step), then copyBufferToBuffer() into a
// mappable buffer. The shader is SPIR-V, so this runs on Vulkan only.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>

#include <cstdint>
#include <vector>

using namespace systems::leal::campello_gpu;

#if defined(__ANDROID__) || (defined(__linux__) && !defined(__EMSCRIPTEN__))
namespace {

// outputs[i] = inputs[i] * 2 + add for i < count, one workgroup (of one
// invocation) per element (glslangValidator -V):
//
//   layout(local_size_x = 1) in;
//   layout(std140, binding = 0) uniform Params { uint count; uint add; uint pad0; uint pad1; };
//   layout(std430, binding = 1) readonly buffer Input { uint inputs[]; };
//   layout(std430, binding = 2) buffer Output { uint outputs[]; };
//   void main() {
//       uint i = gl_WorkGroupID.x;
//       if (i >= count) return;
//       outputs[i] = inputs[i] * 2u + add;
//   }
const uint32_t kComputeSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000033, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0006000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x00060010, 0x00000004,
    0x00000011, 0x00000001, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000008, 0x00000069, 0x00060005, 0x0000000b,
    0x575f6c67, 0x476b726f, 0x70756f72, 0x00004449, 0x00040005, 0x00000011, 0x61726150, 0x0000736d,
    0x00050006, 0x00000011, 0x00000000, 0x6e756f63, 0x00000074, 0x00040006, 0x00000011, 0x00000001,
    0x00646461, 0x00050006, 0x00000011, 0x00000002, 0x30646170, 0x00000000, 0x00050006, 0x00000011,
    0x00000003, 0x31646170, 0x00000000, 0x00030005, 0x00000013, 0x00000000, 0x00040005, 0x0000001f,
    0x7074754f, 0x00007475, 0x00050006, 0x0000001f, 0x00000000, 0x7074756f, 0x00737475, 0x00030005,
    0x00000021, 0x00000000, 0x00040005, 0x00000024, 0x75706e49, 0x00000074, 0x00050006, 0x00000024,
    0x00000000, 0x75706e69, 0x00007374, 0x00030005, 0x00000026, 0x00000000, 0x00040047, 0x0000000b,
    0x0000000b, 0x0000001a, 0x00030047, 0x00000011, 0x00000002, 0x00050048, 0x00000011, 0x00000000,
    0x00000023, 0x00000000, 0x00050048, 0x00000011, 0x00000001, 0x00000023, 0x00000004, 0x00050048,
    0x00000011, 0x00000002, 0x00000023, 0x00000008, 0x00050048, 0x00000011, 0x00000003, 0x00000023,
    0x0000000c, 0x00040047, 0x00000013, 0x00000021, 0x00000000, 0x00040047, 0x00000013, 0x00000022,
    0x00000000, 0x00040047, 0x0000001e, 0x00000006, 0x00000004, 0x00030047, 0x0000001f, 0x00000003,
    0x00050048, 0x0000001f, 0x00000000, 0x00000023, 0x00000000, 0x00040047, 0x00000021, 0x00000021,
    0x00000002, 0x00040047, 0x00000021, 0x00000022, 0x00000000, 0x00040047, 0x00000023, 0x00000006,
    0x00000004, 0x00030047, 0x00000024, 0x00000003, 0x00040048, 0x00000024, 0x00000000, 0x00000018,
    0x00050048, 0x00000024, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000026, 0x00000018,
    0x00040047, 0x00000026, 0x00000021, 0x00000001, 0x00040047, 0x00000026, 0x00000022, 0x00000000,
    0x00040047, 0x00000032, 0x0000000b, 0x00000019, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00040015, 0x00000006, 0x00000020, 0x00000000, 0x00040020, 0x00000007, 0x00000007,
    0x00000006, 0x00040017, 0x00000009, 0x00000006, 0x00000003, 0x00040020, 0x0000000a, 0x00000001,
    0x00000009, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001, 0x0004002b, 0x00000006, 0x0000000c,
    0x00000000, 0x00040020, 0x0000000d, 0x00000001, 0x00000006, 0x0006001e, 0x00000011, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x00000012, 0x00000002, 0x00000011, 0x0004003b,
    0x00000012, 0x00000013, 0x00000002, 0x00040015, 0x00000014, 0x00000020, 0x00000001, 0x0004002b,
    0x00000014, 0x00000015, 0x00000000, 0x00040020, 0x00000016, 0x00000002, 0x00000006, 0x00020014,
    0x00000019, 0x0003001d, 0x0000001e, 0x00000006, 0x0003001e, 0x0000001f, 0x0000001e, 0x00040020,
    0x00000020, 0x00000002, 0x0000001f, 0x0004003b, 0x00000020, 0x00000021, 0x00000002, 0x0003001d,
    0x00000023, 0x00000006, 0x0003001e, 0x00000024, 0x00000023, 0x00040020, 0x00000025, 0x00000002,
    0x00000024, 0x0004003b, 0x00000025, 0x00000026, 0x00000002, 0x0004002b, 0x00000006, 0x0000002a,
    0x00000002, 0x0004002b, 0x00000014, 0x0000002c, 0x00000001, 0x0004002b, 0x00000006, 0x00000031,
    0x00000001, 0x0006002c, 0x00000009, 0x00000032, 0x00000031, 0x00000031, 0x00000031, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003b, 0x00000007,
    0x00000008, 0x00000007, 0x00050041, 0x0000000d, 0x0000000e, 0x0000000b, 0x0000000c, 0x0004003d,
    0x00000006, 0x0000000f, 0x0000000e, 0x0003003e, 0x00000008, 0x0000000f, 0x0004003d, 0x00000006,
    0x00000010, 0x00000008, 0x00050041, 0x00000016, 0x00000017, 0x00000013, 0x00000015, 0x0004003d,
    0x00000006, 0x00000018, 0x00000017, 0x000500ae, 0x00000019, 0x0000001a, 0x00000010, 0x00000018,
    0x000300f7, 0x0000001c, 0x00000000, 0x000400fa, 0x0000001a, 0x0000001b, 0x0000001c, 0x000200f8,
    0x0000001b, 0x000100fd, 0x000200f8, 0x0000001c, 0x0004003d, 0x00000006, 0x00000022, 0x00000008,
    0x0004003d, 0x00000006, 0x00000027, 0x00000008, 0x00060041, 0x00000016, 0x00000028, 0x00000026,
    0x00000015, 0x00000027, 0x0004003d, 0x00000006, 0x00000029, 0x00000028, 0x00050084, 0x00000006,
    0x0000002b, 0x00000029, 0x0000002a, 0x00050041, 0x00000016, 0x0000002d, 0x00000013, 0x0000002c,
    0x0004003d, 0x00000006, 0x0000002e, 0x0000002d, 0x00050080, 0x00000006, 0x0000002f, 0x0000002b,
    0x0000002e, 0x00060041, 0x00000016, 0x00000030, 0x00000021, 0x00000015, 0x00000022, 0x0003003e,
    0x00000030, 0x0000002f, 0x000100fd, 0x00010038
};

struct Params {
    uint32_t count;
    uint32_t add;
    uint32_t pad0;
    uint32_t pad1;
};

constexpr uint32_t kCount = 64;

BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<int>(a) | static_cast<int>(b));
}

// Runs the dispatch; waitWithFence picks how the submit is waited on.
std::vector<uint32_t> runDispatch(const std::shared_ptr<Device>& device, bool waitWithFence) {
    auto module = device->createShaderModule(reinterpret_cast<const uint8_t*>(kComputeSpirv),
                                             sizeof(kComputeSpirv));
    EXPECT_NE(module, nullptr);
    if (!module) return {};

    BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entries = {
        {0, ShaderStage::compute, EntryObjectType::buffer,
         {{false, sizeof(Params), EntryObjectBufferType::uniform}}},
        {1, ShaderStage::compute, EntryObjectType::buffer,
         {{false, 0, EntryObjectBufferType::readOnlyStorage}}},
        {2, ShaderStage::compute, EntryObjectType::buffer,
         {{false, 0, EntryObjectBufferType::storage}}},
    };
    auto bindGroupLayout = device->createBindGroupLayout(layoutDesc);
    PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayouts = {bindGroupLayout};
    auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
    ComputePipelineDescriptor pipelineDesc{};
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = "main";
    pipelineDesc.layout = pipelineLayout;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    EXPECT_NE(pipeline, nullptr);
    if (!pipeline) return {};

    Params params{kCount, 5, 0, 0};
    std::vector<uint32_t> inputs(kCount);
    for (uint32_t i = 0; i < kCount; ++i) inputs[i] = i;
    const uint64_t bytes = kCount * sizeof(uint32_t);
    auto paramBuffer = device->createBuffer(sizeof(Params), BufferUsage::uniform, &params);
    auto inputBuffer = device->createBuffer(bytes, BufferUsage::storage, inputs.data());
    auto outputBuffer = device->createBuffer(bytes, BufferUsage::storage | BufferUsage::copySrc);
    auto readback = device->createBuffer(bytes, BufferUsage::copyDst | BufferUsage::mapRead);
    EXPECT_TRUE(paramBuffer && inputBuffer && outputBuffer && readback);
    if (!paramBuffer || !inputBuffer || !outputBuffer || !readback) return {};

    BindGroupDescriptor groupDesc{};
    groupDesc.layout = bindGroupLayout;
    groupDesc.entries = {
        {0, BufferBinding{paramBuffer, 0, sizeof(Params)}},
        {1, BufferBinding{inputBuffer, 0, bytes}},
        {2, BufferBinding{outputBuffer, 0, bytes}},
    };
    auto bindGroup = device->createBindGroup(groupDesc);
    EXPECT_NE(bindGroup, nullptr);
    if (!bindGroup) return {};

    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    pass->setPipeline(pipeline);
    pass->setBindGroup(0, bindGroup, {}, 0, 0);
    pass->dispatchWorkgroups(kCount, 1, 1);
    pass->end();
    encoder->copyBufferToBuffer(outputBuffer, 0, readback, 0, bytes);
    auto commands = encoder->finish();
    if (waitWithFence) {
        auto fence = device->createFence();
        device->submit(commands, fence);
        fence->wait();
    } else {
        device->submit(commands);
        device->waitForIdle();
    }

    std::vector<uint32_t> out(kCount, 0xdeadbeef);
    EXPECT_TRUE(readback->download(0, bytes, out.data()));
    return out;
}

void expectResults(const std::vector<uint32_t>& out) {
    ASSERT_EQ(out.size(), kCount);
    for (uint32_t i = 0; i < kCount; ++i) {
        ASSERT_EQ(out[i], i * 2 + 5) << "element " << i;
    }
}

}  // namespace

TEST(ComputeDispatch, WritesStorageBufferWaitedWithFence) {
    auto device = Device::createDefaultDevice(nullptr);
    if (!device) GTEST_SKIP() << "No device on this platform";
    expectResults(runDispatch(device, true));
}

TEST(ComputeDispatch, WritesStorageBufferWaitedWithWaitForIdle) {
    auto device = Device::createDefaultDevice(nullptr);
    if (!device) GTEST_SKIP() << "No device on this platform";
    expectResults(runDispatch(device, false));
}
#endif
