#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include "common.hpp"
#include "command_encoder_handle.hpp"
#include "command_buffer_handle.hpp"
#include "render_pass_encoder_handle.hpp"
#include "compute_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include "query_set_handle.hpp"

using namespace systems::leal::campello_gpu;

CommandEncoder::CommandEncoder(void* pd) {
    native = pd;
}

CommandEncoder::~CommandEncoder() {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    if (handle->encoder) {
        wgpuCommandEncoderRelease(handle->encoder);
    }
    if (handle->timestampQuerySet) {
        wgpuQuerySetRelease(handle->timestampQuerySet);
    }
    if (handle->timestampReadbackBuffer) {
        wgpuBufferRelease(handle->timestampReadbackBuffer);
    }
    delete handle;
}

std::shared_ptr<ComputePassEncoder> CommandEncoder::beginComputePass() {
    auto* handle = static_cast<CommandEncoderHandle*>(native);

    WGPUComputePassDescriptor desc{};
    WGPUComputePassEncoder encoder = wgpuCommandEncoderBeginComputePass(handle->encoder, &desc);
    if (!encoder) return nullptr;

    auto* passHandle = new ComputePassEncoderHandle();
    passHandle->encoder = encoder;
    passHandle->deviceData = handle->deviceData;

    if (handle->deviceData) {
        handle->deviceData->computePasses++;
    }

    return std::shared_ptr<ComputePassEncoder>(new ComputePassEncoder(passHandle));
}

std::shared_ptr<RenderPassEncoder> CommandEncoder::beginRenderPass(const BeginRenderPassDescriptor& descriptor) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);

    std::vector<WGPURenderPassColorAttachment> colorAttachments;
    colorAttachments.reserve(descriptor.colorAttachments.size());

    for (const auto& ca : descriptor.colorAttachments) {
        WGPURenderPassColorAttachment wgpuCa{};
        wgpuCa.view = static_cast<TextureViewHandle*>(ca.view->native)->view;
        wgpuCa.loadOp = toWGPULoadOp(ca.loadOp);
        wgpuCa.storeOp = toWGPUStoreOp(ca.storeOp);
        wgpuCa.clearValue.r = ca.clearValue[0];
        wgpuCa.clearValue.g = ca.clearValue[1];
        wgpuCa.clearValue.b = ca.clearValue[2];
        wgpuCa.clearValue.a = ca.clearValue[3];
        if (ca.resolveTarget) {
            wgpuCa.resolveTarget = static_cast<TextureViewHandle*>(ca.resolveTarget->native)->view;
        }
        colorAttachments.push_back(wgpuCa);
    }

    WGPURenderPassDescriptor desc{};
    desc.colorAttachmentCount = colorAttachments.size();
    desc.colorAttachments = colorAttachments.data();

    std::unique_ptr<WGPURenderPassDepthStencilAttachment> dsAttachment;
    if (descriptor.depthStencilAttachment) {
        dsAttachment = std::make_unique<WGPURenderPassDepthStencilAttachment>();
        *dsAttachment = WGPURenderPassDepthStencilAttachment{};
        dsAttachment->view = static_cast<TextureViewHandle*>(descriptor.depthStencilAttachment->view->native)->view;
        dsAttachment->depthLoadOp = toWGPULoadOp(descriptor.depthStencilAttachment->depthLoadOp);
        dsAttachment->depthStoreOp = toWGPUStoreOp(descriptor.depthStencilAttachment->depthStoreOp);
        dsAttachment->depthClearValue = descriptor.depthStencilAttachment->depthClearValue;
        dsAttachment->depthReadOnly = descriptor.depthStencilAttachment->depthReadOnly;
        dsAttachment->stencilLoadOp = toWGPULoadOp(descriptor.depthStencilAttachment->stencilLoadOp);
        dsAttachment->stencilStoreOp = toWGPUStoreOp(descriptor.depthStencilAttachment->stencilStoreOp);
        dsAttachment->stencilClearValue = descriptor.depthStencilAttachment->stencilClearValue;
        dsAttachment->stencilReadOnly = descriptor.depthStencilAttachment->stencilReadOnly;
        desc.depthStencilAttachment = dsAttachment.get();
    }

    if (descriptor.occlusionQuerySet) {
        desc.occlusionQuerySet = static_cast<QuerySetHandle*>(descriptor.occlusionQuerySet->native)->querySet;
    }

    WGPURenderPassTimestampWrites timestampWrites{};
    if (!descriptor.timestampWrites.empty()) {
        const auto& tw = descriptor.timestampWrites[0];
        timestampWrites.querySet = static_cast<QuerySetHandle*>(tw.querySet->native)->querySet;
        timestampWrites.beginningOfPassWriteIndex = tw.beginningOfPassWriteIndex;
        timestampWrites.endOfPassWriteIndex = tw.endOfPassWriteIndex;
        desc.timestampWrites = &timestampWrites;
    }

    WGPURenderPassEncoder encoder = wgpuCommandEncoderBeginRenderPass(handle->encoder, &desc);
    if (!encoder) return nullptr;

    auto* passHandle = new RenderPassEncoderHandle();
    passHandle->encoder = encoder;
    passHandle->deviceData = handle->deviceData;

    if (handle->deviceData) {
        handle->deviceData->renderPasses++;
    }

    return std::shared_ptr<RenderPassEncoder>(new RenderPassEncoder(passHandle));
}

