//
//  WASM / WebGPU compute example — campello_gpu + Emscripten.
//
//  Runs SAXPY (y = a * x + y) over a million floats in a compute shader,
//  reads the result back without blocking the page (Buffer::downloadAsync),
//  checks it against the same computation on the CPU, and reports the GPU
//  time when the browser exposes the "timestamp-query" feature.
//
//  Build (see README.md, "Web (Emscripten / WebGPU)"):
//    emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON
//    cmake --build build
//    python3 -m http.server -d build/examples/wasm 8080
//    open http://localhost:8080/campello_wasm_compute.html
//

#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace systems::leal::campello_gpu;

static constexpr uint32_t kCount = 1u << 20;     // 1,048,576 elements
static constexpr uint32_t kWorkgroupSize = 256;
static constexpr float kA = 2.5f;

static const char* kSaxpyWGSL = R"(
struct Params {
    a: f32,
    count: u32,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read> x: array<f32>;
@group(0) @binding(2) var<storage, read_write> y: array<f32>;

@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let i = id.x;
    if (i < params.count) {
        y[i] = params.a * x[i] + y[i];
    }
}
)";

struct Params {
    float a;
    uint32_t count;
};

// Kept alive until the asynchronous readback completes (main() returns first).
static std::shared_ptr<Device> g_device;
static std::shared_ptr<Buffer> g_yBuffer;
static std::shared_ptr<CommandBuffer> g_commandBuffer;
static std::vector<float> g_x, g_y, g_result;

static EntryObject bufferEntry(uint32_t binding, EntryObjectBufferType type) {
    EntryObject entry{};
    entry.binding = binding;
    entry.visibility = ShaderStage::compute;
    entry.type = EntryObjectType::buffer;
    entry.data.buffer.type = type;
    return entry;
}

static void onResult(bool ok) {
    if (!ok) {
        fprintf(stderr, "Readback failed\n");
        return;
    }

    // Verify against the CPU.
    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < kCount; i++) {
        float expected = kA * g_x[i] + g_y[i];
        if (std::fabs(g_result[i] - expected) > 1e-4f * std::fabs(expected) + 1e-6f) mismatches++;
    }
    printf("y[0..3] = %.3f %.3f %.3f %.3f\n", g_result[0], g_result[1], g_result[2], g_result[3]);
    printf("Verified %u elements against the CPU: %s (%u mismatches)\n",
           kCount, mismatches == 0 ? "OK" : "MISMATCH", mismatches);

    // The command buffer was submitted, so its GPU time is (or will be) known.
    g_commandBuffer->getGPUExecutionTimeAsync([](uint64_t ns) {
        if (ns == 0) {
            printf("GPU time: not available (the browser doesn't expose timestamp-query)\n");
        } else {
            printf("GPU time: %.3f ms\n", ns / 1e6);
        }
    });
}

int main() {
    // Blocks until the browser grants a device (ASYNCIFY). No canvas needed.
    g_device = Device::createDefaultDevice(nullptr);
    if (!g_device) {
        fprintf(stderr, "WebGPU is not available in this browser\n");
        return 1;
    }
    printf("Device: %s\n", g_device->getName().c_str());

    // Input data.
    g_x.resize(kCount);
    g_y.resize(kCount);
    g_result.resize(kCount);
    for (uint32_t i = 0; i < kCount; i++) {
        g_x[i] = static_cast<float>(i % 1000) * 0.001f;
        g_y[i] = static_cast<float>(i % 7);
    }

    Params params{ kA, kCount };
    auto paramsBuffer = g_device->createBuffer(sizeof(Params), BufferUsage::uniform, &params);
    auto xBuffer = g_device->createBuffer(kCount * sizeof(float), BufferUsage::storage, g_x.data());
    g_yBuffer = g_device->createBuffer(kCount * sizeof(float), BufferUsage::storage, g_y.data());

    // Layout: params (uniform), x (read-only storage), y (read-write storage).
    BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entries = {
        bufferEntry(0, EntryObjectBufferType::uniform),
        bufferEntry(1, EntryObjectBufferType::readOnlyStorage),
        bufferEntry(2, EntryObjectBufferType::storage),
    };
    auto bindGroupLayout = g_device->createBindGroupLayout(bglDesc);

    PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayouts = { bindGroupLayout };

    ComputePipelineDescriptor cpDesc{};
    cpDesc.compute.module = g_device->createShaderModule(kSaxpyWGSL);
    cpDesc.compute.entryPoint = "main";
    cpDesc.layout = g_device->createPipelineLayout(plDesc);
    auto pipeline = g_device->createComputePipeline(cpDesc);
    if (!pipeline) {
        fprintf(stderr, "Failed to create the compute pipeline\n");
        return 1;
    }

    BindGroupDescriptor bgDesc{};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entries = {
        { 0, BufferBinding{ paramsBuffer, 0, sizeof(Params) } },
        { 1, BufferBinding{ xBuffer, 0, kCount * sizeof(float) } },
        { 2, BufferBinding{ g_yBuffer, 0, kCount * sizeof(float) } },
    };
    auto bindGroup = g_device->createBindGroup(bgDesc, false);

    // Record and submit the dispatch.
    auto encoder = g_device->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    pass->setPipeline(pipeline);
    pass->setBindGroup(0, bindGroup, {}, 0, 0);
    pass->dispatchWorkgroups((kCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
    pass->end();
    g_commandBuffer = encoder->finish();
    g_device->submit(g_commandBuffer);
    printf("Dispatched SAXPY over %u elements\n", kCount);

    // Non-blocking readback: onResult runs once the GPU work and the copy are
    // done, while the page stays responsive. (Buffer::download() is the
    // blocking alternative.)
    g_yBuffer->downloadAsync(0, kCount * sizeof(float), g_result.data(), onResult);
    return 0;
}
