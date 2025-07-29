#pragma once

namespace systems::leal::campello_gpu {

    /**
     * An enumerated value that defines which polygons are considered front-facing.
     */
    enum class FrontFace {

        /**
         * Polygons with vertices whose framebuffer coordinates are given in counter-clockwise
         * order.
         */
        ccw,

        /**
         * Polygons with vertices whose framebuffer coordinates are given in clockwise order.
         */
        cw
    };

}