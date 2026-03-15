#include <android/log.h>
#include <vulkan/vulkan.h>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"

using namespace systems::leal::campello_gpu;

// Forward-declared; defined in device.cpp.
VkFormat pixelFormatToNative(systems::leal::campello_gpu::PixelFormat format);

Texture::Texture(void *pd) {
    this->native = pd;
}

Texture::~Texture() {
    auto handle = (TextureHandle *)native;
    handle->buffer = nullptr;
    if (handle->defaultView != VK_NULL_HANDLE) {
        vkDestroyImageView(handle->device, handle->defaultView, nullptr);
    }
    vkDestroyImage(handle->device, handle->image, nullptr);
    delete handle;
}

bool Texture::upload(uint64_t offset, uint64_t length, void *data) {
    auto handle = (TextureHandle *)native;
    return handle->buffer->upload(offset, length, data);
}

std::shared_ptr<TextureView> Texture::createView(PixelFormat format,
                                                   uint32_t    arrayLayerCount,
                                                   Aspect      aspect,
                                                   uint32_t    baseArrayLayer,
                                                   uint32_t    baseMipLevel,
                                                   TextureType dimension) {
    auto handle = (TextureHandle *)native;

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    switch (aspect) {
        case Aspect::depthOnly:   aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;   break;
        case Aspect::stencilOnly: aspectFlags = VK_IMAGE_ASPECT_STENCIL_BIT; break;
        default: break;
    }

    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    switch (dimension) {
        case TextureType::tt1d: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
        case TextureType::tt3d: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
        default: break;
    }

    uint32_t layerCount = (arrayLayerCount == static_cast<uint32_t>(-1))
                              ? VK_REMAINING_ARRAY_LAYERS
                              : arrayLayerCount;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = handle->image;
    viewInfo.viewType = viewType;
    viewInfo.format   = pixelFormatToNative(format);
    viewInfo.subresourceRange.aspectMask     = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel   = baseMipLevel;
    viewInfo.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    viewInfo.subresourceRange.layerCount     = layerCount;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(handle->device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        return nullptr;
    }

    auto vh = new TextureViewHandle();
    vh->device    = handle->device;
    vh->imageView = imageView;
    vh->owned     = true;
    return std::shared_ptr<TextureView>(new TextureView(vh));
}

uint32_t Texture::getDepthOrarrayLayers() {
    return ((TextureHandle *)native)->depth;
}

TextureType Texture::getDimension() {
    return ((TextureHandle *)native)->textureType;
}

PixelFormat Texture::getFormat() {
    return ((TextureHandle *)native)->pixelFormat;
}

uint32_t Texture::getWidth() {
    return ((TextureHandle *)native)->width;
}

uint32_t Texture::getHeight() {
    return ((TextureHandle *)native)->height;
}

uint32_t Texture::getMipLevelCount() {
    return ((TextureHandle *)native)->mipLevels;
}

uint32_t Texture::getSampleCount() {
    return ((TextureHandle *)native)->samples;
}

TextureUsage Texture::getUsage() {
    return ((TextureHandle *)native)->usage;
}
