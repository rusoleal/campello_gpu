// copyExternalImageToTexture() on Metal: CVPixelBuffers (AVFoundation,
// VideoToolbox) imported through a CVMetalTextureCache, one MTLTexture per
// plane, and drawn into the target by a full-screen triangle that converts
// YCbCr to RGB. Objective-C++ with ARC: the backend's metal-cpp objects are
// the same Objective-C objects, bridged here.

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

#include "common.hpp"
#include "texture_handle.hpp"
#include "../pi/external_image_color.hpp"
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture.hpp>

#include <cstdio>
#include <mutex>
#include <unordered_map>

using namespace systems::leal::campello_gpu;

namespace {

const char *kShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 pos [[position]]; float2 uv; };

vertex VOut campello_external_vs(uint vid [[vertex_id]]) {
    float2 p = float2((vid << 1) & 2, vid & 2);
    VOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);
    return o;
}

struct Params {
    float4 rows[3];
    float4 uvRow0;
    float4 uvRow1;
    uint planes;
    uint srgbTarget;
};

fragment float4 campello_external_fs(VOut in [[stage_in]], constant Params &P [[buffer(0)]],
                                     texture2d<float> t0 [[texture(0)]],
                                     texture2d<float> t1 [[texture(1)]],
                                     texture2d<float> t2 [[texture(2)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 uv1 = float3(in.uv, 1.0);
    float2 uv = float2(dot(P.uvRow0.xyz, uv1), dot(P.uvRow1.xyz, uv1));
    float3 c;
    if (P.planes == 1) {
        c = t0.sample(s, uv).rgb;
    } else if (P.planes == 2) {
        c = float3(t0.sample(s, uv).r, t1.sample(s, uv).rg);
    } else {
        c = float3(t0.sample(s, uv).r, t1.sample(s, uv).r, t2.sample(s, uv).r);
    }
    float4 x = float4(c, 1.0);
    float3 rgb = saturate(float3(dot(P.rows[0], x), dot(P.rows[1], x), dot(P.rows[2], x)));
    if (P.srgbTarget != 0) {
        // The target re-encodes to sRGB on write: hand it the linear value so
        // it stores the source's (already non-linear) bytes.
        rgb = select(pow((rgb + 0.055) / 1.055, 2.4), rgb / 12.92, rgb <= 0.04045);
    }
    return float4(rgb, 1.0);
}
)";

struct Params {
    float rows[3][4];
    float uvRow0[4];
    float uvRow1[4];
    uint32_t planes;
    uint32_t srgbTarget;
    uint32_t pad[2];
};

struct MetalExternalImageConverter {
    std::mutex mutex;
    CVMetalTextureCacheRef cache = nullptr;
    id<MTLFunction> vertex = nil;
    id<MTLFunction> fragment = nil;
    std::unordered_map<uint64_t, id<MTLRenderPipelineState>> pipelines;  // by target format
    bool failed = false;
};

std::mutex gConverterCreation;

MetalExternalImageConverter *converterFor(MetalDeviceData *d) {
    std::lock_guard<std::mutex> lock(gConverterCreation);
    if (d->externalImageConverter) return static_cast<MetalExternalImageConverter *>(d->externalImageConverter);
    auto *c = new MetalExternalImageConverter();
    id<MTLDevice> device = (__bridge id<MTLDevice>)(void *)d->device;
    if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &c->cache) != kCVReturnSuccess) {
        fprintf(stderr, "[Metal] copyExternalImageToTexture: CVMetalTextureCacheCreate failed\n");
        c->failed = true;
    }
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:@(kShaderSource) options:nil error:&error];
    if (!library) {
        fprintf(stderr, "[Metal] copyExternalImageToTexture: shader compilation failed: %s\n",
                error ? error.localizedDescription.UTF8String : "?");
        c->failed = true;
    } else {
        c->vertex = [library newFunctionWithName:@"campello_external_vs"];
        c->fragment = [library newFunctionWithName:@"campello_external_fs"];
    }
    d->externalImageConverter = c;
    return c;
}