void CommandEncoder::clearBuffer(std::shared_ptr<Buffer> buffer, uint64_t offset, uint64_t size) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* bufHandle = static_cast<BufferHandle*>(buffer->native);
    wgpuCommandEncoderClearBuffer(handle->encoder, bufHandle->buffer, offset, size);
}

void CommandEncoder::copyBufferToBuffer(std::shared_ptr<Buffer> source, uint64_t sourceOffset,
                                        std::shared_ptr<Buffer> destination, uint64_t destinationOffset,
                                        uint64_t size) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* srcHandle = static_cast<BufferHandle*>(source->native);
    auto* dstHandle = static_cast<BufferHandle*>(destination->native);
    wgpuCommandEncoderCopyBufferToBuffer(handle->encoder, srcHandle->buffer, sourceOffset,
                                         dstHandle->buffer, destinationOffset, size);
    if (handle->deviceData) {
        handle->deviceData->copies++;
    }
}

void CommandEncoder::copyBufferToTexture(std::shared_ptr<Buffer> source, uint64_t sourceOffset,
                                         uint64_t bytesPerRow, std::shared_ptr<Texture> destination,
                                         uint32_t mipLevel, uint32_t arrayLayer) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* srcHandle = static_cast<BufferHandle*>(source->native);
    auto* dstHandle = static_cast<TextureHandle*>(destination->native);

    uint32_t mipW = std::max(1u, dstHandle->width >> mipLevel);
    uint32_t mipH = std::max(1u, dstHandle->height >> mipLevel);

    WGPUImageCopyBuffer srcInfo{};
    srcInfo.buffer = srcHandle->buffer;
    srcInfo.layout.offset = sourceOffset;
    srcInfo.layout.bytesPerRow = bytesPerRow;
    srcInfo.layout.rowsPerImage = mipH;

    WGPUImageCopyTexture dstInfo{};
    dstInfo.texture = dstHandle->texture;
    dstInfo.mipLevel = mipLevel;
    dstInfo.origin = { 0, 0, arrayLayer };
    dstInfo.aspect = WGPUTextureAspect_All;

    WGPUExtent3D extent{};
    extent.width = mipW;
    extent.height = mipH;
    extent.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyBufferToTexture(handle->encoder, &srcInfo, &dstInfo, &extent);
    if (handle->deviceData) {
        handle->deviceData->copies++;
    }
}

void CommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> source, uint32_t mipLevel,
                                         uint32_t arrayLayer, std::shared_ptr<Buffer> destination,
                                         uint64_t destinationOffset, uint64_t bytesPerRow) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* srcHandle = static_cast<TextureHandle*>(source->native);
    auto* dstHandle = static_cast<BufferHandle*>(destination->native);

    uint32_t mipW = std::max(1u, srcHandle->width >> mipLevel);
    uint32_t mipH = std::max(1u, srcHandle->height >> mipLevel);

    WGPUImageCopyTexture srcInfo{};
    srcInfo.texture = srcHandle->texture;
    srcInfo.mipLevel = mipLevel;
    srcInfo.origin = { 0, 0, arrayLayer };
    srcInfo.aspect = WGPUTextureAspect_All;

    WGPUImageCopyBuffer dstInfo{};
    dstInfo.buffer = dstHandle->buffer;
    dstInfo.layout.offset = destinationOffset;
    dstInfo.layout.bytesPerRow = bytesPerRow;
    dstInfo.layout.rowsPerImage = mipH;

    WGPUExtent3D extent{};
    extent.width = mipW;
    extent.height = mipH;
    extent.depthOrArrayLayers = 1;

    wgpuCommandEncoderCopyTextureToBuffer(handle->encoder, &srcInfo, &dstInfo, &extent);
    if (handle->deviceData) {
        handle->deviceData->copies++;
    }
}

void CommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> source, uint32_t srcMipLevel,
                                          const Offset3D& sourceOffset, std::shared_ptr<Texture> destination,
                                          uint32_t dstMipLevel, const Offset3D& destinationOffset,
                                          const Extent3D& extent) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* srcHandle = static_cast<TextureHandle*>(source->native);
    auto* dstHandle = static_cast<TextureHandle*>(destination->native);

    WGPUImageCopyTexture srcInfo{};
    srcInfo.texture = srcHandle->texture;
    srcInfo.mipLevel = srcMipLevel;
    srcInfo.origin = { static_cast<uint32_t>(sourceOffset.x), static_cast<uint32_t>(sourceOffset.y), static_cast<uint32_t>(sourceOffset.z) };
    srcInfo.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture dstInfo{};
    dstInfo.texture = dstHandle->texture;
    dstInfo.mipLevel = dstMipLevel;
    dstInfo.origin = { static_cast<uint32_t>(destinationOffset.x), static_cast<uint32_t>(destinationOffset.y), static_cast<uint32_t>(destinationOffset.z) };
    dstInfo.aspect = WGPUTextureAspect_All;

    WGPUExtent3D ext{};
    ext.width = extent.width;
    ext.height = extent.height;
    ext.depthOrArrayLayers = extent.depth;

    wgpuCommandEncoderCopyTextureToTexture(handle->encoder, &srcInfo, &dstInfo, &ext);
    if (handle->deviceData) {
        handle->deviceData->copies++;
    }
}

static bool isFilterableColorFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::r8unorm:
        case PixelFormat::r8snorm:
        case PixelFormat::rg8unorm:
        case PixelFormat::rg8snorm:
        case PixelFormat::rgba8unorm:
        case PixelFormat::rgba8unorm_srgb:
        case PixelFormat::rgba8snorm:
        case PixelFormat::bgra8unorm:
        case PixelFormat::bgra8unorm_srgb:
        case PixelFormat::r16float:
        case PixelFormat::rg16float:
        case PixelFormat::rgba16float:
        case PixelFormat::r32float:
        case PixelFormat::rgb10a2unorm:
        case PixelFormat::rg11b10ufloat:
        case PixelFormat::rgb9e5ufloat:
            return true;
        default:
            return false;
    }
}

static WGPUShaderModule createWGSLShaderModule(WGPUDevice device, const char* code) {
    WGPUShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = code;

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    return wgpuDeviceCreateShaderModule(device, &desc);
}

static void releaseMipmapGenResources(MipmapGenResources& res) {
    if (res.sampler) { wgpuSamplerRelease(res.sampler); res.sampler = nullptr; }
    if (res.pipelineLayout) { wgpuPipelineLayoutRelease(res.pipelineLayout); res.pipelineLayout = nullptr; }
    if (res.bgl) { wgpuBindGroupLayoutRelease(res.bgl); res.bgl = nullptr; }
    if (res.fsModule) { wgpuShaderModuleRelease(res.fsModule); res.fsModule = nullptr; }
    if (res.vsModule) { wgpuShaderModuleRelease(res.vsModule); res.vsModule = nullptr; }
}

