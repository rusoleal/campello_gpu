// TemporalUpscaler where the backend has none yet (Vulkan, DirectX,
// WebGPU): the device reports no support and creates none; encoding one
// does nothing. Metal's is src/metal/temporal_upscaler.mm (MetalFX).

#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/temporal_upscaler.hpp>

using namespace systems::leal::campello_gpu;

TemporalUpscaler::TemporalUpscaler(void *pd, const TemporalUpscalerDescriptor &descriptor)
    : native(pd), descriptor_(descriptor) {}

TemporalUpscaler::~TemporalUpscaler() = default;

TextureUsage TemporalUpscaler::outputUsage() const {
    return static_cast<TextureUsage>(0);
}

float Device::getTemporalUpscalerMaxScale() { return 0.0f; }

bool Device::supportsTemporalUpscaler() {
    return false;
}

std::shared_ptr<TemporalUpscaler> Device::createTemporalUpscaler(const TemporalUpscalerDescriptor &) {
    return nullptr;
}

void CommandEncoder::encodeTemporalUpscale(std::shared_ptr<TemporalUpscaler>, const TemporalUpscaleInputs &) {}
