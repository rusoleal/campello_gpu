#pragma once

namespace systems::leal::campello_gpu {

    enum class ColorSpace {

        undefined,
        srgbNonLinear,
        srgbExtendedLinear,
        srgbExtendedNonLinear,
        displayP3NonLinear,
        displayP3Linear,
        bt709Linear,
        bt709NonLinear,
        bt2020Linear,
        hdr10St2084,
        hdr10Hlg,
        dolbyVision,
        adobeRgbLinear,
        adobeRgbNonLinear,
    };

}