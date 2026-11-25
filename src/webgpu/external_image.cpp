// copyExternalImageToTexture() on WebGPU: the browser's
// GPUQueue.copyExternalImageToTexture() converts the image source (VideoFrame,
// <video>, ImageBitmap, canvas) into an RGBA staging texture, and a
// full-screen draw recorded into the encoder writes it into the target with
// the uv transform. The browser's copy is a queue operation: it runs at once,
// so before the encoder's commands, whenever they're submitted. Each call
// gets its own staging texture, so several copies in one encoder don't
// overwrite each other's source. The JS side is in campello_gpu_web.js.

#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture.hpp>

#include "common.hpp"

#include <cstdio>
#include <unordered_map>

extern "C" {
void campello_gpu_external_image_size(int handle, int *width, int *height);
int campello_gpu_copy_external_image(WGPUQueue queue, WGPUTexture texture, int handle, int width, int height);
void campello_gpu_release_external_image(int handle);
}

namespace systems::leal::campello_gpu {

struct ExternalImageState {
    WGPUShaderModule module = nullptr;
    WGPUSampler sampler = nullptr;
    std::unordered_map<int, WGPURenderPipeline> pipelines;  // by target WGPUTextureFormat
};

namespace {

const char *kShader = R"(
struct Params { uvRow0: vec4f, uvRow1: vec4f, srgbTarget: vec4u };
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var sourceSampler: sampler;
@group(0) @binding(2) var source: texture_2d<f32>;

struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };

@vertex fn vs(@builtin(vertex_index) i: u32) -> VOut {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    var o: VOut;
    o.pos = vec4f(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2f(p.x, 1.0 - p.y);
    return o;
}

@fragment fn fs(v: VOut) -> @location(0) vec4f {
    let u = vec3f(v.uv, 1.0);
    let uv = vec2f(dot(params.uvRow0.xyz, u), dot(params.uvRow1.xyz, u));
    var c = textureSampleLevel(source, sourceSampler, uv, 0.0).rgb;
    if (params.srgbTarget.x != 0u) {
        // The target re-encodes to sRGB on write: hand it the linear value so
        // it stores the source's (non-linear) bytes.
        c = select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
    }
    return vec4f(c, 1.0);
}
)";

ExternalImageState *stateFor(WebGPUDeviceData *d) {
    if (d->externalImage) return d->externalImage;
    auto *s = new ExternalImageState();
    s->module = createWGSLShaderModule(d->device, kShader);
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;  // 0 is invalid
    s->sampler = wgpuDeviceCreateSampler(d->device, &samplerDesc);
    d->externalImage = s;
    return s;
}

WGPURenderPipeline pipelineFor(WebGPUDeviceData *d, ExternalImageState *s, WGPUTextureFormat format) {
    auto it = s->pipelines.find(int(format));
    if (it != s->pipelines.end()) return it->second;
    WGPUColorTargetState target{};
    target.format = format;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment{};
    fragment.module = s->module;
    fragment.entryPoint = toStringView("fs");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc{};
    desc.label = toStringView("campello_gpu external image copy");
    desc.layout = nullptr;  // auto
    desc.vertex.module = s->module;
    desc.vertex.entryPoint = toStringView("vs");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(d->device, &desc);
    if (pipeline) s->pipelines[int(format)] = pipeline;
    return pipeline;
}

bool isSupportedTarget(PixelFormat f, bool &srgb) {
    srgb = f == PixelFormat::rgba8unorm_srgb || f == PixelFormat::bgra8unorm_srgb;
    return srgb || f == PixelFormat::rgba8unorm || f == PixelFormat::bgra8unorm;
}

int handleOf(const ExternalImage &source) { return int(reinterpret_cast<intptr_t>(source.handle)); }

bool reject(const ExternalImage &source) {
    if (source.type == ExternalImageType::webImageSource && source.handle) {
        campello_gpu_release_external_image(handleOf(source));
    }
    return false;
}

// Copies the source into a staging texture now and records the draw into
// `encoder`.
bool encodeCopy(WebGPUDeviceData *d, WGPUCommandEncoder encoder, const ExternalImage &source, TextureHandle *t,
                const ExternalImageCopyOptions &options) {
    if (source.type != ExternalImageType::webImageSource || !source.handle) {
        fprintf(stderr, "[WebGPU] copyExternalImageToTexture: only ExternalImageType::webImageSource is supported\n");
        return reject(source);
    }
    bool srgb = false;
    if (!t || !t->texture || !t->defaultView || t->textureType != TextureType::tt2d ||
        !(static_cast<uint32_t>(t->usage) & static_cast<uint32_t>(TextureUsage::renderTarget)) ||
        !isSupportedTarget(t->pixelFormat, srgb)) {
        fprintf(stderr, "[WebGPU] copyExternalImageToTexture: the target must be a 2D render-target texture in "
                        "rgba8unorm, bgra8unorm or their _srgb variants\n");
        return reject(source);
    }
    const int handle = handleOf(source);
    int width = 0, height = 0;
    campello_gpu_external_image_size(handle, &width, &height);
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "[WebGPU] copyExternalImageToTexture: unknown or empty image source (handle %d)\n", handle);
        return reject(source);
    }
    ExternalImageState *s = stateFor(d);
    WGPURenderPipeline pipeline = s->module && s->sampler ? pipelineFor(d, s, toWGPUTextureFormat(t->pixelFormat))
                                                          : nullptr;
    if (!pipeline) return reject(source);

    WGPUTextureDescriptor stagingDesc{};
    stagingDesc.label = toStringView("campello_gpu external image staging");
    stagingDesc.size = {uint32_t(width), uint32_t(height), 1};
    stagingDesc.mipLevelCount = 1;
    stagingDesc.sampleCount = 1;
    stagingDesc.dimension = WGPUTextureDimension_2D;
    stagingDesc.format = WGPUTextureFormat_RGBA8Unorm;
    stagingDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture staging = wgpuDeviceCreateTexture(d->device, &stagingDesc);
    if (!staging) return reject(source);
    if (!campello_gpu_copy_external_image(d->queue, staging, handle, width, height)) {
        wgpuTextureRelease(staging);
        return false;
    }

    struct Params { float uvRow0[4]; float uvRow1[4]; uint32_t srgbTarget[4]; } params{};
    static const float kIdentity[6] = {1, 0, 0, 0, 1, 0};
    const float *m = options.uvTransform ? options.uvTransform : kIdentity;
    params.uvRow0[0] = m[0]; params.uvRow0[1] = m[1]; params.uvRow0[2] = m[2];
    params.uvRow1[0] = m[3]; params.uvRow1[1] = m[4]; params.uvRow1[2] = m[5];
    params.srgbTarget[0] = srgb ? 1 : 0;
    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.size = sizeof(params);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniforms = wgpuDeviceCreateBuffer(d->device, &bufferDesc);
    wgpuQueueWriteBuffer(d->queue, uniforms, 0, &params, sizeof(params));

    WGPUTextureView stagingView = wgpuTextureCreateView(staging, nullptr);
    WGPUBindGroupEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].buffer = uniforms;
    entries[0].size = sizeof(params);
    entries[1].binding = 1;
    entries[1].sampler = s->sampler;
    entries[2].binding = 2;
    entries[2].textureView = stagingView;
    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    bgDesc.entryCount = 3;
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(d->device, &bgDesc);
    wgpuBindGroupLayoutRelease(bgDesc.layout);

    WGPURenderPassColorAttachment color{};
    color.view = t->defaultView;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;  // every pixel is written
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor passDesc{};
    passDesc.label = toStringView("campello_gpu external image copy");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // The command buffer keeps what it uses alive.
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(stagingView);
    wgpuTextureRelease(staging);
    wgpuBufferRelease(uniforms);
    return true;
}

} // namespace