static bool initMipmapGenResources(WebGPUDeviceData* deviceData) {
    if (deviceData->mipmapGen.vsModule && deviceData->mipmapGen.fsModule &&
        deviceData->mipmapGen.bgl && deviceData->mipmapGen.pipelineLayout &&
        deviceData->mipmapGen.sampler) {
        return true;
    }

    // Clean up any partial state from a previous failed attempt
    releaseMipmapGenResources(deviceData->mipmapGen);

    const char* vsCode = R"(
        @vertex
        fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4f {
            var pos = array<vec2f, 3>(
                vec2f(-1.0, -1.0),
                vec2f( 3.0, -1.0),
                vec2f(-1.0,  3.0)
            );
            return vec4f(pos[vertexIndex], 0.0, 1.0);
        }
    )";

    const char* fsCode = R"(
        @group(0) @binding(0) var srcTexture: texture_2d<f32>;
        @group(0) @binding(1) var srcSampler: sampler;

        @fragment
        fn fs_main(@builtin(position) fragCoord: vec4f) -> @location(0) vec4f {
            let srcSize = vec2f(textureDimensions(srcTexture, 0));
            let uv = fragCoord.xy / (srcSize * 0.5);
            return textureSample(srcTexture, srcSampler, uv);
        }
    )";

    deviceData->mipmapGen.vsModule = createWGSLShaderModule(deviceData->device, vsCode);
    deviceData->mipmapGen.fsModule = createWGSLShaderModule(deviceData->device, fsCode);
    if (!deviceData->mipmapGen.vsModule || !deviceData->mipmapGen.fsModule) {
        releaseMipmapGenResources(deviceData->mipmapGen);
        return false;
    }

    WGPUBindGroupLayoutEntry bglEntries[2] = {};
    bglEntries[0].binding = 0;
    bglEntries[0].visibility = WGPUShaderStage_Fragment;
    bglEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
    bglEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    bglEntries[1].binding = 1;
    bglEntries[1].visibility = WGPUShaderStage_Fragment;
    bglEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 2;
    bglDesc.entries = bglEntries;
    deviceData->mipmapGen.bgl = wgpuDeviceCreateBindGroupLayout(deviceData->device, &bglDesc);
    if (!deviceData->mipmapGen.bgl) {
        releaseMipmapGenResources(deviceData->mipmapGen);
        return false;
    }

    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &deviceData->mipmapGen.bgl;
    deviceData->mipmapGen.pipelineLayout = wgpuDeviceCreatePipelineLayout(deviceData->device, &plDesc);
    if (!deviceData->mipmapGen.pipelineLayout) {
        releaseMipmapGenResources(deviceData->mipmapGen);
        return false;
    }

    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    deviceData->mipmapGen.sampler = wgpuDeviceCreateSampler(deviceData->device, &samplerDesc);
    if (!deviceData->mipmapGen.sampler) {
        releaseMipmapGenResources(deviceData->mipmapGen);
        return false;
    }

    return true;
}

bool CommandEncoder::generateMipmaps(std::shared_ptr<Texture> texture) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* texHandle = static_cast<TextureHandle*>(texture->native);
    if (!handle->deviceData || !texHandle->texture) return false;

    // Only 2D textures with multiple mip levels are supported
    if (texHandle->mipLevels <= 1) return false;
    if (texHandle->textureType != TextureType::tt2d) return false;

    // Must have both render target and texture binding usage
    bool hasRenderTarget = (static_cast<uint32_t>(texHandle->usage) & static_cast<uint32_t>(TextureUsage::renderTarget)) != 0;
    bool hasTextureBinding = (static_cast<uint32_t>(texHandle->usage) & static_cast<uint32_t>(TextureUsage::textureBinding)) != 0;
    if (!hasRenderTarget || !hasTextureBinding) return false;

    // Must be a filterable color format
    if (!isFilterableColorFormat(texHandle->pixelFormat)) return false;

    auto* deviceData = handle->deviceData;
    if (!initMipmapGenResources(deviceData)) return false;

    WGPUTextureFormat wgpuFormat = toWGPUTextureFormat(texHandle->pixelFormat);

    // Create render pipeline for this specific format
    WGPUVertexState vertexState{};
    vertexState.module = deviceData->mipmapGen.vsModule;
    vertexState.entryPoint = "vs_main";

    WGPUFragmentState fragmentState{};
    fragmentState.module = deviceData->mipmapGen.fsModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    WGPUColorTargetState colorTarget{};
    colorTarget.format = wgpuFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = deviceData->mipmapGen.pipelineLayout;
    pipelineDesc.vertex = vertexState;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(deviceData->device, &pipelineDesc);
    if (!pipeline) return false;
    deviceData->renderPipelineCount++;

    bool anyMipGenerated = false;

    for (uint32_t mip = 1; mip < texHandle->mipLevels; mip++) {
        uint32_t srcMip = mip - 1;

        // Create view for destination mip level (render attachment)
        WGPUTextureViewDescriptor dstViewDesc{};
        dstViewDesc.format = wgpuFormat;
        dstViewDesc.dimension = WGPUTextureViewDimension_2D;
        dstViewDesc.baseMipLevel = mip;
        dstViewDesc.mipLevelCount = 1;
        dstViewDesc.baseArrayLayer = 0;
        dstViewDesc.arrayLayerCount = 1;
        dstViewDesc.aspect = WGPUTextureAspect_All;
        WGPUTextureView dstView = wgpuTextureCreateView(texHandle->texture, &dstViewDesc);
        if (!dstView) continue;

        // Create view for source mip level (sampled)
        WGPUTextureViewDescriptor srcViewDesc{};
        srcViewDesc.format = wgpuFormat;
        srcViewDesc.dimension = WGPUTextureViewDimension_2D;
        srcViewDesc.baseMipLevel = srcMip;
        srcViewDesc.mipLevelCount = 1;
        srcViewDesc.baseArrayLayer = 0;
        srcViewDesc.arrayLayerCount = 1;
        srcViewDesc.aspect = WGPUTextureAspect_All;
        WGPUTextureView srcView = wgpuTextureCreateView(texHandle->texture, &srcViewDesc);
        if (!srcView) {
            wgpuTextureViewRelease(dstView);
            continue;
        }

        // Create bind group for this pass
        WGPUBindGroupEntry bgEntries[2] = {};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = srcView;
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = deviceData->mipmapGen.sampler;

        WGPUBindGroupDescriptor bgDesc{};
        bgDesc.layout = deviceData->mipmapGen.bgl;
        bgDesc.entryCount = 2;
        bgDesc.entries = bgEntries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(deviceData->device, &bgDesc);

        WGPURenderPassColorAttachment colorAttach{};
        colorAttach.view = dstView;
        colorAttach.loadOp = WGPULoadOp_Clear;
        colorAttach.storeOp = WGPUStoreOp_Store;
        colorAttach.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};

        WGPURenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &colorAttach;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(handle->encoder, &rpDesc);
        if (pass) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            deviceData->renderPasses++;
            deviceData->drawCalls++;
            anyMipGenerated = true;
        }

        if (bindGroup) wgpuBindGroupRelease(bindGroup);
        wgpuTextureViewRelease(srcView);
        wgpuTextureViewRelease(dstView);
    }

    wgpuRenderPipelineRelease(pipeline);
    return anyMipGenerated;
}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {
    auto* handle = static_cast<CommandEncoderHandle*>(native);

    // Write end timestamp and resolve query data before finishing
    if (handle->timestampQuerySet && handle->timestampReadbackBuffer) {
        wgpuCommandEncoderWriteTimestamp(handle->encoder, handle->timestampQuerySet, 1);
        wgpuCommandEncoderResolveQuerySet(handle->encoder, handle->timestampQuerySet, 0, 2,
                                          handle->timestampReadbackBuffer, 0);
    }

    WGPUCommandBufferDescriptor desc{};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(handle->encoder, &desc);
    if (!commandBuffer) return nullptr;

    auto* cbHandle = new CommandBufferHandle();
    cbHandle->commandBuffer = commandBuffer;
    cbHandle->deviceData = handle->deviceData;

    // Transfer timing resources to command buffer
    if (handle->timestampQuerySet && handle->timestampReadbackBuffer) {
        cbHandle->timestampReadbackBuffer = handle->timestampReadbackBuffer;
        cbHandle->hasTimingData = true;
        handle->timestampReadbackBuffer = nullptr;
        wgpuQuerySetRelease(handle->timestampQuerySet);
        handle->timestampQuerySet = nullptr;
    }

    // Release the encoder since it's now finished
    wgpuCommandEncoderRelease(handle->encoder);
    handle->encoder = nullptr;

    return std::shared_ptr<CommandBuffer>(new CommandBuffer(cbHandle));
}

