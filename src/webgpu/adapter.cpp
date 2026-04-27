#include <set>
#include <campello_gpu/adapter.hpp>
#include "common.hpp"

using namespace systems::leal::campello_gpu;

Adapter::Adapter() {
    native = nullptr;
}

std::set<Feature> Adapter::getFeatures() {
    std::set<Feature> features;
    // WebGPU via Emscripten does not expose ray tracing.
    features.insert(Feature::msaa32bit);
    features.insert(Feature::depth24Stencil8PixelFormat);
    return features;
}
