#include "Metal.hpp"
#include "campello_gpu_config.h"
#include "TargetConditionals.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <dispatch/dispatch.h>
#include <iostream>
#include <cmath>

using namespace systems::leal::campello_gpu;

struct MetalDeviceData {
    MTL::Device       *device;
    MTL::CommandQueue *commandQueue;
};

// --- Mapping helpers ---

static MTL::SamplerAddressMode toMTLAddressMode(WrapMode mode) {
    switch (mode) {
        case WrapMode::repeat:       return MTL::SamplerAddressModeRepeat;
        case WrapMode::mirrorRepeat: return MTL::SamplerAddressModeMirrorRepeat;
        default:                     return MTL::SamplerAddressModeClampToEdge;
    }
}

static MTL::SamplerMinMagFilter toMTLFilter(FilterMode mode) {
    switch (mode) {
        case FilterMode::fmLinear:
        case FilterMode::fmLinearMipmapNearest:
        case FilterMode::fmLinearMipmapLinear:  return MTL::SamplerMinMagFilterLinear;
        default:                                return MTL::SamplerMinMagFilterNearest;
    }
}

static MTL::SamplerMipFilter toMTLMipFilter(FilterMode mode) {
    switch (mode) {
        case FilterMode::fmNearestMipmapNearest:
        case FilterMode::fmLinearMipmapNearest:  return MTL::SamplerMipFilterNearest;
        case FilterMode::fmNearestMipmapLinear:
        case FilterMode::fmLinearMipmapLinear:   return MTL::SamplerMipFilterLinear;
        default:                                 return MTL::SamplerMipFilterNotMipmapped;
    }
}

static MTL::CompareFunction toMTLCompare(CompareOp op) {
    switch (op) {
        case CompareOp::never:        return MTL::CompareFunctionNever;
        case CompareOp::less:         return MTL::CompareFunctionLess;
        case CompareOp::equal:        return MTL::CompareFunctionEqual;
        case CompareOp::lessEqual:    return MTL::CompareFunctionLessEqual;
        case CompareOp::greater:      return MTL::CompareFunctionGreater;
        case CompareOp::notEqual:     return MTL::CompareFunctionNotEqual;
        case CompareOp::greaterEqual: return MTL::CompareFunctionGreaterEqual;
        default:                      return MTL::CompareFunctionAlways;
    }
}

static MTL::VertexFormat toMTLVertexFormat(ComponentType comp, AccessorType acc) {
    switch (acc) {
        case AccessorType::acScalar:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt;
                case ComponentType::ctByte:          return MTL::VertexFormatChar;
                case ComponentType::ctUnsignedByte:  return MTL::VertexFormatUChar;
                case ComponentType::ctShort:         return MTL::VertexFormatShort;
                case ComponentType::ctUnsignedShort: return MTL::VertexFormatUShort;
                default:                             return MTL::VertexFormatFloat;
            }
        case AccessorType::acVec2:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat2;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt2;
                case ComponentType::ctByte:          return MTL::VertexFormatChar2;
                case ComponentType::ctUnsignedByte:  return MTL::VertexFormatUChar2;
                case ComponentType::ctShort:         return MTL::VertexFormatShort2;
                case ComponentType::ctUnsignedShort: return MTL::VertexFormatUShort2;
                default:                             return MTL::VertexFormatFloat2;
            }
        case AccessorType::acVec3:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat3;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt3;
                case ComponentType::ctByte:          return MTL::VertexFormatChar3;
                case ComponentType::ctUnsignedByte:  return MTL::VertexFormatUChar3;
                case ComponentType::ctShort:         return MTL::VertexFormatShort3;
                case ComponentType::ctUnsignedShort: return MTL::VertexFormatUShort3;
                default:                             return MTL::VertexFormatFloat3;
            }
        case AccessorType::acVec4:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat4;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt4;
                case ComponentType::ctByte:          return MTL::VertexFormatChar4;
                case ComponentType::ctUnsignedByte:  return MTL::VertexFormatUChar4;
                case ComponentType::ctShort:         return MTL::VertexFormatShort4;
                case ComponentType::ctUnsignedShort: return MTL::VertexFormatUShort4;
                default:                             return MTL::VertexFormatFloat4;
            }
        default:
            return MTL::VertexFormatFloat;
    }
}

// --- Device ---

