#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief Optional GPU features that may or may not be available on a given adapter.
     *
     * Query support via `Adapter` before enabling features in device creation.
     * Attempting to use a feature that is not supported produces undefined behaviour.
     */
    enum class Feature
    {
        raytracing,             ///< Hardware-accelerated ray tracing (DXR / Metal RT / Vulkan KHR RT).
        msaa32bit,              ///< 32-bit MSAA resolve support.
        bcTextureCompression,   ///< BC1–BC7 block-compressed texture formats.
        etc2TextureCompression, ///< ETC2/EAC block-compressed texture formats.
        astcTextureCompression, ///< ASTC block-compressed texture formats.
        depth24Stencil8PixelFormat, ///< Combined 24-bit depth + 8-bit stencil pixel format.
        geometryShader,         ///< Programmable geometry shader stage (not available on Metal).
        cooperativeMatrix,      ///< Hardware-accelerated small-tile matrix multiply-accumulate
                                ///< (Vulkan `VK_KHR_cooperative_matrix`, Metal `simdgroup_matrix`,
                                ///< DirectX SM6.8 Wave Matrix). Support implies only that *some*
                                ///< tile shape/type combination is available, not every one —
                                ///< query the specific shapes you need before compiling a kernel.
        fp16,                   ///< 16-bit floating point (`half`) arithmetic in shader code
                                ///< (Vulkan `shaderFloat16`, Metal native `half`, DirectX
                                ///< `Native16BitShaderOpsSupported`, WebGPU `shader-f16`).
        subgroupOperations      ///< Cross-invocation subgroup/SIMD-group/wave operations —
                                ///< ballot and arithmetic reductions (sum/min/max/etc.) across
                                ///< a subgroup (Vulkan subgroup basic+ballot+arithmetic,
                                ///< Metal SIMD-group functions, DirectX `WaveOps`, WebGPU
                                ///< `subgroups`). Requires compute-stage support at minimum.
    };

}
