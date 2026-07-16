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
    if (dev->supportsFamily(MTL::GPUFamilyApple6))
        toReturn.insert(Feature::cooperativeMatrix);
    if (__builtin_available(macOS 10.11, *)) {
        if (dev->depth24Stencil8PixelFormatSupported())
            toReturn.insert(Feature::depth24Stencil8PixelFormat);
    }

    // `half` is a native MSL scalar type on every Metal-capable device (has been
    // since MSL 1.0) — there is no MTLDevice capability query for it because
    // there's nothing to gate.
    toReturn.insert(Feature::fp16);

    // Full SIMD-group functions (simd_shuffle/simd_ballot/simd_sum etc., as
    // opposed to the more limited quad-group functions available on older
    // hardware) require GPUFamilyApple6+ — same bar as cooperativeMatrix above,
    // and for the same reason (both landed with the same GPU generation).
    if (dev->supportsFamily(MTL::GPUFamilyApple6))
        toReturn.insert(Feature::subgroupOperations);

    return toReturn;
}
