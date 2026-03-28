#include "Metal.hpp"
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>

using namespace systems::leal::campello_gpu;

Texture::Texture(void *pd) {
    native = pd;
}

Texture::~Texture() {
    if (native != nullptr) {
        ((MTL::Texture *)native)->release();
    }
}

PixelFormat Texture::getFormat() {
    return (PixelFormat)((MTL::Texture *)native)->pixelFormat();
}

uint32_t Texture::getWidth() {
    return (uint32_t)((MTL::Texture *)native)->width();
}

uint32_t Texture::getHeight() {
    return (uint32_t)((MTL::Texture *)native)->height();
}

uint32_t Texture::getDepthOrarrayLayers() {
    return (uint32_t)((MTL::Texture *)native)->depth();
}

TextureType Texture::getDimension() {
    switch (((MTL::Texture *)native)->textureType()) {
        case MTL::TextureType1D:            return TextureType::tt1d;
        case MTL::TextureType3D:            return TextureType::tt3d;
        case MTL::TextureType2D:
        case MTL::TextureType2DMultisample:
        default:                            return TextureType::tt2d;
    }
}

uint32_t Texture::getMipLevelCount() {
    return (uint32_t)((MTL::Texture *)native)->mipmapLevelCount();
}

uint32_t Texture::getSampleCount() {
    return (uint32_t)((MTL::Texture *)native)->sampleCount();
}

TextureUsage Texture::getUsage() {
    MTL::TextureUsage mtlUsage = ((MTL::Texture *)native)->usage();
    int result = 0;
    if (mtlUsage & MTL::TextureUsageShaderRead)   result |= static_cast<int>(TextureUsage::textureBinding);
    if (mtlUsage & MTL::TextureUsageShaderWrite)  result |= static_cast<int>(TextureUsage::storageBinding);
    if (mtlUsage & MTL::TextureUsageRenderTarget) result |= static_cast<int>(TextureUsage::renderTarget);
    return static_cast<TextureUsage>(result);
}

bool Texture::upload(uint64_t offset, uint64_t length, void *data) {
    auto *tex    = static_cast<MTL::Texture *>(native);
    uint32_t w   = (uint32_t)tex->width();
    uint32_t h   = (uint32_t)tex->height();
    uint32_t d   = (uint32_t)tex->depth();

    // Compute bytes-per-row from the pixel format size stored in the texture.
    // We use the raw length / height as the row stride since callers are expected
    // to pass tightly-packed data for mip level 0.
    NS::UInteger bytesPerRow   = (h > 0) ? (length / h) : length;
    NS::UInteger bytesPerImage = length;

    MTL::Region region = MTL::Region::Make3D(0, 0, 0, w, h, d);
    tex->replaceRegion(region, 0, 0, data, bytesPerRow, bytesPerImage);
    return true;
}

bool Texture::download(uint32_t mipLevel, uint32_t arrayLayer, void *data, uint64_t length) {
    if (!native || !data || length == 0) return false;
    auto *tex = static_cast<MTL::Texture *>(native);
    auto *device = tex->device();
    if (!device) return false;

    // Get the command queue from the device
    auto *commandQueue = device->newCommandQueue();
    if (!commandQueue) return false;

    // Create a readback buffer
    auto *readbackBuf = device->newBuffer(length, MTL::ResourceStorageModeShared);
    if (!readbackBuf) {
        commandQueue->release();
        return false;
    }

    // Create a command buffer
    auto *cmdBuffer = commandQueue->commandBuffer();
    if (!cmdBuffer) {
        readbackBuf->release();
        commandQueue->release();
        return false;
    }

    // Create a blit encoder
    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    if (!blitEncoder) {
        cmdBuffer->release();
        readbackBuf->release();
        commandQueue->release();
        return false;
    }

    // Calculate region size for the mip level
    uint32_t width  = std::max(1u, (uint32_t)tex->width() >> mipLevel);
    uint32_t height = std::max(1u, (uint32_t)tex->height() >> mipLevel);
    uint32_t depth  = std::max(1u, (uint32_t)tex->depth() >> mipLevel);

    MTL::Origin origin = MTL::Origin::Make(0, 0, 0);
    MTL::Size   size   = MTL::Size::Make(width, height, depth);

    // Bytes per row calculation
    NS::UInteger bytesPerRow = length / (height > 0 ? height : 1);

    // Copy from texture to buffer
    blitEncoder->copyFromTexture(
        tex, arrayLayer, mipLevel, origin, size,
        readbackBuf, 0, bytesPerRow, bytesPerRow * height);

    // Synchronize for CPU read access
    blitEncoder->synchronizeResource(readbackBuf);
    blitEncoder->endEncoding();

    // Commit and wait
    cmdBuffer->commit();
    cmdBuffer->waitUntilCompleted();

    // Copy data from buffer
    memcpy(data, readbackBuf->contents(), length);

    readbackBuf->release();
    cmdBuffer->release();
    commandQueue->release();
    return true;
}

std::shared_ptr<TextureView> Texture::createView(
    PixelFormat format,
    uint32_t arrayLayerCount,
    Aspect aspect,
    uint32_t baseArrayLayer,
    uint32_t baseMipLevel,
    TextureType dimension) {

    auto *tex = static_cast<MTL::Texture *>(native);

    MTL::TextureType mtlType;
    switch (dimension) {
        case TextureType::tt1d: mtlType = MTL::TextureType1D;  break;
        case TextureType::tt3d: mtlType = MTL::TextureType3D;  break;
        default:                mtlType = MTL::TextureType2D;  break;
    }

    NS::Range mipRange   = NS::Range::Make(baseMipLevel, tex->mipmapLevelCount() - baseMipLevel);
    NS::Range sliceRange = NS::Range::Make(baseArrayLayer, arrayLayerCount > 0 ? arrayLayerCount : 1);

    auto *view = tex->newTextureView(
        (MTL::PixelFormat)format, mtlType, mipRange, sliceRange);
    if (!view) return nullptr;
    return std::shared_ptr<TextureView>(new TextureView(view));
}
