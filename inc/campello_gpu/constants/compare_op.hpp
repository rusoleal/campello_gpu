#pragma once

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

}