id<MTLRenderPipelineState> pipelineFor(MetalExternalImageConverter *c, id<MTLDevice> device, MTLPixelFormat format) {
    auto it = c->pipelines.find(uint64_t(format));
    if (it != c->pipelines.end()) return it->second;
    MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
    desc.label = @"campello_gpu external image copy";
    desc.vertexFunction = c->vertex;
    desc.fragmentFunction = c->fragment;
    desc.colorAttachments[0].pixelFormat = format;
    NSError *error = nil;
    id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!state) {
        fprintf(stderr, "[Metal] copyExternalImageToTexture: pipeline creation failed: %s\n",
                error ? error.localizedDescription.UTF8String : "?");
        return nil;
    }
    c->pipelines[uint64_t(format)] = state;
    return state;
}

YCbCrMatrix matrixOf(CVPixelBufferRef buffer) {
    CFTypeRef value = nullptr;
    if (@available(macOS 12.0, iOS 15.0, tvOS 15.0, *)) {
        value = CVBufferCopyAttachment(buffer, kCVImageBufferYCbCrMatrixKey, nullptr);
    }
    YCbCrMatrix m = YCbCrMatrix::fromSource;
    if (value) {
        if (CFEqual(value, kCVImageBufferYCbCrMatrix_ITU_R_709_2)) m = YCbCrMatrix::bt709;
        else if (CFEqual(value, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) m = YCbCrMatrix::bt601;
        CFRelease(value);
    }
    return m;
}

bool isSupportedTarget(MTLPixelFormat f, bool &srgb) {
    srgb = f == MTLPixelFormatRGBA8Unorm_sRGB || f == MTLPixelFormatBGRA8Unorm_sRGB;
    return srgb || f == MTLPixelFormatRGBA8Unorm || f == MTLPixelFormatBGRA8Unorm;
}

// Records the copy into `commandBuffer`. No encoder may be open on it.
bool encodeCopy(id<MTLCommandBuffer> commandBuffer, const ExternalImage &source, MetalTextureHandle *target,
                const ExternalImageCopyOptions &options) {
    if (source.type != ExternalImageType::cvPixelBuffer || !source.handle) {
        fprintf(stderr, "[Metal] copyExternalImageToTexture: only ExternalImageType::cvPixelBuffer is supported\n");
        return false;
    }
    if (!target || !target->texture || !target->deviceData) return false;
    id<MTLTexture> out = (__bridge id<MTLTexture>)(void *)target->texture;
    bool srgb = false;
    if (out.textureType != MTLTextureType2D || !(out.usage & MTLTextureUsageRenderTarget) ||
        !isSupportedTarget(out.pixelFormat, srgb)) {
        fprintf(stderr, "[Metal] copyExternalImageToTexture: the target must be a 2D render-target texture in "
                        "rgba8unorm, bgra8unorm or their _srgb variants\n");
        return false;
    }

    MetalDeviceData *d = target->deviceData;
    MetalExternalImageConverter *c = converterFor(d);
    if (c->failed) return false;
    std::lock_guard<std::mutex> lock(c->mutex);
    id<MTLDevice> device = (__bridge id<MTLDevice>)(void *)d->device;
    id<MTLRenderPipelineState> pipeline = pipelineFor(c, device, out.pixelFormat);
    if (!pipeline) return false;

    auto buffer = static_cast<CVPixelBufferRef>(source.handle);
    const OSType format = CVPixelBufferGetPixelFormatType(buffer);
    struct Plane { MTLPixelFormat format; };
    Plane planes[3]{};
    uint32_t planeCount = 0;
    YCbCrRange range = YCbCrRange::limited;
    switch (format) {
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange: range = YCbCrRange::full; [[fallthrough]];
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
            planes[0].format = MTLPixelFormatR8Unorm;
            planes[1].format = MTLPixelFormatRG8Unorm;
            planeCount = 2;
            break;
        case kCVPixelFormatType_420YpCbCr8PlanarFullRange: range = YCbCrRange::full; [[fallthrough]];
        case kCVPixelFormatType_420YpCbCr8Planar:
            planes[0].format = planes[1].format = planes[2].format = MTLPixelFormatR8Unorm;
            planeCount = 3;
            break;
        case kCVPixelFormatType_32BGRA:
            planes[0].format = MTLPixelFormatBGRA8Unorm;
            planeCount = 1;
            break;
        default:
            fprintf(stderr, "[Metal] copyExternalImageToTexture: unsupported CVPixelBuffer format '%c%c%c%c'\n",
                    char(format >> 24), char(format >> 16), char(format >> 8), char(format));
            return false;
    }

    CVMetalTextureCacheFlush(c->cache, 0);
    CVMetalTextureRef planeTextures[3] = {nullptr, nullptr, nullptr};
    id<MTLTexture> textures[3] = {nil, nil, nil};
    const bool planar = CVPixelBufferIsPlanar(buffer);
    for (uint32_t i = 0; i < planeCount; ++i) {
        const size_t w = planar ? CVPixelBufferGetWidthOfPlane(buffer, i) : CVPixelBufferGetWidth(buffer);
        const size_t h = planar ? CVPixelBufferGetHeightOfPlane(buffer, i) : CVPixelBufferGetHeight(buffer);
        CVReturn r = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, c->cache, buffer, nullptr,
                                                               planes[i].format, w, h, planar ? i : 0,
                                                               &planeTextures[i]);
        if (r == kCVReturnSuccess) textures[i] = CVMetalTextureGetTexture(planeTextures[i]);
        if (!textures[i]) {
            fprintf(stderr, "[Metal] copyExternalImageToTexture: CVPixelBuffer import failed (%d); is it "
                            "IOSurface-backed (kCVPixelBufferMetalCompatibilityKey)?\n", int(r));
            for (auto t : planeTextures) if (t) CFRelease(t);
            return false;
        }
    }

    Params params{};
    if (planeCount == 1) {
        identityRgbRows(params.rows);
    } else {
        const auto height = uint32_t(CVPixelBufferGetHeight(buffer));
        yCbCrToRgbRows(resolveYCbCrMatrix(source.matrix, matrixOf(buffer), height),
                       resolveYCbCrRange(source.range, range), params.rows);
    }
    static const float kIdentity[6] = {1, 0, 0, 0, 1, 0};
    const float *m = options.uvTransform ? options.uvTransform : kIdentity;
    params.uvRow0[0] = m[0]; params.uvRow0[1] = m[1]; params.uvRow0[2] = m[2];
    params.uvRow1[0] = m[3]; params.uvRow1[1] = m[4]; params.uvRow1[2] = m[5];
    params.planes = planeCount;
    params.srgbTarget = srgb ? 1 : 0;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = out;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
        for (auto t : planeTextures) if (t) CFRelease(t);
        return false;
    }
    encoder.label = @"campello_gpu external image copy";
    [encoder setRenderPipelineState:pipeline];
    [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
    for (uint32_t i = 0; i < 3; ++i) [encoder setFragmentTexture:textures[i] ? textures[i] : textures[0] atIndex:i];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];

    // The pixel buffer and its Metal textures must outlive the GPU's use.
    CVPixelBufferRetain(buffer);
    CVMetalTextureRef t0 = planeTextures[0], t1 = planeTextures[1], t2 = planeTextures[2];
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
        if (t0) CFRelease(t0);
        if (t1) CFRelease(t1);
        if (t2) CFRelease(t2);
        CVPixelBufferRelease(buffer);
    }];
    return true;
}

} // namespace