void CommandEncoder::resolveQuerySet(std::shared_ptr<QuerySet> querySet, uint32_t firstQuery,
                                     uint32_t queryCount, std::shared_ptr<Buffer> destination,
                                     uint64_t destinationOffset) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* qsHandle = static_cast<QuerySetHandle*>(querySet->native);
    auto* dstHandle = static_cast<BufferHandle*>(destination->native);
    wgpuCommandEncoderResolveQuerySet(handle->encoder, qsHandle->querySet, firstQuery, queryCount,
                                      dstHandle->buffer, destinationOffset);
}

void CommandEncoder::writeTimestamp(std::shared_ptr<QuerySet> querySet, uint32_t queryIndex) {
    auto* handle = static_cast<CommandEncoderHandle*>(native);
    auto* qsHandle = static_cast<QuerySetHandle*>(querySet->native);
    wgpuCommandEncoderWriteTimestamp(handle->encoder, qsHandle->querySet, queryIndex);
}

std::shared_ptr<RayTracingPassEncoder> CommandEncoder::beginRayTracingPass() {
    return nullptr; // Ray tracing not supported in WebGPU
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const BottomLevelAccelerationStructureDescriptor& descriptor,
    std::shared_ptr<Buffer> scratchBuffer) {
    (void)dst;
    (void)descriptor;
    (void)scratchBuffer;
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const TopLevelAccelerationStructureDescriptor& descriptor,
    std::shared_ptr<Buffer> scratchBuffer) {
    (void)dst;
    (void)descriptor;
    (void)scratchBuffer;
}

void CommandEncoder::updateAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst,
    std::shared_ptr<Buffer> scratchBuffer) {
    (void)src;
    (void)dst;
    (void)scratchBuffer;
}

void CommandEncoder::copyAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst) {
    (void)src;
    (void)dst;
}
