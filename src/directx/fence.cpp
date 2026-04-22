#include "common.hpp"
#include <campello_gpu/fence.hpp>
#include <windows.h>

using namespace systems::leal::campello_gpu;

Fence::Fence(void *pd) : native(pd) {}

Fence::~Fence() {
    if (native != nullptr) {
        auto *data = static_cast<DirectXFenceData *>(native);
        if (data->fenceEvent) CloseHandle(data->fenceEvent);
        if (data->fence) data->fence->Release();
        delete data;
    }
}

bool Fence::wait(uint64_t timeoutNs) {
    if (native == nullptr) return true;
    auto *data = static_cast<DirectXFenceData *>(native);

    if (data->fence->GetCompletedValue() >= data->value) return true;

    DWORD timeoutMs = (timeoutNs == UINT64_MAX)
                          ? INFINITE
                          : static_cast<DWORD>(timeoutNs / 1'000'000);

    data->fence->SetEventOnCompletion(data->value, data->fenceEvent);
    DWORD result = WaitForSingleObject(data->fenceEvent, timeoutMs);
    return (result == WAIT_OBJECT_0);
}

bool Fence::isSignaled() const {
    if (native == nullptr) return true;
    auto *data = static_cast<DirectXFenceData *>(native);
    return data->fence->GetCompletedValue() >= data->value;
}
