#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/buffer.hpp>
#include <cstring>

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void* pd) : native(pd) {}

Buffer::~Buffer() {
    if (!native) return;
    auto* h = static_cast<BufferHandle*>(native);
    if (h->resource) {
        if (h->mapped) h->resource->Unmap(0, nullptr);
        h->resource->Release();
    }
    delete h;
}

uint64_t Buffer::getLength() {
    if (!native) return 0;
    return static_cast<BufferHandle*>(native)->size;
}

bool Buffer::upload(uint64_t offset, uint64_t length, void* data) {
    if (!native || !data) return false;
    auto* h = static_cast<BufferHandle*>(native);
    if (!h->mapped) return false;
    if (offset + length > h->size) return false;
    std::memcpy(static_cast<uint8_t*>(h->mapped) + offset, data, length);
    return true;
}

bool Buffer::download(uint64_t offset, uint64_t length, void* data) {
    if (!native || !data) return false;
    auto* h = static_cast<BufferHandle*>(native);
    if (!h->resource) return false;
    if (offset + length > h->size) return false;

    // For readback heaps, map and copy directly
    if (h->isReadback && h->mapped) {
        std::memcpy(data, static_cast<uint8_t*>(h->mapped) + offset, length);
        return true;
    }

    // For upload/default heaps, we need to create a temporary readback buffer
    // and copy data to it first. This requires a command list and queue.
    if (!h->device || !h->queue) return false;

    // Create a readback buffer
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = (length + 255) & ~255ULL;  // align to 256 bytes
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* readbackBuf = nullptr;
    if (FAILED(h->device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readbackBuf)))) return false;

    // Create a command list to copy the data
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(h->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&alloc)))) {
        readbackBuf->Release();
        return false;
    }
    if (FAILED(h->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             alloc, nullptr, IID_PPV_ARGS(&list)))) {
        alloc->Release();
        readbackBuf->Release();
        return false;
    }

    // Copy from source buffer to readback buffer
    list->CopyBufferRegion(readbackBuf, 0, h->resource, offset, length);

    if (FAILED(list->Close())) {
        list->Release();
        alloc->Release();
        readbackBuf->Release();
        return false;
    }

    // Execute and wait
    ID3D12CommandList* lists[] = { list };
    h->queue->ExecuteCommandLists(1, lists);

    ID3D12Fence* fence = nullptr;
    h->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    h->queue->Signal(fence, 1);
    if (fence->GetCompletedValue() < 1) {
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }

    // Map and copy data
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(length) };
    if (SUCCEEDED(readbackBuf->Map(0, &readRange, &mapped))) {
        std::memcpy(data, mapped, length);
        readbackBuf->Unmap(0, nullptr);
    }

    fence->Release();
    list->Release();
    alloc->Release();
    readbackBuf->Release();
    return true;
}
