// WebGPU backend browser test: web-specific checks the cross-platform
// GoogleTest suite (webgpu_gtest_suite) doesn't cover — the canvas surface and
// resizing, JavaScript texture interop, GPU timing and the error counter.
//
// A self-contained program: every check prints "PASS <name>" or
// "FAIL <name>", and the run ends with "CAMPELLO_WEBGPU_TESTS_DONE
// failures=<n>". run_in_browser.mjs loads the page in headless Chrome and
// turns that output into an exit code.
//
// Blocking calls (device creation, download(), waitForIdle()) yield to the
// browser (ASYNCIFY or JSPI), so the checks read top to bottom.

#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/validation_diagnostics.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <emscripten.h>
#include <webgpu/webgpu.h>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace systems::leal::campello_gpu;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failures++;
}

static const char* kDoubleWGSL = R"(
@group(0) @binding(0) var<storage, read_write> data: array<f32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&data)) { data[id.x] = data[id.x] * 2.0; }
}
)";

// Full-screen triangle filled with opaque red.
static const char* kFullscreenWGSL = R"(
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(p[i], 0.5, 1.0);
}
@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }
)";

static std::shared_ptr<RenderPipeline> fullscreenPipeline(std::shared_ptr<Device> device,
                                                          PixelFormat colorFormat,
                                                          uint32_t samples, bool withDepth) {
    auto module = device->createShaderModule(kFullscreenWGSL);
    RenderPipelineDescriptor desc{};
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs";
    desc.fragment = FragmentDescriptor{};
    desc.fragment->module = module;
    desc.fragment->entryPoint = "fs";
    desc.fragment->targets = { ColorState{ colorFormat, ColorWrite::all, std::nullopt } };
    desc.topology = PrimitiveTopology::triangleList;
    desc.cullMode = CullMode::none;
    desc.frontFace = FrontFace::ccw;
    desc.sampleCount = samples;
    if (withDepth) {
        DepthStencilDescriptor ds{};
        ds.format = PixelFormat::depth32float;
        ds.depthWriteEnabled = true;
        ds.depthCompare = CompareOp::less;
        desc.depthStencil = ds;
    }
    return device->createRenderPipeline(desc);
}

static bool centerIsRed(std::shared_ptr<Texture> tex, uint32_t w, uint32_t h) {
    std::vector<uint8_t> px(w * h * 4);
    if (!tex->download(0, 0, px.data(), px.size())) return false;
    const uint8_t* c = &px[((h / 2) * w + w / 2) * 4];
    return c[0] == 255 && c[1] == 0 && c[2] == 0 && c[3] == 255;
}

// ---------------------------------------------------------------------------

static void testCompute(std::shared_ptr<Device> device) {
    const uint32_t n = 256;
    std::vector<float> input(n), output(n, 0.0f);
    for (uint32_t i = 0; i < n; i++) input[i] = float(i);

    // Storage usage only: upload()/download() rely on the copy usages the
    // backend adds implicitly.
    auto buffer = device->createBuffer(n * sizeof(float), BufferUsage::storage, input.data());
    check(buffer != nullptr, "compute: storage buffer created");

    EntryObject entry{};
    entry.binding = 0;
    entry.visibility = ShaderStage::compute;
    entry.type = EntryObjectType::buffer;
    entry.data.buffer.type = EntryObjectBufferType::storage;
    BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entries = { entry };
    auto bgl = device->createBindGroupLayout(bglDesc);

    PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayouts = { bgl };

    ComputePipelineDescriptor cpDesc{};
    cpDesc.compute.module = device->createShaderModule(kDoubleWGSL);
    cpDesc.compute.entryPoint = "main";
    cpDesc.layout = device->createPipelineLayout(plDesc);
    auto pipeline = device->createComputePipeline(cpDesc);
    check(pipeline != nullptr, "compute: pipeline created");

    BindGroupDescriptor bgDesc{};
    bgDesc.layout = bgl;
    bgDesc.entries = { { 0, BufferBinding{ buffer, 0, n * sizeof(float) } } };
    auto bindGroup = device->createBindGroup(bgDesc, false);

    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    pass->setPipeline(pipeline);
    pass->setBindGroup(0, bindGroup, {}, 0, 0);
    pass->dispatchWorkgroups(n / 64, 1, 1);
    pass->end();
    auto cmd = encoder->finish();
    device->submit(cmd);

    check(buffer->download(0, n * sizeof(float), output.data()), "compute: download() succeeds");
    bool correct = true;
    for (uint32_t i = 0; i < n; i++) correct &= (output[i] == float(i) * 2.0f);
    check(correct, "compute: results doubled");

    // 0 when the adapter lacks "timestamp-query"; informational only.
    printf("INFO compute getGPUExecutionTime = %llu ns\n",
           (unsigned long long)cmd->getGPUExecutionTime());
}

