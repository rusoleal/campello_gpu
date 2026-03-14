#include "Metal.hpp"
#include "TargetConditionals.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>
#include <iostream>

using namespace systems::leal::campello_gpu;

std::ostream& operator<<(std::ostream &os, const std::set<Feature> &obj) {
    os << "[";
    for (auto feature: obj) {
        switch (feature) {
            case Feature::raytracing:
                os << "raytracing, ";
                break;
            case Feature::msaa32bit:
                os << "msaa32bit, ";
                break;
            case Feature::bcTextureCompression:
                os << "bcTextureCompression, ";
                break;
            case Feature::depth24Stencil8PixelFormat:
                os << "depth24Stencil8PixelFormat, ";
                break;
            default:
                os << "unknown, ";
                break;
        }
    }
    os << "]";
    return os;
}

Device::Device(void *pd) {
    native = pd;

    std::cout << "  - name: " << getName() << std::endl;
    std::cout << "  - supportsRayTracing: " << ((MTL::Device *)native)->supportsRaytracing() << std::endl;
    std::cout << "  - supportsPrimitiveMotionBlur: " << ((MTL::Device *)native)->supportsPrimitiveMotionBlur() << std::endl;
    std::cout << "  - supportsRaytracingFromRender: " << ((MTL::Device *)native)->supportsRaytracingFromRender() << std::endl;
    std::cout << "  - supports32BitMSAA: " << ((MTL::Device *)native)->supports32BitMSAA() << std::endl;
    std::cout << "  - supportsPullModelInterpolation: " << ((MTL::Device *)native)->supportsPullModelInterpolation() << std::endl;
    std::cout << "  - supportsShaderBarycentricCoordinates: " << ((MTL::Device *)native)->supportsShaderBarycentricCoordinates() << std::endl;
    std::cout << "  - programmableSamplePositionsSupported: " << ((MTL::Device *)native)->programmableSamplePositionsSupported() << std::endl;
    std::cout << "  - rasterOrderGroupsSupported: " << ((MTL::Device *)native)->rasterOrderGroupsSupported() << std::endl;
    std::cout << "  - supports32BitFloatFiltering: " << ((MTL::Device *)native)->supports32BitFloatFiltering() << std::endl;
    std::cout << "  - supportsBCTextureCompression: " << ((MTL::Device *)native)->supportsBCTextureCompression() << std::endl;
    if (__builtin_available(macOS 10.11, *)) {
        std::cout << "  - depth24Stencil8PixelFormatSupported: " << ((MTL::Device *)native)->depth24Stencil8PixelFormatSupported() << std::endl;
    }
    std::cout << "  - supportsQueryTextureLOD: " << ((MTL::Device *)native)->supportsQueryTextureLOD() << std::endl;
    std::cout << "  - readWriteTextureSupport: " << ((MTL::Device *)native)->readWriteTextureSupport() << std::endl;
    std::cout << "  - supportsFunctionPointers: " << ((MTL::Device *)native)->supportsFunctionPointers() << std::endl;
    std::cout << "  - supportsFunctionPointersFromRender: " << ((MTL::Device *)native)->supportsFunctionPointersFromRender() << std::endl;
    std::cout << "  - hasUnifiedMemory: " << ((MTL::Device *)native)->hasUnifiedMemory() << std::endl;
    std::cout << "  - currentAllocatedSize: " << ((MTL::Device *)native)->currentAllocatedSize()/(1024*1024.0) << "Mb." << std::endl;
    std::cout << "  - recommendedMaxWorkingSetSize: " << ((MTL::Device *)native)->recommendedMaxWorkingSetSize()/(1024*1024.0) << "Mb." << std::endl;
    std::cout << "  - features: " << getFeatures() << std::endl;
}

Device::~Device() {
    if (native != nullptr) {
        ((MTL::Device *)native)->release();
        std::cout << "Device::~Device()" << std::endl;
    }
}

std::shared_ptr<Device> Device::createDefaultDevice(void *pd) {
    auto device = MTL::CreateSystemDefaultDevice();
    if (device == nullptr) {
        return nullptr;
    }

    std::cout << "Device::createDefaultDevice()" << std::endl;

    Device *toReturn = new Device(device);
    return std::shared_ptr<Device>(toReturn);
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> adapter, void *pd) {
    if (adapter == nullptr || adapter->native == nullptr) {
        return nullptr;
    }

    auto mtlDevice = static_cast<MTL::Device *>(adapter->native);
    mtlDevice->retain();

    Device *toReturn = new Device(mtlDevice);
    return std::shared_ptr<Device>(toReturn);
}

std::vector<std::shared_ptr<Adapter>> Device::getAdapters() {
    std::vector<std::shared_ptr<Adapter>> toReturn;

    auto devices = MTL::CopyAllDevices();
    for (NS::UInteger i = 0; i < devices->count(); ++i) {
        auto mtlDevice = static_cast<MTL::Device *>(devices->object(i));
        Adapter *adapter = new Adapter();
        adapter->native = mtlDevice;
        toReturn.push_back(std::shared_ptr<Adapter>(adapter));
    }
    devices->release();

    return toReturn;
}

std::shared_ptr<Texture> Device::createTexture(
    TextureType type,
    PixelFormat pixelFormat,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t mipLevels,
    uint32_t samples,
    TextureUsage usageMode) {

    MTL::TextureDescriptor *pTextureDesc = MTL::TextureDescriptor::alloc()->init();
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

    MTL::Texture *pTexture = ((MTL::Device *)native)->newTexture(pTextureDesc);
    pTextureDesc->release();

    if (pTexture == nullptr) {
        return nullptr;
    }

    Texture *texture = new Texture(pTexture);
    return std::shared_ptr<Texture>(texture);
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {
    MTL::ResourceOptions options;

    const int u = static_cast<int>(usage);
    if (u & (static_cast<int>(BufferUsage::mapRead) | static_cast<int>(BufferUsage::mapWrite))) {
        // CPU needs direct read/write — use Shared storage
        options = MTL::ResourceStorageModeShared;
    } else {
        // Default: Managed — CPU writes, GPU reads (typical for vertex/uniform/index)
        options = MTL::ResourceStorageModeManaged;
    }

    MTL::Buffer *pBuffer = ((MTL::Device *)native)->newBuffer(size, options);
    if (pBuffer == nullptr) {
        return nullptr;
    }

    Buffer *buffer = new Buffer(pBuffer);
    return std::shared_ptr<Buffer>(buffer);
}

std::string Device::getName() {
    return ((MTL::Device *)native)->name()->utf8String();
}

std::set<Feature> Device::getFeatures() {
    std::set<Feature> toReturn;

    if (((MTL::Device *)native)->supportsRaytracing()) {
        toReturn.insert(Feature::raytracing);
    }
    if (((MTL::Device *)native)->supports32BitMSAA()) {
        toReturn.insert(Feature::msaa32bit);
    }
    if (((MTL::Device *)native)->supportsBCTextureCompression()) {
        toReturn.insert(Feature::bcTextureCompression);
    }
    if (__builtin_available(macOS 10.11, *)) {
        if (((MTL::Device *)native)->depth24Stencil8PixelFormatSupported()) {
            toReturn.insert(Feature::depth24Stencil8PixelFormat);
        }
    }

    return toReturn;
}

std::string Device::getEngineVersion() {
    return "unknown";
}
