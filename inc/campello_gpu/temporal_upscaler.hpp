#pragma once

#include <cstdint>
#include <memory>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>

namespace systems::leal::campello_gpu {

    class Texture;

    /**
     * @brief Configuration of a `TemporalUpscaler`.
     *
     * The input (the frame, rendered at the input size with a sub-pixel
     * jitter) and output sizes, and the formats of the textures it's given.
     */
    struct TemporalUpscalerDescriptor {
        uint32_t    inputWidth = 0;
        uint32_t    inputHeight = 0;
        uint32_t    outputWidth = 0;
        uint32_t    outputHeight = 0;
        PixelFormat colorFormat = PixelFormat::rgba16float;   ///< The frame's.
        PixelFormat depthFormat = PixelFormat::depth32float;  ///< Its depth buffer's.
        PixelFormat motionFormat = PixelFormat::rg16float;    ///< Its motion vectors'.
        PixelFormat outputFormat = PixelFormat::rgba16float;  ///< The output's.
        /// Depth 1 near, 0 far (reversed-Z). Default: 0 near, 1 far.
        bool        depthReversed = false;
    };

    /**
     * @brief The inputs of one `CommandEncoder::encodeTemporalUpscale()`.
     *
     * Motion vectors point from a pixel's current position to its previous
     * one; `motionScale` converts the texture's values to input pixels (e.g.
     * the input size for vectors in uv units). `jitter` is the sub-pixel
     * offset the frame's projection was jittered by, in input pixels.
     */
    struct TemporalUpscaleInputs {
        std::shared_ptr<Texture> color;   ///< The frame, input size.
        std::shared_ptr<Texture> depth;   ///< Its depth, input size.
        std::shared_ptr<Texture> motion;  ///< Its motion vectors, input size.
        std::shared_ptr<Texture> output;  ///< Output size, with `TemporalUpscaler::outputUsage()`.
        float jitterX = 0.0f;
        float jitterY = 0.0f;
        float motionScaleX = 1.0f;
        float motionScaleY = 1.0f;
        /// Discard the history (a camera cut, the first frame).
        bool  reset = false;
    };

    /**
     * @brief A temporal upscaler: reconstructs an output-size image from
     * jittered input-size frames, their depth and motion vectors, keeping
     * its history between frames.
     *
     * Metal: MetalFX's temporal scaler (macOS 13 / iOS 16 and later, where
     * `Device::supportsTemporalUpscaler()`). Other backends: not yet
     * (`Device::createTemporalUpscaler()` returns null).
     */
    class TemporalUpscaler {
    private:
        friend class Device;
        friend class CommandEncoder;
        void *native;
        TemporalUpscalerDescriptor descriptor_;
        TemporalUpscaler(void *pd, const TemporalUpscalerDescriptor &descriptor);

    public:
        ~TemporalUpscaler();

        [[nodiscard]] const TemporalUpscalerDescriptor &getDescriptor() const { return descriptor_; }

        /**
         * @brief The usage the output texture must have been created with
         * (on Metal: render target and/or storage binding).
         */
        [[nodiscard]] TextureUsage outputUsage() const;
    };

}
