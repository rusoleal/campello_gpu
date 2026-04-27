#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"
#include "common.hpp"
#include <emscripten.h>
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
    TextureType dimension) {

    auto* handle = static_cast<TextureHandle*>(native);

    WGPUTextureViewDescriptor desc{};
    desc.format = toWGPUTextureFormat(format);
    desc.dimension = toWGPUTextureViewDimension(dimension);
    desc.baseMipLevel = baseMipLevel;
    desc.mipLevelCount = handle->mipLevels - baseMipLevel;
    desc.baseArrayLayer = baseArrayLayer;
    desc.arrayLayerCount = (arrayLayerCount == static_cast<uint32_t>(-1))
                           ? (handle->arrayLayers - baseArrayLayer)
                           : arrayLayerCount;

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

        WGPUImageCopyTexture dst{};
        dst.texture = handle->texture;
        dst.mipLevel = mip;
        dst.origin = { 0, 0, 0 };
        dst.aspect = WGPUTextureAspect_All;

        WGPUTextureDataLayout layout{};
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

// Helper for async buffer mapping with ASYNCIFY
struct MapContext {
    bool done = false;
    WGPUBufferMapAsyncStatus status = WGPUBufferMapAsyncStatus_Unknown;
};

static void onBufferMapped(WGPUBufferMapAsyncStatus status, void* userdata) {
    auto* ctx = static_cast<MapContext*>(userdata);
    ctx->status = status;
    ctx->done = true;
}

bool Texture::download(uint32_t mipLevel, uint32_t arrayLayer, void* data, uint64_t length) {
    auto* handle = static_cast<TextureHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) return false;

    bool compressed = isCompressedFormat(handle->pixelFormat);
    auto blockDim = compressed ? getPixelFormatBlockDimensions(handle->pixelFormat)
                               : PixelFormatBlockDimensions{1, 1};
    uint32_t blockBytes = compressed ? getPixelFormatBlockBytes(handle->pixelFormat)
                                     : getPixelFormatSize(handle->pixelFormat) / 8;

    uint32_t mipW = std::max(1u, handle->width >> mipLevel);
    uint32_t mipH = std::max(1u, handle->height >> mipLevel);

    uint32_t blocksX = (mipW + blockDim.width - 1) / blockDim.width;
    uint32_t blocksY = (mipH + blockDim.height - 1) / blockDim.height;

    // WebGPU requires bytesPerRow to be a multiple of 256 for copyTextureToBuffer.
    uint32_t unpaddedBytesPerRow = blocksX * blockBytes;
    uint32_t paddedBytesPerRow = ((unpaddedBytesPerRow + 255) / 256) * 256;
    uint64_t bufferSize = static_cast<uint64_t>(paddedBytesPerRow) * blocksY;

    if (length < static_cast<uint64_t>(unpaddedBytesPerRow) * blocksY) return false;

    // 1. Create readback buffer
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = bufferSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!readback) return false;

    // 2. Record copy texture -> buffer
    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);

    WGPUImageCopyTexture src{};
    src.texture = handle->texture;
    src.mipLevel = mipLevel;
    src.origin = { 0, 0, arrayLayer };
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyBuffer dst{};
    dst.buffer = readback;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = paddedBytesPerRow;
    dst.layout.rowsPerImage = blocksY;

    WGPUExtent3D extent{};
    extent.width = mipW;
    extent.height = mipH;
    extent.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    // 3. Submit and wait
    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    // Wait for GPU completion
    MapContext idleCtx;
    wgpuQueueOnSubmittedWorkDone(handle->deviceData->queue, 0,
        [](WGPUQueueWorkDoneStatus status, void* userdata) {
            (void)status;
            auto* ctx = static_cast<MapContext*>(userdata);
            ctx->done = true;
        }, &idleCtx);

    while (!idleCtx.done) {
        emscripten_sleep(1);
    }

    // 4. Map buffer and read data
    MapContext mapCtx;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bufferSize, onBufferMapped, &mapCtx);

    while (!mapCtx.done) {
        emscripten_sleep(1);
    }

    if (mapCtx.status != WGPUBufferMapAsyncStatus_Success) {
        wgpuBufferRelease(readback);
        return false;
    }

    const uint8_t* mapped = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, bufferSize));
    if (mapped) {
        // Copy row by row to handle potential padding
        uint8_t* out = static_cast<uint8_t*>(data);
        for (uint32_t row = 0; row < blocksY; ++row) {
            memcpy(out + row * unpaddedBytesPerRow, mapped + row * paddedBytesPerRow, unpaddedBytesPerRow);
        }
    }

    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);

    return mapped != nullptr;
}

