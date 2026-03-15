#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief A compiled shader program loaded onto the GPU.
     *
     * A `ShaderModule` wraps the platform-native compiled shader binary:
     * - **Metal** — a `.metallib` compiled from Metal Shading Language source.
     * - **Vulkan** — a SPIR-V binary.
     * - **DirectX** — an HLSL bytecode blob (not yet implemented).
     *
     * Shader modules are created by `Device::createShaderModule()` from a raw byte
     * buffer and are referenced in `VertexDescriptor` and `FragmentDescriptor` when
     * building render/compute pipelines.
     *
     * @code
     * // Metal: load default.metallib from the app bundle
     * NSData *lib = [NSData dataWithContentsOfFile:
     *     [[NSBundle mainBundle] pathForResource:@"default" ofType:@"metallib"]];
     * auto shaderModule = device->createShaderModule(
     *     (const uint8_t *)lib.bytes, (uint64_t)lib.length);
     * @endcode
     */
    class ShaderModule
    {
    private:
        friend class Device;
        void *native;
        ShaderModule(void *pd);

    public:
        ~ShaderModule();
    };

}
