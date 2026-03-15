#pragma once

#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Stencil test configuration for one face (front or back) of a primitive.
     *
     * Defines the comparison function and the operations applied to the stencil
     * buffer depending on whether the stencil and depth tests pass or fail.
     */
    struct StencilDescriptor {
        /**
         * @brief Comparison function used for the stencil test.
         *
         * The current stencil value is compared with the stencil reference value using
         * this function. Defaults to `CompareOp::always` (always passes).
         */
        CompareOp compare;

        /**
         * @brief Stencil operation when the stencil test passes but the depth test fails.
         *
         * Defaults to `StencilOp::keep`.
         */
        StencilOp depthFailOp;

        /**
         * @brief Stencil operation when the stencil test fails.
         *
         * Defaults to `StencilOp::keep`.
         */
        StencilOp failOp;

        /**
         * @brief Stencil operation when both the stencil and depth tests pass.
         *
         * Defaults to `StencilOp::keep`.
         */
        StencilOp passOp;
    };

    /**
     * @brief Depth/stencil state for a `RenderPipeline`.
     *
     * Configures depth testing, depth writes, polygon depth bias, stencil testing,
     * and stencil write masks. Set as `RenderPipelineDescriptor::depthStencil`.
     *
     * @code
     * DepthStencilDescriptor ds{};
     * ds.format           = PixelFormat::depth32float;
     * ds.depthWriteEnabled = true;
     * ds.depthCompare     = CompareOp::less;
     * @endcode
     */
    struct DepthStencilDescriptor {
        /**
         * @brief Constant bias added to the depth of every fragment.
         *
         * Useful for shadow map rendering to avoid self-shadowing. Defaults to 0.
         */
        double depthBias;

        /**
         * @brief Maximum absolute value of the depth bias applied to a fragment.
         *
         * Clamps the total bias to prevent excessive depth offsets. Defaults to 0
         * (no clamp).
         */
        double depthBiasClamp;

        /**
         * @brief Depth bias that scales with the slope of the polygon surface.
         *
         * Helps reduce shadow acne on sloped surfaces. Defaults to 0.
         */
        double depthBiasSlopeScale;

        /**
         * @brief Comparison function used for depth testing.
         *
         * A fragment passes the depth test when this comparison between the
         * fragment depth and the stored depth value returns `true`.
         */
        CompareOp depthCompare;

        /**
         * @brief Whether this pipeline may write new depth values to the attachment.
         *
         * Set to `true` for opaque geometry passes. Set to `false` for transparent
         * or read-only depth passes.
         */
        bool depthWriteEnabled;

        /**
         * @brief Pixel format of the depth/stencil attachment this pipeline targets.
         *
         * Must match the format of the `TextureView` supplied as
         * `DepthStencilAttachment::view` in the render pass descriptor.
         */
        PixelFormat format;

        /**
         * @brief Stencil configuration for back-facing primitives.
         *
         * Leave as `std::nullopt` to disable stencil testing for back faces.
         */
        std::optional<StencilDescriptor> stencilBack;

        /**
         * @brief Stencil configuration for front-facing primitives.
         *
         * Leave as `std::nullopt` to disable stencil testing for front faces.
         */
        std::optional<StencilDescriptor> stencilFront;

        /**
         * @brief Bitmask of stencil bits that are read during stencil comparison tests.
         *
         * Defaults to `0xFFFFFFFF` (all bits readable).
         */
        uint32_t stencilReadMask;

        /**
         * @brief Bitmask of stencil bits that may be written by stencil operations.
         *
         * Defaults to `0xFFFFFFFF` (all bits writable).
         */
        uint32_t stencilWriteMask;
    };

}