// Async download — chains wgpuQueueOnSubmittedWorkDone -> wgpuBufferMapAsync -> user callback
struct TextureDownloadAsyncData {
    WGPUBuffer readback;
    void* data;
    uint32_t blockRows;
    uint32_t unpaddedBytesPerRow;
    uint32_t paddedBytesPerRow;
    uint64_t bufferSize;
    std::function<void(bool)> callback;
};

void Texture::downloadAsync(uint32_t mipLevel, uint32_t arrayLayer, void* data, uint64_t length,
                            std::function<void(bool)> callback) {
    auto* handle = static_cast<TextureHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) {
        callback(false);
        return;
    }

    bool compressed = isCompressedFormat(handle->pixelFormat);
    auto blockDim = compressed ? getPixelFormatBlockDimensions(handle->pixelFormat)
                               : PixelFormatBlockDimensions{1, 1};
    uint32_t blockBytes = compressed ? getPixelFormatBlockBytes(handle->pixelFormat)
                                     : getPixelFormatSize(handle->pixelFormat) / 8;

    uint32_t mipW = std::max(1u, handle->width >> mipLevel);
    uint32_t mipH = std::max(1u, handle->height >> mipLevel);

    uint32_t blocksX = (mipW + blockDim.width - 1) / blockDim.width;
    uint32_t blocksY = (mipH + blockDim.height - 1) / blockDim.height;

    uint32_t unpaddedBytesPerRow = blocksX * blockBytes;
    uint32_t paddedBytesPerRow = ((unpaddedBytesPerRow + 255) / 256) * 256;
    uint64_t bufferSize = static_cast<uint64_t>(paddedBytesPerRow) * blocksY;

    if (length < static_cast<uint64_t>(unpaddedBytesPerRow) * blocksY) {
        callback(false);
        return;
    }

    // 1. Create readback buffer
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = bufferSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!readback) {
        callback(false);
        return;
    }

    // 2. Record copy texture -> buffer
    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);

    WGPUImageCopyTexture src{};
    src.texture = handle->texture;
    src.mipLevel = mipLevel;
    src.origin = { 0, 0, arrayLayer };
    src.aspect = WGPUTextureAspect_All;

    WGPUImageCopyBuffer dst{};
    dst.buffer = readback;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = paddedBytesPerRow;
    dst.layout.rowsPerImage = blocksY;

    WGPUExtent3D extent{};
    extent.width = mipW;
    extent.height = mipH;
    extent.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    // 3. Submit
    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    // 4. Chain async
    auto* asyncData = new TextureDownloadAsyncData{
        readback, data, blocksY, unpaddedBytesPerRow, paddedBytesPerRow, bufferSize, std::move(callback)
    };

    wgpuQueueOnSubmittedWorkDone(handle->deviceData->queue, 0,
        [](WGPUQueueWorkDoneStatus, void* userdata) {
            auto* d = static_cast<TextureDownloadAsyncData*>(userdata);
            wgpuBufferMapAsync(d->readback, WGPUMapMode_Read, 0, d->bufferSize,
                [](WGPUBufferMapAsyncStatus status, void* userdata2) {
                    auto* d2 = static_cast<TextureDownloadAsyncData*>(userdata2);
                    if (status == WGPUBufferMapAsyncStatus_Success) {
                        const uint8_t* mapped = static_cast<const uint8_t*>(
                            wgpuBufferGetConstMappedRange(d2->readback, 0, d2->bufferSize));
                        if (mapped) {
                            uint8_t* out = static_cast<uint8_t*>(d2->data);
                            for (uint32_t row = 0; row < d2->blockRows; ++row) {
                                memcpy(out + row * d2->unpaddedBytesPerRow,
                                       mapped + row * d2->paddedBytesPerRow,
                                       d2->unpaddedBytesPerRow);
                            }
                            wgpuBufferUnmap(d2->readback);
                        }
                        d2->callback(mapped != nullptr);
                    } else {
                        d2->callback(false);
                    }
                    wgpuBufferRelease(d2->readback);
                    delete d2;
                }, d);
        }, asyncData);
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
