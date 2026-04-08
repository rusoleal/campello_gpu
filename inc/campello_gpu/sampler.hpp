#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief An immutable sampler state object.
     *
     * A `Sampler` encodes how the GPU samples a texture — filtering modes (nearest /
     * linear / anisotropic), wrap modes (clamp / repeat / mirror), LOD clamp range,
     * and an optional depth-comparison function for shadow maps.
     *
     * Samplers are created exclusively by `Device::createSampler()` and are immutable
     * after creation.
     *
     * @code
     * SamplerDescriptor desc{};
     * desc.addressModeU = WrapMode::repeat;
     * desc.addressModeV = WrapMode::repeat;
     * desc.addressModeW = WrapMode::clampToEdge;
     * desc.minFilter    = FilterMode::fmLinear;
     * desc.magFilter    = FilterMode::fmLinear;
     * desc.lodMinClamp  = 0.0;
     * desc.lodMaxClamp  = 32.0;
     * desc.maxAnisotropy = 1.0;
     * auto sampler = device->createSampler(desc);
     * @endcode
     */
    class Sampler
    {
    private:
        friend class Device;
        friend class RenderPassEncoder;
        friend class ComputePassEncoder;
        friend class RayTracingPassEncoder;
        void *native;

        Sampler(void *pd);

    public:
        ~Sampler();
    };

}