static void testTextures(std::shared_ptr<Device> device) {
    const uint32_t w = 64, h = 64;
    std::vector<uint8_t> pixels(w * h * 4), back(w * h * 4, 0);
    for (size_t i = 0; i < pixels.size(); i++) pixels[i] = uint8_t(i * 7);
    auto tex = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, w, h, 1, 1, 1,
                                     TextureUsage::textureBinding);
    check(tex && tex->upload(0, pixels.size(), pixels.data()), "texture: upload() succeeds");
    check(tex && tex->download(0, 0, back.data(), back.size()), "texture: download() succeeds");
    check(memcmp(pixels.data(), back.data(), pixels.size()) == 0, "texture: round-trip matches");

    // A multi-layer 2D texture needs a 2D-array default view.
    auto arr = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, 16, 16, 3, 1, 1,
                                     TextureUsage::textureBinding);
    check(arr != nullptr, "texture: 2D array texture created");
}

static void testDepthPass(std::shared_ptr<Device> device) {
    const uint32_t w = 32, h = 32;
    auto color = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, w, h, 1, 1, 1,
                                       TextureUsage::renderTarget);
    auto depth = device->createTexture(TextureType::tt2d, PixelFormat::depth32float, w, h, 1, 1, 1,
                                       TextureUsage::renderTarget);
    auto pipeline = fullscreenPipeline(device, PixelFormat::rgba8unorm, 1, true);

    ColorAttachment ca{};
    ca.view = color->createView(PixelFormat::rgba8unorm);
    ca.loadOp = LoadOp::clear;
    ca.storeOp = StoreOp::store;
    DepthStencilAttachment dsa{};
    dsa.view = depth->createView(PixelFormat::depth32float);
    dsa.depthClearValue = 1.0f;
    dsa.depthLoadOp = LoadOp::clear;
    dsa.depthStoreOp = StoreOp::store;
    // depth32float has no stencil aspect: the backend must drop these.
    dsa.stencilLoadOp = LoadOp::clear;
    dsa.stencilStoreOp = StoreOp::store;
    BeginRenderPassDescriptor rp{};
    rp.colorAttachments = { ca };
    rp.depthStencilAttachment = dsa;

    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginRenderPass(rp);
    pass->setPipeline(pipeline);
    pass->draw(3, 1, 0, 0);
    pass->end();
    device->submit(encoder->finish());
    check(centerIsRed(color, w, h), "render: depth32float pass draws");
}

