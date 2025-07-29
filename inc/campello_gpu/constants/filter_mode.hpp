#pragma once

namespace systems::leal::campello_gpu {

    /**
     * Magnification/minification filter modes.
     */
    enum class FilterMode
    {
        fmUnknown = 0,
        fmNearest = 9728,
        fmLinear = 9729,
        fmNearestMipmapNearest = 9984,
        fmLinearMipmapNearest = 9985,
        fmNearestMipmapLinear = 9986,
        fmLinearMipmapLinear = 9987,
    };    

}