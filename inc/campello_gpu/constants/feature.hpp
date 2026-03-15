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
        depth24Stencil8PixelFormat, ///< Combined 24-bit depth + 8-bit stencil pixel format.
        geometryShader          ///< Programmable geometry shader stage (not available on Metal).
    };

}
