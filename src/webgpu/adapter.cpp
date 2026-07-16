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
    // Feature::cooperativeMatrix intentionally omitted: verified this session
    // (2026-07-16, via the gpuweb wiki meeting minutes and Chrome's own release
    // notes, not from memory) that WGSL's cooperative/subgroup-matrix proposal is
    // still in design discussion — unshipped, and not even behind an experimental
    // Chromium flag yet. There is genuinely no WebGPU-exposed equivalent to query
    // or enable today. Revisit once the proposal reaches at least an experimental
    // flag (e.g. a future "chromium-experimental-subgroup-matrix"-style feature).
    return features;
}
