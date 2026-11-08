#include <set>
#include <campello_gpu/adapter.hpp>
#include "common.hpp"

using namespace systems::leal::campello_gpu;

std::vector<WGPUFeatureName> systems::leal::campello_gpu::desiredOptionalFeatures() {
    return {
        WGPUFeatureName_TextureCompressionBC,
        WGPUFeatureName_TextureCompressionETC2,
        WGPUFeatureName_TextureCompressionASTC,
        WGPUFeatureName_ShaderF16,
        WGPUFeatureName_TimestampQuery,
        WGPUFeatureName_Depth32FloatStencil8,
        WGPUFeatureName_Float32Filterable,
        WGPUFeatureName_RG11B10UfloatRenderable,
        WGPUFeatureName_BGRA8UnormStorage,
        WGPUFeatureName_IndirectFirstInstance,
        WGPUFeatureName_DepthClipControl,
        WGPUFeatureName_Subgroups,
    };
}

std::set<Feature> systems::leal::campello_gpu::featuresFromWGPU(
    const std::function<bool(WGPUFeatureName)>& has) {
    std::set<Feature> features;
    // Core WebGPU guarantees 4x MSAA on 32-bit color formats and the
    // depth24plus-stencil8 format.
    features.insert(Feature::msaa32bit);
    features.insert(Feature::depth24Stencil8PixelFormat);

    if (has(WGPUFeatureName_TextureCompressionBC)) features.insert(Feature::bcTextureCompression);
    if (has(WGPUFeatureName_TextureCompressionETC2)) features.insert(Feature::etc2TextureCompression);
    if (has(WGPUFeatureName_TextureCompressionASTC)) features.insert(Feature::astcTextureCompression);
    if (has(WGPUFeatureName_ShaderF16)) features.insert(Feature::fp16);
    if (has(WGPUFeatureName_Subgroups)) features.insert(Feature::subgroupOperations);

    // Never reported on WebGPU:
    //  - raytracing / geometryShader: no WebGPU equivalent.
    //  - cooperativeMatrix: WGSL's subgroup-matrix proposal is still in design
    //    discussion (checked 2026-07-16 against gpuweb meeting minutes and
    //    Chrome release notes; see TODO.md). Revisit once it reaches at least
    //    an experimental feature name.
    return features;
}

Adapter::Adapter() {
    native = nullptr;
}

Adapter::~Adapter() {
    auto* handle = static_cast<AdapterHandle*>(native);
    if (handle) {
        if (handle->adapter) wgpuAdapterRelease(handle->adapter);
        delete handle;
    }
}

std::set<Feature> Adapter::getFeatures() {
    auto* handle = static_cast<AdapterHandle*>(native);
    WGPUAdapter adapter = handle ? handle->adapter : nullptr;
    return featuresFromWGPU([adapter](WGPUFeatureName f) {
        return adapter && wgpuAdapterHasFeature(adapter, f);
    });
}