void releaseExternalImageState(WebGPUDeviceData *deviceData) {
    ExternalImageState *s = deviceData->externalImage;
    if (!s) return;
    for (auto &[format, pipeline] : s->pipelines) wgpuRenderPipelineRelease(pipeline);
    if (s->sampler) wgpuSamplerRelease(s->sampler);
    if (s->module) wgpuShaderModuleRelease(s->module);
    delete s;
    deviceData->externalImage = nullptr;
}

bool Device::supportsExternalImage(ExternalImageType type) const {
    return type == ExternalImageType::webImageSource;
}

bool Device::copyExternalImageToTexture(const ExternalImage &source, std::shared_ptr<Texture> target,
                                        const ExternalImageCopyOptions &options) {
    auto *d = static_cast<WebGPUDeviceData *>(native);
    if (!d || !target) return reject(source);
    WGPUCommandEncoderDescriptor encDesc{};
    encDesc.label = toStringView("campello_gpu copyExternalImageToTexture");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(d->device, &encDesc);
    if (!encoder) return reject(source);
    const bool ok = encodeCopy(d, encoder, source, static_cast<TextureHandle *>(target->native), options);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (ok && commands) {
        wgpuQueueSubmit(d->queue, 1, &commands);
        WGPUAsyncResult result;
        wgpuQueueOnSubmittedWorkDone(d->queue, makeCallbackInfo<WGPUQueueWorkDoneCallbackInfo>(&result));
        waitForAsync(result);
    }
    if (commands) wgpuCommandBufferRelease(commands);
    return ok && commands;
}

bool CommandEncoder::copyExternalImageToTexture(const ExternalImage &source, std::shared_ptr<Texture> target,
                                                const ExternalImageCopyOptions &options) {
    auto *handle = static_cast<CommandEncoderHandle *>(native);
    if (!handle || !handle->deviceData || !target) return reject(source);
    return encodeCopy(handle->deviceData, handle->encoder, source, static_cast<TextureHandle *>(target->native),
                      options);
}

} // namespace systems::leal::campello_gpu
