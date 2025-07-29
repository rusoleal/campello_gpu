#pragma once

namespace systems::leal::campello_gpu {

    enum class WrapMode {

        /**
         * The texture coordinates are clamped between 0.0 and 1.0, inclusive.
         */
        clampToEdge = 33071,

        /**
         * The texture coordinates wrap to the other side of the texture.
         */
        repeat = 10497,

        /**
         * The texture coordinates wrap to the other side of the texture, but the texture
         * is flipped when the integer part of the coordinate is odd.
         */
        mirrorRepeat = 33648
    };

}