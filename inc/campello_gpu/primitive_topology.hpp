#pragma once

namespace systems::leal::campello_gpu {

    /**
     * An enumerated value that defines the type of primitive to be constructed from the
     * specified vertex inputs.
     */
    enum class PrimitiveTopology {

        /**
         * Each consecutive pair of two vertices defines a line primitive.
         */
        lineList,

        /**
         * Each vertex after the first defines a line primitive between it and the previous
         * vertex.
         */
        lineStrip,

        /**
         * Each vertex defines a point primitive.
         */
        pointList,

        /**
         * Each consecutive triplet of three vertices defines a triangle primitive.
         */
        triangleList,

        /**
         * Each vertex after the first two defines a triangle primitive between it and the
         * previous two vertices.
         */
        triangleStrip
    };

}