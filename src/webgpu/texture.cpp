#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"
#include "common.hpp"
#include <algorithm>
#include <cstring>

using namespace systems::leal::campello_gpu;

Texture::Texture(void* pd) {
    native = pd;
}

Texture::~Texture() {
    auto* handle = static_cast<TextureHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->textureCount--;
        handle->deviceData->textureBytes.fetch_sub(handle->allocatedSize);
    }
    if (handle->defaultView) {
        wgpuTextureViewRelease(handle->defaultView);
    }
    if (handle->texture) {
        wgpuTextureRelease(handle->texture);
    }
    delete handle;
}

std::shared_ptr<TextureView> Texture::createView(
    PixelFormat format,
    uint32_t arrayLayerCount,
    Aspect aspect,
    uint32_t baseArrayLayer,
    uint32_t baseMipLevel,
    TextureType dimension,
    uint32_t mipLevelCount) {

    auto* handle = static_cast<TextureHandle*>(native);

    WGPUTextureViewDescriptor desc{};
    desc.format = toWGPUTextureFormat(format);
    desc.baseMipLevel = baseMipLevel;
    desc.mipLevelCount = (mipLevelCount == static_cast<uint32_t>(-1))
                          ? (handle->mipLevels - baseMipLevel)
                          : mipLevelCount;
    desc.baseArrayLayer = baseArrayLayer;
    desc.arrayLayerCount = (arrayLayerCount == static_cast<uint32_t>(-1))
                           ? (handle->arrayLayers - baseArrayLayer)
                           : arrayLayerCount;
    if (handle->textureType == TextureType::tt3d) desc.arrayLayerCount = 1;
    desc.dimension = toWGPUTextureViewDimension(dimension, desc.arrayLayerCount);

    switch (aspect) {
        case Aspect::depthOnly: desc.aspect = WGPUTextureAspect_DepthOnly; break;
        case Aspect::stencilOnly: desc.aspect = WGPUTextureAspect_StencilOnly; break;
        case Aspect::all: default: desc.aspect = WGPUTextureAspect_All; break;
    }

    WGPUTextureView view = wgpuTextureCreateView(handle->texture, &desc);
    if (!view) return nullptr;

    auto* viewHandle = new TextureViewHandle();
    viewHandle->view = view;
    viewHandle->ownsView = true;
    viewHandle->format = (desc.format != WGPUTextureFormat_Undefined)
                         ? desc.format : toWGPUTextureFormat(handle->pixelFormat);
    return std::shared_ptr<TextureView>(new TextureView(viewHandle));
}

bool Texture::upload(uint64_t offset, uint64_t length, void* data) {
    auto* handle = static_cast<TextureHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->queue) return false;

    bool compressed = isCompressedFormat(handle->pixelFormat);
    auto blockDim = compressed ? getPixelFormatBlockDimensions(handle->pixelFormat)
                               : PixelFormatBlockDimensions{1, 1};
    uint32_t blockBytes = compressed ? getPixelFormatBlockBytes(handle->pixelFormat)
                                     : getPixelFormatSize(handle->pixelFormat) / 8;

    uint64_t bufOffset = offset;
    uint32_t w = handle->width;
    uint32_t h = handle->height;
    uint32_t d = handle->depth;

    for (uint32_t mip = 0; mip < handle->mipLevels; mip++) {
        uint32_t blocksX = (w + blockDim.width - 1) / blockDim.width;
        uint32_t blocksY = (h + blockDim.height - 1) / blockDim.height;
        uint64_t mipSize = static_cast<uint64_t>(blocksX) * blocksY * d * blockBytes;
        if (bufOffset + mipSize > offset + length) break;

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = handle->texture;
        dst.mipLevel = mip;
        dst.origin = { 0, 0, 0 };
        dst.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.offset = bufOffset;
        layout.bytesPerRow = blocksX * blockBytes;
        layout.rowsPerImage = blocksY;

        WGPUExtent3D extent{};
        extent.width = w;
        extent.height = h;
        extent.depthOrArrayLayers = d;

        wgpuQueueWriteTexture(handle->deviceData->queue, &dst,
                              static_cast<uint8_t*>(data) + bufOffset, mipSize,
                              &layout, &extent);

        bufOffset += mipSize;
        if (w > 1) w /= 2;
        if (h > 1) h /= 2;
        if (d > 1) d /= 2;
    }

    return true;
}

// One mip/layer readback: the texture is copied into a MapRead buffer whose
// rows are padded to WebGPU's 256-byte bytesPerRow alignment, then unpadded
// into the caller's tightly packed buffer.
struct TextureReadback {
    WGPUBuffer buffer = nullptr;
    uint32_t blockRows = 0;
    uint32_t unpaddedBytesPerRow = 0;
    uint32_t paddedBytesPerRow = 0;
    uint64_t bufferSize = 0;
};