void systems::leal::campello_gpu::destroyMetalExternalImageConverter(MetalDeviceData *d) {
    auto *c = static_cast<MetalExternalImageConverter *>(d->externalImageConverter);
    if (!c) return;
    if (c->cache) CFRelease(c->cache);
    delete c;
    d->externalImageConverter = nullptr;
}

bool Device::supportsExternalImage(ExternalImageType type) const {
    return type == ExternalImageType::cvPixelBuffer;
}

bool Device::copyExternalImageToTexture(const ExternalImage &source, std::shared_ptr<Texture> target,
                                        const ExternalImageCopyOptions &options) {
    if (!native || !target) return false;
    @autoreleasepool {
        auto *d = static_cast<MetalDeviceData *>(native);
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)(void *)d->commandQueue;
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        commandBuffer.label = @"campello_gpu copyExternalImageToTexture";
        if (!encodeCopy(commandBuffer, source, static_cast<MetalTextureHandle *>(target->native), options)) {
            return false;
        }
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "[Metal] copyExternalImageToTexture: command buffer failed: %s\n",
                    commandBuffer.error ? commandBuffer.error.localizedDescription.UTF8String : "?");
            return false;
        }
        return true;
    }
}

bool CommandEncoder::copyExternalImageToTexture(const ExternalImage &source, std::shared_ptr<Texture> target,
                                                const ExternalImageCopyOptions &options) {
    if (!native || !target) return false;
    @autoreleasepool {
        id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)native;
        return encodeCopy(commandBuffer, source, static_cast<MetalTextureHandle *>(target->native), options);
    }
}
