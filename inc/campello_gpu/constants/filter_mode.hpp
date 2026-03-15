#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Texture magnification and minification filter modes.
     *
     * Numeric values match the OpenGL/WebGL filter mode constants for
     * interoperability with glTF sampler data.
     */
    enum class FilterMode
    {
        fmUnknown              = 0,    ///< Unset / unrecognised filter mode.
        fmNearest              = 9728, ///< Nearest-neighbour (point) filtering — no interpolation.
        fmLinear               = 9729, ///< Bilinear filtering between the four nearest texels.
        fmNearestMipmapNearest = 9984, ///< Point filter within the nearest mip level.
        fmLinearMipmapNearest  = 9985, ///< Bilinear filter within the nearest mip level.
        fmNearestMipmapLinear  = 9986, ///< Point filter with linear blending between mip levels.
        fmLinearMipmapLinear   = 9987, ///< Trilinear filtering — bilinear within + blend between mip levels.
    };

}
