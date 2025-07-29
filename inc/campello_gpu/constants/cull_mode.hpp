#pragma once

namespace systems::leal::campello_gpu {

    /**
     * An enumerated value that defines which polygon orientation will be culled, if any.
     */
    enum class CullMode {

        /**
         * Back-facing polygons are culled.
         */
        back,

        /**
         * Front-facing polygons are culled.
         */
        front,

        /**
         * No polygons are culled.
         */
        none
    };

}