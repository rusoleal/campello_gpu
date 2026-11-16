// TemporalUpscaler on Metal: MetalFX's temporal scaler (MTLFXTemporalScaler,
// macOS 13 / iOS 16 and later; MetalFX is weak-linked, so older systems load
// the library and report no support). Objective-C++ with ARC: the backend's
// metal-cpp objects are the same Objective-C objects, bridged here.

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include "common.hpp"
#include "texture_handle.hpp"
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/temporal_upscaler.hpp>
#include <campello_gpu/texture.hpp>

using namespace systems::leal::campello_gpu;

namespace {

// The upscaler's native data: MetalFX's scaler, and a private-storage
// texture it writes (MetalFX requires private storage; campello_gpu's
// colour textures are shared / managed for upload and download), blitted
// into the caller's output.
struct MetalTemporalUpscaler {
    id scaler = nil;            // id<MTLFXTemporalScaler>
    id<MTLTexture> output = nil;
};

} // namespace

TemporalUpscaler::TemporalUpscaler(void *pd, const TemporalUpscalerDescriptor &descriptor)
    : native(pd), descriptor_(descriptor) {}

TemporalUpscaler::~TemporalUpscaler() {
    delete static_cast<MetalTemporalUpscaler *>(native);
}

TextureUsage TemporalUpscaler::outputUsage() const {
    uint32_t usage = 0;
    if (@available(macOS 13.0, iOS 16.0, *)) {
        id<MTLFXTemporalScaler> scaler = static_cast<MetalTemporalUpscaler *>(native)->scaler;
        const MTLTextureUsage u = scaler.outputTextureUsage;
        if (u & MTLTextureUsageRenderTarget) usage |= static_cast<uint32_t>(TextureUsage::renderTarget);
        if (u & MTLTextureUsageShaderWrite) usage |= static_cast<uint32_t>(TextureUsage::storageBinding);
        if (u & MTLTextureUsageShaderRead) usage |= static_cast<uint32_t>(TextureUsage::textureBinding);
    }
    return static_cast<TextureUsage>(usage);
}

bool Device::supportsTemporalUpscaler() {
    if (!native) return false;
    if (@available(macOS 13.0, iOS 16.0, *)) {
        id<MTLDevice> device = (__bridge id<MTLDevice>)static_cast<MetalDeviceData *>(native)->device;
        return [MTLFXTemporalScalerDescriptor supportsDevice:device];
    }
    return false;
}

std::shared_ptr<TemporalUpscaler> Device::createTemporalUpscaler(const TemporalUpscalerDescriptor &descriptor) {
    if (!supportsTemporalUpscaler() || descriptor.inputWidth == 0 || descriptor.inputHeight == 0 ||
        descriptor.outputWidth == 0 || descriptor.outputHeight == 0) {
        return nullptr;
    }
    if (@available(macOS 13.0, iOS 16.0, *)) {
        auto *deviceData = static_cast<MetalDeviceData *>(native);
        id<MTLDevice> device = (__bridge id<MTLDevice>)deviceData->device;
        MTLFXTemporalScalerDescriptor *d = [MTLFXTemporalScalerDescriptor new];
        d.inputWidth = descriptor.inputWidth;
        d.inputHeight = descriptor.inputHeight;
        d.outputWidth = descriptor.outputWidth;
        d.outputHeight = descriptor.outputHeight;
        d.colorTextureFormat = static_cast<MTLPixelFormat>(toMTLPixelFormat(deviceData->device, descriptor.colorFormat));
        d.depthTextureFormat = static_cast<MTLPixelFormat>(toMTLPixelFormat(deviceData->device, descriptor.depthFormat));
        d.motionTextureFormat = static_cast<MTLPixelFormat>(toMTLPixelFormat(deviceData->device, descriptor.motionFormat));
        d.outputTextureFormat = static_cast<MTLPixelFormat>(toMTLPixelFormat(deviceData->device, descriptor.outputFormat));
        id<MTLFXTemporalScaler> scaler = [d newTemporalScalerWithDevice:device];
        if (!scaler) return nullptr;
        auto *data = new MetalTemporalUpscaler();
        data->scaler = scaler;
        return std::shared_ptr<TemporalUpscaler>(new TemporalUpscaler(data, descriptor));
    }
    return nullptr;
}

void CommandEncoder::encodeTemporalUpscale(std::shared_ptr<TemporalUpscaler> upscaler, const TemporalUpscaleInputs &inputs) {
    if (!native || !upscaler || !upscaler->native || !inputs.color || !inputs.depth || !inputs.motion || !inputs.output) {
        return;
    }
    if (@available(macOS 13.0, iOS 16.0, *)) {
        const auto texture = [](const std::shared_ptr<Texture> &t) {
            return (__bridge id<MTLTexture>)static_cast<MetalTextureHandle *>(t->native)->texture;
        };
        auto *data = static_cast<MetalTemporalUpscaler *>(upscaler->native);
        id<MTLFXTemporalScaler> scaler = data->scaler;
        const auto &d = upscaler->getDescriptor();
        id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)native;
        id<MTLTexture> destination = texture(inputs.output);
        // MetalFX writes a private-storage texture of the output's size and
        // format; then a blit into the caller's (any storage mode).
        if (!data->output || data->output.width != destination.width || data->output.height != destination.height ||
            data->output.pixelFormat != destination.pixelFormat) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:destination.pixelFormat
                                                                                          width:destination.width
                                                                                         height:destination.height
                                                                                      mipmapped:NO];
            td.storageMode = MTLStorageModePrivate;
            td.usage = scaler.outputTextureUsage;
            data->output = [commandBuffer.device newTextureWithDescriptor:td];
            if (!data->output) return;
        }
        scaler.colorTexture = texture(inputs.color);
        scaler.depthTexture = texture(inputs.depth);
        scaler.motionTexture = texture(inputs.motion);
        scaler.outputTexture = data->output;
        scaler.inputContentWidth = d.inputWidth;
        scaler.inputContentHeight = d.inputHeight;
        scaler.jitterOffsetX = inputs.jitterX;
        scaler.jitterOffsetY = inputs.jitterY;
        scaler.motionVectorScaleX = inputs.motionScaleX;
        scaler.motionVectorScaleY = inputs.motionScaleY;
        scaler.reset = inputs.reset;
        scaler.depthReversed = d.depthReversed;
        [scaler encodeToCommandBuffer:commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        [blit copyFromTexture:data->output toTexture:destination];
        [blit endEncoding];
    }
}
