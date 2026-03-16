#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/constants/feature.hpp>

using namespace systems::leal::campello_gpu;

Adapter::Adapter() : native(nullptr) {}

std::set<Feature> Adapter::getFeatures() {
    std::set<Feature> features;
    if (!native) return features;

    auto* adapter = static_cast<IDXGIAdapter1*>(native);

    // Create a temporary device to query features
    ID3D12Device* tmpDevice = nullptr;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                  IID_PPV_ARGS(&tmpDevice)))) {
        return features;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    if (SUCCEEDED(tmpDevice->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5)))) {
        if (opts5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            features.insert(Feature::raytracing);
    }

    D3D12_FEATURE_DATA_FORMAT_SUPPORT fmt = {};
    fmt.Format = DXGI_FORMAT_BC7_UNORM;
    tmpDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fmt, sizeof(fmt));
    if (fmt.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)
        features.insert(Feature::bcTextureCompression);

    tmpDevice->Release();
    return features;
}
