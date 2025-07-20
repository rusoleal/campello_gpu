#pragma once

#include <campello_gpu/pixel_format.hpp>

namespace systems::leal::campello_gpu {

    /**
     * An enumerated value specifying the comparison operation used to test fragment
     * depths against depthStencilAttachment depth values.
     */
    enum class CompareOp {

        /**
         * Comparison tests never pass.
         */
        never,

        /**
         * A provided value passes the comparison test if it is less than the sampled
         * value.
         */
        less,

        /**
         * A provided value passes the comparison test if it is equal to the sampled
         * value.
         */
        equal,

        /**
         * A provided value passes the comparison test if it is less than or equal to
         * the sampled value.
         */
        lessEqual,

        /**
         * A provided value passes the comparison test if it is greater than the sampled
         * value.
         */
        greater,

        /**
         * A provided value passes the comparison test if it is not equal to the sampled
         * value.
         */
        notEqual,

        /**
         * A provided value passes the comparison test if it is greater than or equal to
         * the sampled value.
         */
        greaterEqual,

        /**
         * Comparison tests always pass.
         */
        always
    };

    /**
     * An enumerated value specifying the stencil operation performed if the fragment depth
     * comparison described by depthCompare fails.
     */
    enum class StencilOp {

        /**
         * Decrement the current render state stencil value, clamping it to 0.
         */
        decrementClamp,

        /**
         * Decrement the current render state stencil value, wrapping it to the maximum
         * representable value of the depthStencilAttachment's stencil aspect if the value
         * goes below 0.
         */
        decrementWrap,

        /**
         * Bitwise-invert the current render state stencil value.
         */
        invert,

        /**
         * Increments the current render state stencil value, clamping it to the maximum
         * representable value of the depthStencilAttachment's stencil aspect.
         */
        incrementClamp,

        /**
         * Increments the current render state stencil value, wrapping it to zero if the
         * value exceeds the maximum representable value of the depthStencilAttachment's
         * stencil aspect.
         */
        incrementWrap,

        /**
         * Keep the current stencil value.
         */
        keep,

        /**
         * Set the stencil value to the current render state stencil value.
         */
        replace,

        /**
         * Set the stencil value to 0.
         */
        zero
    };

    struct StencilDescriptor {

        /**
         * An enumerated value specifying the comparison operation used when testing fragments
         * against depthStencilAttachment stencil values.
         * If omitted, compare defaults to "always".
         */
        CompareOp compare;

        /**
         * An enumerated value specifying the stencil operation performed if the fragment depth
         * comparison described by depthCompare fails.
         * If omitted, depthFailOp defaults to "keep".
         */
        StencilOp depthFailOp;

        /**
         * An enumerated value specifying the stencil operation performed if the fragment stencil
         * comparison test described by compare fails.
         * If omitted, failOp defaults to "keep".
         */
        StencilOp failOp;

        /**
         * An enumerated value specifying the stencil operation performed if the fragment stencil
         * comparison test described by compare passes.
         * If omitted, passOp defaults to "keep".
         */
        StencilOp passOp;
    };

    /**
     * An object describing depth-stencil properties including testing, operations, 
     * and bias.
     */
    struct DepthStencilDescriptor {

        /**
         * A number representing a constant depth bias that is added to each fragment.
         * If omitted, depthBias defaults to 0.
         */
        double depthBias;

        /**
         * A number representing the maximum depth bias of a fragment.
         * If omitted, depthBiasClamp defaults to 0.
         */
        double depthBiasClamp;

        /**
         * A number representing a depth bias that scales with the fragment's slope.
         * If omitted, depthBiasSlopeScale defaults to 0.
         */
        double depthBiasSlopeScale;

        /**
         * Comparison operation used to test fragment depths against depthStencilAttachment
         * depth values.
         */
        CompareOp depthCompare;

        /**
         * A boolean. A value of true specifies that the GPURenderPipeline can modify
         * depthStencilAttachment depth values after creation. Setting it to false means
         * it cannot.
         */
        bool depthWriteEnabled;

        /**
         * An enumerated value specifying the depthStencilAttachment format that the
         * GPURenderPipeline will be compatible with.
         */
        PixelFormat format;

        /**
         * An object that defines how stencil comparisons and operations are performed
         * for back-facing primitives.
         */
        std::optional<StencilDescriptor> stencilBack;

        /**
         * An object that defines how stencil comparisons and operations are performed
         * for front-facing primitives.
         */
        std::optional<StencilDescriptor> stencilFront;

        /**
         * A bitmask controlling which depthStencilAttachment stencil value bits are read
         * when performing stencil comparison tests.
         * If omitted, stencilReadMask defaults to 0xFFFFFFFF.
         */
        uint32_t stencilReadMask;

        /**
         * A bitmask controlling which depthStencilAttachment stencil value bits are written
         * to when performing stencil operations.
         * If omitted, stencilWriteMask defaults to 0xFFFFFFFF.
         */
        uint32_t stencilWriteMask;
    };

}