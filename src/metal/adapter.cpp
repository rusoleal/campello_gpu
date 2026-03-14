#include "Metal.hpp"
#include <campello_gpu/adapter.hpp>

using namespace systems::leal::campello_gpu;

Adapter::Adapter() : native(nullptr) {}

std::set<Feature> Adapter::getFeatures() {
    std::set<Feature> toReturn;
    if (native == nullptr) return toReturn;

    auto *dev = static_cast<MTL::Device *>(native);

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