Device::Device(void *pd) {
    auto *deviceData = new MetalDeviceData();
    deviceData->device       = static_cast<MTL::Device *>(pd);
    deviceData->commandQueue = deviceData->device->newCommandQueue();
    native = deviceData;

    auto *dev = deviceData->device;
    std::cout << "  - name: " << getName() << std::endl;
    std::cout << "  - supportsRayTracing: " << dev->supportsRaytracing() << std::endl;
    std::cout << "  - supports32BitMSAA: " << dev->supports32BitMSAA() << std::endl;
    std::cout << "  - supportsBCTextureCompression: " << dev->supportsBCTextureCompression() << std::endl;
    std::cout << "  - hasUnifiedMemory: " << dev->hasUnifiedMemory() << std::endl;
    std::cout << "  - currentAllocatedSize: " << dev->currentAllocatedSize() / (1024 * 1024.0) << "Mb." << std::endl;
    std::cout << "  - recommendedMaxWorkingSetSize: " << dev->recommendedMaxWorkingSetSize() / (1024 * 1024.0) << "Mb." << std::endl;
}

Device::~Device() {
    if (native != nullptr) {
        auto *deviceData = static_cast<MetalDeviceData *>(native);
        if (deviceData->commandQueue) deviceData->commandQueue->release();
        if (deviceData->device)       deviceData->device->release();
        delete deviceData;
        std::cout << "Device::~Device()" << std::endl;
    }
}

std::shared_ptr<Device> Device::createDefaultDevice(void *pd) {
    auto *device = MTL::CreateSystemDefaultDevice();
    if (device == nullptr) return nullptr;
    std::cout << "Device::createDefaultDevice()" << std::endl;
    return std::shared_ptr<Device>(new Device(device));
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> adapter, void *pd) {
    if (adapter == nullptr || adapter->native == nullptr) return nullptr;
    auto *mtlDevice = static_cast<MTL::Device *>(adapter->native);
    mtlDevice->retain();
    return std::shared_ptr<Device>(new Device(mtlDevice));
}

std::vector<std::shared_ptr<Adapter>> Device::getAdapters() {
    std::vector<std::shared_ptr<Adapter>> toReturn;
    auto *devices = MTL::CopyAllDevices();
    for (NS::UInteger i = 0; i < devices->count(); ++i) {
        auto *mtlDevice = static_cast<MTL::Device *>(devices->object(i));
        Adapter *adapter = new Adapter();
        adapter->native = mtlDevice;
        toReturn.push_back(std::shared_ptr<Adapter>(adapter));
    }
    devices->release();
    return toReturn;
}

std::string Device::getName() {
    return static_cast<MetalDeviceData *>(native)->device->name()->utf8String();
}

std::set<Feature> Device::getFeatures() {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    std::set<Feature> toReturn;
    if (dev->supportsRaytracing())
        toReturn.insert(Feature::raytracing);
    if (dev->supports32BitMSAA())
        toReturn.insert(Feature::msaa32bit);
    if (dev->supportsBCTextureCompression())
        toReturn.insert(Feature::bcTextureCompression);
    if (__builtin_available(macOS 10.11, *)) {
        if (dev->depth24Stencil8PixelFormatSupported())
            toReturn.insert(Feature::depth24Stencil8PixelFormat);
    }
    return toReturn;
}

std::string Device::getEngineVersion() {
    MTL::Device *d = MTL::CreateSystemDefaultDevice();
    if (!d) return "Metal";
    std::string result = "Metal";
#if defined(MTL_GPU_FAMILY_METAL3) || defined(__MAC_13_0) || defined(__IPHONE_16_0)
    if (d->supportsFamily(MTL::GPUFamilyMetal3)) result = "Metal 3";
    else
#endif
    if (d->supportsFamily(MTL::GPUFamilyMac2))   result = "Metal 2";
    else                                          result = "Metal 1";
    d->release();
    return result;
}

