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
    // Not yet implemented for Metal textures.
    return false;
}

std::shared_ptr<TextureView> Texture::createView(
    PixelFormat format,
    uint32_t arrayLayerCount,
    Aspect aspect,
    uint32_t baseArrayLayer,
    uint32_t baseMipLevel,
    TextureType dimension) {
    // Not yet implemented for Metal textures.
    return nullptr;
}
