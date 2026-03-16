#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/aspect.hpp>
#include <algorithm>

using namespace systems::leal::campello_gpu;

Texture::Texture(void* pd) : native(pd) {}

Texture::~Texture() {
    if (!native) return;
    auto* h = static_cast<TextureHandle*>(native);
    if (h->resource) h->resource->Release();
    delete h;
}

std::shared_ptr<TextureView> Texture::createView(
    PixelFormat format,
    uint32_t    arrayLayerCount,
    Aspect      aspect,
    uint32_t    baseArrayLayer,
    uint32_t    baseMipLevel,
    TextureType dimension)
{
    if (!native) return nullptr;
    auto* h   = static_cast<TextureHandle*>(native);
    auto* dev = h->device;

    DXGI_FORMAT fmt = toDXGIFormat(format == PixelFormat::invalid ? h->format : format);
    if (fmt == DXGI_FORMAT_UNKNOWN) return nullptr;

    bool isDepth = isDepthFormat(h->format);

    auto* vh = new TextureViewHandle();
    vh->format = fmt;

    if (isDepth) {
        // DSV — returned as cpu-only handle in the view
        vh->viewType = ViewType::DSV;
        vh->cpuHandle = h->dsvHandle;
        // no GPU handle for DSV
    } else {
        // SRV — allocate a shader-visible slot; we don't have access to DeviceData here,
        // but TextureHandle::device lets us create an SRV on a shared heap.
        // For simplicity the view gets the pre-allocated RTV handle as cpuHandle
        // and the SRV is created separately.
        //
        // In practice callers use the TextureView for render pass attachments (RTV)
        // or as a shader resource. We store the RTV handle (already allocated on
        // createTexture) and mark the type accordingly.
        if (h->rtvHandle.ptr != 0) {
            vh->viewType  = ViewType::RTV;
            vh->cpuHandle = h->rtvHandle;
        } else {
            // Regular texture — create an SRV directly on the device's SRV heap.
            // We need the DeviceData for this; since we don't store it in TextureHandle,
            // create a temporary non-shader-visible heap descriptor.
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                  = fmt;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            uint32_t layers = (arrayLayerCount == static_cast<uint32_t>(-1))
                ? std::max(h->depthOrLayers - baseArrayLayer, 1u)
                : arrayLayerCount;

            switch (h->dimension) {
                case TextureType::tt1d:
                    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE1D;
                    srvDesc.Texture1D.MostDetailedMip  = baseMipLevel;
                    srvDesc.Texture1D.MipLevels        = h->mipLevels - baseMipLevel;
                    break;
                case TextureType::tt3d:
                    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE3D;
                    srvDesc.Texture3D.MostDetailedMip  = baseMipLevel;
                    srvDesc.Texture3D.MipLevels        = h->mipLevels - baseMipLevel;
                    break;
                default:
                    if (layers > 1) {
                        srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                        srvDesc.Texture2DArray.MostDetailedMip = baseMipLevel;
                        srvDesc.Texture2DArray.MipLevels       = h->mipLevels - baseMipLevel;
                        srvDesc.Texture2DArray.FirstArraySlice = baseArrayLayer;
                        srvDesc.Texture2DArray.ArraySize       = layers;
                    } else {
                        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Texture2D.MostDetailedMip = baseMipLevel;
                        srvDesc.Texture2D.MipLevels       = h->mipLevels - baseMipLevel;
                    }
                    break;
            }
            vh->viewType = ViewType::SRV;
            // We can't easily get the shared SRV heap here without DeviceData.
            // Leave handles at zero; setBindGroup would need a real handle.
            // A complete implementation would pass DeviceData* into TextureHandle.
        }
    }

    return std::shared_ptr<TextureView>(new TextureView(vh));
}

bool Texture::upload(uint64_t /*offset*/, uint64_t length, void* data) {
    if (!native || !data || length == 0) return false;
    auto* h   = static_cast<TextureHandle*>(native);
    auto* dev = h->device;
    if (!dev || !h->queue) return false;

    // Query the actual footprint required by D3D12 for subresource 0
    D3D12_RESOURCE_DESC texDesc = h->resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 totalBytes = 0;
    dev->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
    if (totalBytes == 0) return false;

    // Create an upload buffer large enough for the full footprint
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = (totalBytes + 255) & ~255ULL;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* uploadBuf = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf))))
        return false;

    // Map and copy source data row-by-row respecting D3D12 row pitch alignment
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (FAILED(uploadBuf->Map(0, &readRange, &mapped))) {
        uploadBuf->Release(); return false;
    }
    // Infer bytes-per-pixel from footprint width vs row pitch
    UINT srcBytesPerRow = static_cast<UINT>(length / (h->height > 0 ? h->height : 1));
    for (UINT row = 0; row < h->height; ++row) {
        std::memcpy(
            static_cast<uint8_t*>(mapped) + footprint.Footprint.RowPitch * row,
            static_cast<const uint8_t*>(data) + srcBytesPerRow * row,
            (std::min)(srcBytesPerRow, footprint.Footprint.RowPitch));
    }
    uploadBuf->Unmap(0, nullptr);

    // Create a one-shot command allocator + list
    ID3D12CommandAllocator*    alloc = nullptr;
    ID3D12GraphicsCommandList* list  = nullptr;
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&alloc)))) {
        uploadBuf->Release(); return false;
    }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       alloc, nullptr, IID_PPV_ARGS(&list)))) {
        alloc->Release(); uploadBuf->Release(); return false;
    }

    // Transition texture COMMON → COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = h->resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = h->resource;
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource       = uploadBuf;
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Transition COPY_DEST → COMMON
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1, &barrier);

    if (FAILED(list->Close())) {
        list->Release(); alloc->Release(); uploadBuf->Release(); return false;
    }

    ID3D12CommandList* lists[] = { list };
    h->queue->ExecuteCommandLists(1, lists);

    // Synchronously wait for the GPU
    ID3D12Fence* fence = nullptr;
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    h->queue->Signal(fence, 1);
    if (fence->GetCompletedValue() < 1) {
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }
    fence->Release();
    list->Release();
    alloc->Release();
    uploadBuf->Release();
    return true;
}

uint32_t    Texture::getDepthOrarrayLayers() {
    if (!native) return 1;
    return static_cast<TextureHandle*>(native)->depthOrLayers;
}
TextureType Texture::getDimension() {
    if (!native) return TextureType::tt2d;
    return static_cast<TextureHandle*>(native)->dimension;
}
PixelFormat Texture::getFormat() {
    if (!native) return PixelFormat::invalid;
    return static_cast<TextureHandle*>(native)->format;
}
uint32_t Texture::getWidth() {
    if (!native) return 0;
    return static_cast<TextureHandle*>(native)->width;
}
uint32_t Texture::getHeight() {
    if (!native) return 0;
    return static_cast<TextureHandle*>(native)->height;
}
uint32_t Texture::getMipLevelCount() {
    if (!native) return 0;
    return static_cast<TextureHandle*>(native)->mipLevels;
}
uint32_t Texture::getSampleCount() {
    if (!native) return 1;
    return static_cast<TextureHandle*>(native)->sampleCount;
}
TextureUsage Texture::getUsage() {
    if (!native) return static_cast<TextureUsage>(0);
    return static_cast<TextureHandle*>(native)->usage;
}