static void testMsaa(std::shared_ptr<Device> device) {
    const uint32_t w = 32, h = 32;
    auto msaa = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, w, h, 1, 1, 4,
                                      TextureUsage::renderTarget);
    auto resolve = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, w, h, 1, 1, 1,
                                         TextureUsage::renderTarget);
    auto pipeline = fullscreenPipeline(device, PixelFormat::rgba8unorm, 4, false);
    check(pipeline != nullptr, "msaa: sampleCount=4 pipeline created");

    ColorAttachment ca{};
    ca.view = msaa->createView(PixelFormat::rgba8unorm);
    ca.resolveTarget = resolve->createView(PixelFormat::rgba8unorm);
    ca.loadOp = LoadOp::clear;
    ca.storeOp = StoreOp::discard;
    BeginRenderPassDescriptor rp{};
    rp.colorAttachments = { ca };

    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginRenderPass(rp);
    pass->setPipeline(pipeline);
    pass->draw(3, 1, 0, 0);
    pass->end();
    device->submit(encoder->finish());
    check(centerIsRed(resolve, w, h), "msaa: resolved image is red");
}

static void testDownloadAsync(std::shared_ptr<Device> device) {
    static float src[4] = { 1, 2, 3, 4 };
    static float dst[4] = { 0, 0, 0, 0 };
    static bool done = false, ok = false;
    auto buffer = device->createBuffer(sizeof(src), BufferUsage::uniform, src);
    buffer->downloadAsync(0, sizeof(src), dst, [](bool success) { ok = success; done = true; });
    for (int i = 0; i < 5000 && !done; i++) emscripten_sleep(1);
    check(done && ok && memcmp(src, dst, sizeof(src)) == 0, "buffer: downloadAsync() delivers data");
}

// Creates a 16x8 rgba8unorm texture in JavaScript on campello's own GPUDevice
// and imports it as a WGPUTexture handle (owned by the caller).
EM_JS(void*, createTextureInJavaScript, (void* devicePtr), {
    var device = Module.campelloGpu.getJsObject(devicePtr);
    var texture = device.createTexture({
        size: [16, 8],
        format: 'rgba8unorm',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST,
    });
    return Module.campelloGpu.importTexture(texture);
});

static void testJavaScriptInterop(std::shared_ptr<Device> device) {
    void* devicePtr = device->getNativeWGPUDevice();
    check(devicePtr != nullptr, "interop: getNativeWGPUDevice()");

    void* handle = createTextureInJavaScript(devicePtr);
    auto tex = device->createTextureFromWGPUTexture(handle);
    wgpuTextureRelease(static_cast<WGPUTexture>(handle));  // tex keeps its own reference
    check(tex != nullptr, "interop: wrap a JavaScript GPUTexture");
    if (!tex) return;
    check(tex->getWidth() == 16 && tex->getHeight() == 8 && tex->getMipLevelCount() == 1 &&
          tex->getFormat() == PixelFormat::rgba8unorm && tex->getDimension() == TextureType::tt2d,
          "interop: wrapped texture reports the JavaScript texture's shape");

    std::vector<uint8_t> pixels(16 * 8 * 4), back(pixels.size(), 0);
    for (size_t i = 0; i < pixels.size(); i++) pixels[i] = uint8_t(i * 3 + 1);
    check(tex->upload(0, pixels.size(), pixels.data()) &&
          tex->download(0, 0, back.data(), back.size()) && pixels == back,
          "interop: upload/download through the wrapped texture");
}

static void testMemoryInfo(std::shared_ptr<Device> device) {
    auto before = device->getMemoryInfo().currentAllocatedSize;
    auto buffer = device->createBuffer(1 << 20, BufferUsage::storage);
    auto after = device->getMemoryInfo().currentAllocatedSize;
    check(after >= before + (1 << 20), "memory: getMemoryInfo() tracks allocations");
    auto stats = device->getResourceMemoryStats();
    check(stats.totalTrackedBytes >= stats.bufferBytes + stats.textureBytes && stats.totalTrackedBytes > 0,
          "memory: totalTrackedBytes is filled in");
}

static void renderToCanvas(std::shared_ptr<Device> device, std::shared_ptr<RenderPipeline> pipeline) {
    auto view = device->getSwapchainTextureView();
    if (!view) return;
    ColorAttachment ca{};
    ca.view = view;
    ca.loadOp = LoadOp::clear;
    ca.storeOp = StoreOp::store;
    BeginRenderPassDescriptor rp{};
    rp.colorAttachments = { ca };
    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginRenderPass(rp);
    pass->setPipeline(pipeline);
    pass->draw(3, 1, 0, 0);
    pass->end();
    device->submit(encoder->finish());
}

