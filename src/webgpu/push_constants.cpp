// Push-constant emulation for the WebGPU backend (see common.hpp).

#include "common.hpp"

namespace systems::leal::campello_gpu {

WGPUBindGroupLayout pushConstantBindGroupLayout(WebGPUDeviceData* deviceData) {
    if (deviceData->pushConstantLayout) return deviceData->pushConstantLayout;
    WGPUBindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = true;
    entry.buffer.minBindingSize = 0;
    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = toStringView("campello_gpu push constants");
    desc.entryCount = 1;
    desc.entries = &entry;
    deviceData->pushConstantLayout = wgpuDeviceCreateBindGroupLayout(deviceData->device, &desc);
    return deviceData->pushConstantLayout;
}

bool appendPushConstantBlock(WebGPUDeviceData* deviceData, PushConstantStream& stream,
                             const uint8_t* block, WGPUBindGroup& bindGroup, uint32_t& offset) {
    if (!deviceData) return false;
    if (stream.chunks.empty() ||
        stream.chunks.back().size() + kPushConstantBlockSize > kPushConstantChunkSize) {
        stream.chunks.emplace_back();
        stream.chunks.back().reserve(kPushConstantChunkSize);
    }
    const size_t index = stream.chunks.size() - 1;

    auto& chunks = deviceData->pushConstantChunks;
    while (chunks.size() <= index) {
        WGPUBufferDescriptor bufferDesc{};
        bufferDesc.label = toStringView("campello_gpu push constants");
        bufferDesc.size = kPushConstantChunkSize;
        bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = wgpuDeviceCreateBuffer(deviceData->device, &bufferDesc);
        if (!buffer) return false;

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.offset = 0;
        entry.size = kPushConstantBlockSize;
        WGPUBindGroupDescriptor groupDesc{};
        groupDesc.layout = pushConstantBindGroupLayout(deviceData);
        groupDesc.entryCount = 1;
        groupDesc.entries = &entry;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(deviceData->device, &groupDesc);
        if (!group) {
            wgpuBufferRelease(buffer);
            return false;
        }
        chunks.push_back({buffer, group});
    }

    auto& data = stream.chunks.back();
    offset = static_cast<uint32_t>(data.size());
    data.insert(data.end(), block, block + kPushConstantBlockSize);
    bindGroup = chunks[index].bindGroup;
    return true;
}

void uploadPushConstants(WebGPUDeviceData* deviceData, const PushConstantStream& stream) {
    for (size_t i = 0; i < stream.chunks.size() && i < deviceData->pushConstantChunks.size(); ++i) {
        const auto& data = stream.chunks[i];
        if (data.empty()) continue;
        wgpuQueueWriteBuffer(deviceData->queue, deviceData->pushConstantChunks[i].buffer, 0,
                             data.data(), data.size());
    }
}

void releasePushConstantResources(WebGPUDeviceData* deviceData) {
    for (auto& chunk : deviceData->pushConstantChunks) {
        if (chunk.bindGroup) wgpuBindGroupRelease(chunk.bindGroup);
        if (chunk.buffer) wgpuBufferRelease(chunk.buffer);
    }
    deviceData->pushConstantChunks.clear();
    if (deviceData->pushConstantLayout) {
        wgpuBindGroupLayoutRelease(deviceData->pushConstantLayout);
        deviceData->pushConstantLayout = nullptr;
    }
}

} // namespace systems::leal::campello_gpu