std::shared_ptr<Texture> Device::createTexture(
    TextureType type, PixelFormat pixelFormat,
    uint32_t width, uint32_t height, uint32_t depth,
    uint32_t mipLevels, uint32_t samples, TextureUsage usageMode) {

    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    auto *pTextureDesc = MTL::TextureDescriptor::alloc()->init();
    pTextureDesc->setWidth(width);
    pTextureDesc->setHeight(height);
    pTextureDesc->setDepth(depth > 0 ? depth : 1);
    pTextureDesc->setMipmapLevelCount(mipLevels > 0 ? mipLevels : 1);
    pTextureDesc->setSampleCount(samples > 0 ? samples : 1);
    pTextureDesc->setPixelFormat((MTL::PixelFormat)pixelFormat);
    pTextureDesc->setStorageMode(MTL::StorageModeManaged);

    switch (type) {
        case TextureType::tt1d:
            pTextureDesc->setTextureType(MTL::TextureType1D);
            break;
        case TextureType::tt2d:
            pTextureDesc->setTextureType(
                samples > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
            break;
        case TextureType::tt3d:
            pTextureDesc->setTextureType(MTL::TextureType3D);
            break;
    }

    MTL::TextureUsage mtlUsage = MTL::TextureUsageUnknown;
    const int u = static_cast<int>(usageMode);
    if (u & static_cast<int>(TextureUsage::textureBinding))
        mtlUsage |= MTL::TextureUsageShaderRead;
    if (u & static_cast<int>(TextureUsage::storageBinding))
        mtlUsage |= MTL::TextureUsageShaderWrite;
    if (u & static_cast<int>(TextureUsage::renderTarget))
        mtlUsage |= MTL::TextureUsageRenderTarget;
    pTextureDesc->setUsage(mtlUsage);

    MTL::Texture *pTexture = dev->newTexture(pTextureDesc);
    pTextureDesc->release();
    if (pTexture == nullptr) return nullptr;
    return std::shared_ptr<Texture>(new Texture(pTexture));
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    MTL::ResourceOptions options;
    const int u = static_cast<int>(usage);
    if (u & (static_cast<int>(BufferUsage::mapRead) | static_cast<int>(BufferUsage::mapWrite))) {
        options = MTL::ResourceStorageModeShared;
    } else {
        options = MTL::ResourceStorageModeManaged;
    }
    MTL::Buffer *pBuffer = dev->newBuffer(size, options);
    if (pBuffer == nullptr) return nullptr;
    return std::shared_ptr<Buffer>(new Buffer(pBuffer));
}

std::shared_ptr<ShaderModule> Device::createShaderModule(const uint8_t *buffer, uint64_t size) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    dispatch_data_t data = dispatch_data_create(
        buffer, (size_t)size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NS::Error *error = nullptr;
    auto *library = dev->newLibrary(data, &error);
    dispatch_release(data);
    if (!library) {
        if (error) std::cerr << "createShaderModule: "
                             << error->localizedDescription()->utf8String() << std::endl;
        return nullptr;
    }
    return std::shared_ptr<ShaderModule>(new ShaderModule(library));
}

std::shared_ptr<RenderPipeline> Device::createRenderPipeline(const RenderPipelineDescriptor &descriptor) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    auto *pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();

    // Vertex function
    if (descriptor.vertex.module) {
        auto *library  = static_cast<MTL::Library *>(descriptor.vertex.module->native);
        auto *funcName = NS::String::string(descriptor.vertex.entryPoint.c_str(), NS::UTF8StringEncoding);
        auto *fn       = library->newFunction(funcName);
        pipelineDesc->setVertexFunction(fn);
        if (fn) fn->release();
    }

    // Fragment function + color attachments
    if (descriptor.fragment && descriptor.fragment->module) {
        auto *library  = static_cast<MTL::Library *>(descriptor.fragment->module->native);
        auto *funcName = NS::String::string(descriptor.fragment->entryPoint.c_str(), NS::UTF8StringEncoding);
        auto *fn       = library->newFunction(funcName);
        pipelineDesc->setFragmentFunction(fn);
        if (fn) fn->release();

        for (size_t i = 0; i < descriptor.fragment->targets.size(); i++) {
            pipelineDesc->colorAttachments()->object(i)->setPixelFormat(
                (MTL::PixelFormat)descriptor.fragment->targets[i].format);
        }
    }

    // Depth attachment pixel format
    if (descriptor.depthStencil) {
        pipelineDesc->setDepthAttachmentPixelFormat(
            (MTL::PixelFormat)descriptor.depthStencil->format);
    }

    // Vertex descriptor
    if (!descriptor.vertex.buffers.empty()) {
        auto *vertexDesc = MTL::VertexDescriptor::alloc()->init();
        for (size_t bufIdx = 0; bufIdx < descriptor.vertex.buffers.size(); bufIdx++) {
            const auto &layout = descriptor.vertex.buffers[bufIdx];
            vertexDesc->layouts()->object(bufIdx)->setStride((NS::UInteger)layout.arrayStride);
            vertexDesc->layouts()->object(bufIdx)->setStepFunction(
                layout.stepMode == StepMode::instance
                    ? MTL::VertexStepFunctionPerInstance
                    : MTL::VertexStepFunctionPerVertex);
            for (const auto &attr : layout.attributes) {
                auto *attrDesc = vertexDesc->attributes()->object(attr.shaderLocation);
                attrDesc->setBufferIndex(bufIdx);
                attrDesc->setOffset(attr.offset);
                attrDesc->setFormat(toMTLVertexFormat(attr.componentType, attr.accessorType));
            }
        }
        pipelineDesc->setVertexDescriptor(vertexDesc);
        vertexDesc->release();
    }

    NS::Error *error       = nullptr;
    auto *pipelineState    = dev->newRenderPipelineState(pipelineDesc, &error);
    pipelineDesc->release();
    if (!pipelineState) {
        if (error) std::cerr << "createRenderPipeline: "
                             << error->localizedDescription()->utf8String() << std::endl;
        return nullptr;
    }
    return std::shared_ptr<RenderPipeline>(new RenderPipeline(pipelineState));
}

