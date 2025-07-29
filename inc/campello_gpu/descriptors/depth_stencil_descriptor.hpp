#pragma once

#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>

namespace systems::leal::campello_gpu {

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