static bool submitTextureReadback(TextureHandle* handle, uint32_t mipLevel, uint32_t arrayLayer,
                                  uint64_t length, TextureReadback& rb) {
    bool compressed = isCompressedFormat(handle->pixelFormat);
    auto blockDim = compressed ? getPixelFormatBlockDimensions(handle->pixelFormat)
                               : PixelFormatBlockDimensions{1, 1};
    uint32_t blockBytes = compressed ? getPixelFormatBlockBytes(handle->pixelFormat)
                                     : getPixelFormatSize(handle->pixelFormat) / 8;

    uint32_t mipW = std::max(1u, handle->width >> mipLevel);
    uint32_t mipH = std::max(1u, handle->height >> mipLevel);
    uint32_t blocksX = (mipW + blockDim.width - 1) / blockDim.width;
    uint32_t blocksY = (mipH + blockDim.height - 1) / blockDim.height;

    rb.blockRows = blocksY;
    rb.unpaddedBytesPerRow = blocksX * blockBytes;
    rb.paddedBytesPerRow = ((rb.unpaddedBytesPerRow + 255) / 256) * 256;
    rb.bufferSize = static_cast<uint64_t>(rb.paddedBytesPerRow) * blocksY;

    if (length < static_cast<uint64_t>(rb.unpaddedBytesPerRow) * blocksY) return false;

    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = rb.bufferSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    rb.buffer = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!rb.buffer) return false;

    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);

    WGPUTexelCopyTextureInfo src{};
    src.texture = handle->texture;
    src.mipLevel = mipLevel;
    src.origin = { 0, 0, arrayLayer };
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = rb.buffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = rb.paddedBytesPerRow;
    dst.layout.rowsPerImage = blocksY;

    WGPUExtent3D extent{};
    extent.width = mipW;
    extent.height = mipH;
    extent.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    return true;
}

static bool copyOutReadback(const TextureReadback& rb, void* data) {
    const uint8_t* mapped = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(rb.buffer, 0, static_cast<size_t>(rb.bufferSize)));
    if (mapped) {
        uint8_t* out = static_cast<uint8_t*>(data);
        for (uint32_t row = 0; row < rb.blockRows; ++row) {
            memcpy(out + static_cast<size_t>(row) * rb.unpaddedBytesPerRow,
                   mapped + static_cast<size_t>(row) * rb.paddedBytesPerRow,
                   rb.unpaddedBytesPerRow);
        }
    }
    wgpuBufferUnmap(rb.buffer);
    return mapped != nullptr;
}

bool Texture::download(uint32_t mipLevel, uint32_t arrayLayer, void* data, uint64_t length) {
    auto* handle = static_cast<TextureHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) return false;

    TextureReadback rb;
    if (!submitTextureReadback(handle, mipLevel, arrayLayer, length, rb)) {
        if (rb.buffer) wgpuBufferRelease(rb.buffer);
        return false;
    }

    // mapAsync resolves once the GPU has finished the submitted copy.
    WGPUAsyncResult result;
    wgpuBufferMapAsync(rb.buffer, WGPUMapMode_Read, 0, static_cast<size_t>(rb.bufferSize),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(&result));
    waitForAsync(result);

    bool ok = result.status == static_cast<int>(WGPUMapAsyncStatus_Success) &&
              copyOutReadback(rb, data);
    wgpuBufferRelease(rb.buffer);
    return ok;
}

void Texture::downloadAsync(uint32_t mipLevel, uint32_t arrayLayer, void* data, uint64_t length,
                            std::function<void(bool)> callback) {
    auto* handle = static_cast<TextureHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) {
        callback(false);
        return;
    }

    TextureReadback rb;
    if (!submitTextureReadback(handle, mipLevel, arrayLayer, length, rb)) {
        if (rb.buffer) wgpuBufferRelease(rb.buffer);
        callback(false);
        return;
    }

    auto* result = newAsyncResult([rb, data, callback = std::move(callback)](WGPUAsyncResult& r) {
        bool ok = r.status == static_cast<int>(WGPUMapAsyncStatus_Success) &&
                  copyOutReadback(rb, data);
        wgpuBufferRelease(rb.buffer);
        callback(ok);
    });
    wgpuBufferMapAsync(rb.buffer, WGPUMapMode_Read, 0, static_cast<size_t>(rb.bufferSize),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(result));
}

uint32_t Texture::getDepthOrarrayLayers() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->depth;
}

TextureType Texture::getDimension() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->textureType;
}

PixelFormat Texture::getFormat() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->pixelFormat;
}

uint32_t Texture::getWidth() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->width;
}

uint32_t Texture::getHeight() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->height;
}

uint32_t Texture::getMipLevelCount() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->mipLevels;
}

uint32_t Texture::getSampleCount() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->samples;
}

TextureUsage Texture::getUsage() {
    auto* handle = static_cast<TextureHandle*>(native);
    return handle->usage;
}