std::shared_ptr<ComputePipeline> Device::createComputePipeline(const ComputePipelineDescriptor &descriptor) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    if (!descriptor.compute.module) return nullptr;

    auto *library  = static_cast<MTL::Library *>(descriptor.compute.module->native);
    auto *funcName = NS::String::string(descriptor.compute.entryPoint.c_str(), NS::UTF8StringEncoding);
    auto *function = library->newFunction(funcName);
    if (!function) return nullptr;

    NS::Error *error      = nullptr;
    auto *pipelineState   = dev->newComputePipelineState(function, &error);
    function->release();
    if (!pipelineState) {
        if (error) std::cerr << "createComputePipeline: "
                             << error->localizedDescription()->utf8String() << std::endl;
        return nullptr;
    }
    return std::shared_ptr<ComputePipeline>(new ComputePipeline(pipelineState));
}

std::shared_ptr<BindGroupLayout> Device::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) {
    // Metal uses implicit binding; no separate layout object is required.
    return std::shared_ptr<BindGroupLayout>(new BindGroupLayout(nullptr));
}

std::shared_ptr<BindGroup> Device::createBindGroup(const BindGroupDescriptor &descriptor) {
    // Metal binds resources directly on the encoder; no separate bind group object needed.
    return std::shared_ptr<BindGroup>(new BindGroup(nullptr));
}

std::shared_ptr<PipelineLayout> Device::createPipelineLayout(const PipelineLayoutDescriptor &descriptor) {
    // Metal uses implicit pipeline layout; no separate object is required.
    return std::shared_ptr<PipelineLayout>(new PipelineLayout(nullptr));
}

std::shared_ptr<Sampler> Device::createSampler(const SamplerDescriptor &descriptor) {
    auto *dev  = static_cast<MetalDeviceData *>(native)->device;
    auto *desc = MTL::SamplerDescriptor::alloc()->init();

    desc->setSAddressMode(toMTLAddressMode(descriptor.addressModeU));
    desc->setTAddressMode(toMTLAddressMode(descriptor.addressModeV));
    desc->setRAddressMode(toMTLAddressMode(descriptor.addressModeW));
    desc->setMinFilter(toMTLFilter(descriptor.minFilter));
    desc->setMagFilter(toMTLFilter(descriptor.magFilter));
    desc->setMipFilter(toMTLMipFilter(descriptor.minFilter));
    desc->setLodMinClamp((float)descriptor.lodMinClamp);
    desc->setLodMaxClamp((float)descriptor.lodMaxClamp);
    desc->setMaxAnisotropy((NS::UInteger)std::max(1.0, descriptor.maxAnisotropy));
    if (descriptor.compare) {
        desc->setCompareFunction(toMTLCompare(*descriptor.compare));
    }

    auto *samplerState = dev->newSamplerState(desc);
    desc->release();
    if (!samplerState) return nullptr;
    return std::shared_ptr<Sampler>(new Sampler(samplerState));
}

std::shared_ptr<QuerySet> Device::createQuerySet(const QuerySetDescriptor &descriptor) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    // Use a shared buffer to store occlusion/timestamp query results (8 bytes per slot).
    auto *buffer = dev->newBuffer(
        descriptor.count * sizeof(uint64_t), MTL::ResourceStorageModeShared);
    if (!buffer) return nullptr;
    return std::shared_ptr<QuerySet>(new QuerySet(buffer));
}

std::shared_ptr<CommandEncoder> Device::createCommandEncoder() {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    auto *cmdBuffer  = deviceData->commandQueue->commandBuffer();
    if (!cmdBuffer) return nullptr;
    cmdBuffer->retain();
    return std::shared_ptr<CommandEncoder>(new CommandEncoder(cmdBuffer));
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
    static_cast<MTL::CommandBuffer *>(commandBuffer->native)->commit();
}

std::shared_ptr<TextureView> Device::getSwapchainTextureView() {
    // On Metal the swapchain is managed by MTKView. Use TextureView::fromNative()
    // with the CAMetalDrawable's texture instead.
    return nullptr;
}

std::string getVersion() {
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." +
           std::to_string(campello_gpu_VERSION_MINOR) + "." +
           std::to_string(campello_gpu_VERSION_PATCH);
}
