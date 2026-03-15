#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Color space / transfer function of a swapchain surface or texture.
     *
     * Used when configuring the presentation surface to control how GPU output
     * values map to display luminance and chromaticity.
     */
    enum class ColorSpace {
        undefined,              ///< No color space specified / unrecognised value.
        srgbNonLinear,          ///< Standard sRGB with gamma-encoded transfer function (most common for SDR).
        srgbExtendedLinear,     ///< Extended sRGB with a linear transfer function (scRGB, HDR on Windows).
        srgbExtendedNonLinear,  ///< Extended sRGB with a non-linear transfer function.
        displayP3NonLinear,     ///< Display P3 primaries with a non-linear (P3-D65) transfer function.
        displayP3Linear,        ///< Display P3 primaries with a linear transfer function.
        bt709Linear,            ///< BT.709 primaries with a linear transfer function.
        bt709NonLinear,         ///< BT.709 primaries with the BT.709 non-linear transfer function.
        bt2020Linear,           ///< BT.2020 / Rec.2020 primaries with a linear transfer function (wide gamut HDR).
        hdr10St2084,            ///< BT.2020 primaries + ST.2084 (PQ) transfer function (HDR10).
        hdr10Hlg,               ///< BT.2020 primaries + Hybrid Log-Gamma transfer function (HDR10 HLG).
        dolbyVision,            ///< Dolby Vision proprietary HDR format.
        adobeRgbLinear,         ///< Adobe RGB (1998) primaries with a linear transfer function.
        adobeRgbNonLinear,      ///< Adobe RGB (1998) primaries with the Adobe RGB gamma transfer function.
    };

}
