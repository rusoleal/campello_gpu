#pragma once

namespace systems::leal::campello_gpu {

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
}