static void testCanvas(std::shared_ptr<Adapter> adapter) {
    EM_ASM({
        var c = document.getElementById('canvas');
        c.width = 320; c.height = 240;
    });
    auto device = Device::createDevice(adapter, const_cast<char*>("#canvas"));
    check(device != nullptr, "canvas: device with a canvas surface created");
    if (!device) return;

    PixelFormat format = device->getSwapchainPixelFormat();
    check(format == PixelFormat::bgra8unorm || format == PixelFormat::rgba8unorm,
          "canvas: swapchain uses the preferred canvas format");

    auto pipeline = fullscreenPipeline(device, format, 1, false);
    check(device->getSwapchainTextureView() != nullptr, "canvas: swapchain view available");
    renderToCanvas(device, pipeline);

    // The surface must follow a canvas resize without errors.
    EM_ASM({
        var c = document.getElementById('canvas');
        c.width = 200; c.height = 100;
    });
    check(device->getSwapchainTextureView() != nullptr, "canvas: swapchain view after resize");
    renderToCanvas(device, pipeline);
    device->waitForIdle();
}

int main() {
    auto adapters = Device::getAdapters();
    check(adapters.size() == 1, "getAdapters() returns the browser adapter");
    if (adapters.empty()) {
        printf("CAMPELLO_WEBGPU_TESTS_DONE failures=%d\n", g_failures);
        return 1;
    }

    auto device = Device::createDevice(adapters[0], nullptr);  // headless
    check(device != nullptr, "createDevice() without a canvas");
    if (!device) {
        printf("CAMPELLO_WEBGPU_TESTS_DONE failures=%d\n", g_failures);
        return 1;
    }
    printf("INFO device: %s\n", device->getName().c_str());

    auto features = device->getFeatures();
    printf("INFO features: fp16=%d subgroups=%d bc=%d etc2=%d astc=%d\n",
           (int)features.count(Feature::fp16), (int)features.count(Feature::subgroupOperations),
           (int)features.count(Feature::bcTextureCompression),
           (int)features.count(Feature::etc2TextureCompression),
           (int)features.count(Feature::astcTextureCompression));
    check(adapters[0]->getFeatures() == features, "adapter and device report the same features");
    check(features.count(Feature::raytracing) == 0 && features.count(Feature::cooperativeMatrix) == 0,
          "unsupported features are not reported");

    resetValidationErrorCount();
    testCompute(device);
    testTextures(device);
    testDepthPass(device);
    testMsaa(device);
    testDownloadAsync(device);
    testJavaScriptInterop(device);
    testMemoryInfo(device);
    testCanvas(adapters[0]);

    device->waitForIdle();
    auto stats = device->getPassPerformanceStats();
    printf("INFO pass stats: render=%llu ns (%u samples), compute=%llu ns (%u samples)\n",
           (unsigned long long)stats.renderPassTimeNs, stats.renderPassSampleCount,
           (unsigned long long)stats.computePassTimeNs, stats.computePassSampleCount);

    check(validationErrorCount() == 0, "no WebGPU errors during the tests");

    // A zero-sized texture is invalid: the uncaptured-error callback must count it.
    uint64_t before = validationErrorCount();
    printf("INFO the next WebGPU error is expected\n");
    device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, 0, 0, 1, 1, 1,
                          TextureUsage::textureBinding);
    device->waitForIdle();
    for (int i = 0; i < 1000 && validationErrorCount() == before; i++) emscripten_sleep(1);
    check(validationErrorCount() > before, "validationErrorCount() counts a deliberate error");

    printf("CAMPELLO_WEBGPU_TESTS_DONE failures=%d\n", g_failures);
    return 0;
}
