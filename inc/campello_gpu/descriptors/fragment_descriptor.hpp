#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    /** @brief Source/destination factors for a blend equation. Values match MTL::BlendFactor. */
    enum class BlendFactor : unsigned long {
        zero                  = 0,
        one                   = 1,
        srcColor              = 2,
        oneMinusSrcColor      = 3,
        srcAlpha              = 4,
        oneMinusSrcAlpha      = 5,
        dstColor              = 6,
        oneMinusDstColor      = 7,
        dstAlpha              = 8,
        oneMinusDstAlpha      = 9,
    };

    /** @brief Blend equation operator. Values match MTL::BlendOperation. */
    enum class BlendOperation : unsigned long {
        add             = 0,
        subtract        = 1,
        reverseSubtract = 2,
        min             = 3,
        max             = 4,
    };

    /** @brief One channel's contribution in a blend equation (src factor, dst factor, op). */
    struct BlendComponent {
        BlendFactor    srcFactor  = BlendFactor::one;
        BlendFactor    dstFactor  = BlendFactor::zero;
        BlendOperation operation  = BlendOperation::add;
    };

    /**
     * @brief Alpha-blending configuration for one color attachment.
     *
     * Set `ColorState::blend` to enable blending. Leave it as `std::nullopt` to
     * keep blending disabled (opaque pass-through).
     *
     * Standard premultiplied-alpha "over" blend:
     * @code
     * BlendState b;
     * b.color = { BlendFactor::one, BlendFactor::oneMinusSrcAlpha, BlendOperation::add };
     * b.alpha = { BlendFactor::one, BlendFactor::oneMinusSrcAlpha, BlendOperation::add };
     * @endcode
     */
    struct BlendState {
        BlendComponent color;
        BlendComponent alpha;
    };

    /**
     * @brief Bitmask flags controlling which RGBA channels a color target writes.
     *
     * Combine flags with bitwise OR to selectively enable channels.
     * Defaults to `all` when not explicitly specified.
     */
    enum class ColorWrite {
        red   = 0x01, ///< Enable writes to the red channel.
        green = 0x02, ///< Enable writes to the green channel.
        blue  = 0x04, ///< Enable writes to the blue channel.
        alpha = 0x08, ///< Enable writes to the alpha channel.
        all   = 0x0f  ///< Enable writes to all four channels (red | green | blue | alpha).
    };

    /**
     * @brief Configuration for a single color attachment output of the fragment stage.
     *
     * Each element in `FragmentDescriptor::targets` corresponds to one color
     * attachment slot in the render pass.
     */
    struct ColorState {
        /**
         * @brief Required pixel format of the color attachment this target writes into.
         *
         * Must match the format of the `TextureView` bound to the corresponding
         * color attachment slot in `BeginRenderPassDescriptor::colorAttachments`.
         */
        PixelFormat format;

        /**
         * @brief Bitmask of channels the pipeline is allowed to write.
         *
         * Combine `ColorWrite` flags (e.g. `ColorWrite::red | ColorWrite::green`) to
         * restrict writes. Defaults to `ColorWrite::all`.
         */
        ColorWrite writeMask;

        /**
         * @brief Optional alpha-blending configuration.
         *
         * When set, blending is enabled for this color attachment using the provided
         * `BlendState`. When `std::nullopt` (default), blending is disabled and the
         * fragment output overwrites the destination directly.
         */
        std::optional<BlendState> blend;
    };

    /**
     * @brief Configuration for the fragment shader stage of a render pipeline.
     *
     * Specifies the shader entry point and the color output targets produced by the
     * fragment shader. Omit `FragmentDescriptor` from `RenderPipelineDescriptor::fragment`
     * to create a depth-only pipeline.
     *
     * @code
     * FragmentDescriptor fd{};
     * fd.module     = fragShader;
     * fd.entryPoint = "fragmentMain";
     * fd.targets    = { ColorState{ PixelFormat::bgra8unorm, ColorWrite::all } };
     * @endcode
     */
    struct FragmentDescriptor {
        /**
         * @brief Shader module containing the fragment entry point.
         */
        std::shared_ptr<ShaderModule> module;

        /**
         * @brief Name of the fragment function within `module`.
         */
        std::string entryPoint;

        /**
         * @brief Color output targets, one per fragment shader output location.
         *
         * The number of entries must match the number of color attachments in the
         * `BeginRenderPassDescriptor` used with this pipeline.
         */
        std::vector<ColorState> targets;
    };

}
