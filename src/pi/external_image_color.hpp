#pragma once

// Shared by the backends of copyExternalImageToTexture(): the YCbCr to RGB
// conversion as one affine map, and the defaults for sources without color
// metadata.

#include <campello_gpu/external_image.hpp>

#include <cmath>

namespace systems::leal::campello_gpu {

    // Defaults when neither the caller nor the image says (see YCbCrMatrix).
    inline YCbCrMatrix resolveYCbCrMatrix(YCbCrMatrix requested, YCbCrMatrix fromImage, uint32_t height) {
        if (requested != YCbCrMatrix::fromSource) return requested;
        if (fromImage != YCbCrMatrix::fromSource) return fromImage;
        return height >= 720 ? YCbCrMatrix::bt709 : YCbCrMatrix::bt601;
    }

    inline YCbCrRange resolveYCbCrRange(YCbCrRange requested, YCbCrRange fromImage) {
        if (requested != YCbCrRange::fromSource) return requested;
        if (fromImage != YCbCrRange::fromSource) return fromImage;
        return YCbCrRange::limited;
    }

    // rows[i] · (Y, Cb, Cr, 1) = R, G, B (i = 0, 1, 2), for normalized
    // 8-bit samples in [0, 1]. matrix and range must be resolved.
    inline void yCbCrToRgbRows(YCbCrMatrix matrix, YCbCrRange range, float rows[3][4]) {
        const double kr = matrix == YCbCrMatrix::bt709 ? 0.2126 : 0.299;
        const double kb = matrix == YCbCrMatrix::bt709 ? 0.0722 : 0.114;
        const double kg = 1.0 - kr - kb;
        // Y' = ys * Y + yo,  C' = cs * C + co   (C' in [-0.5, 0.5])
        const bool limited = range == YCbCrRange::limited;
        const double ys = limited ? 255.0 / 219.0 : 1.0;
        const double yo = limited ? -16.0 / 219.0 : 0.0;
        const double cs = limited ? 255.0 / 224.0 : 1.0;
        const double co = limited ? -128.0 / 224.0 : -128.0 / 255.0;
        // R = Y' + crR C'r,  G = Y' + cbG C'b + crG C'r,  B = Y' + cbB C'b
        const double crR = 2.0 * (1.0 - kr);
        const double cbB = 2.0 * (1.0 - kb);
        const double cbG = -cbB * kb / kg;
        const double crG = -crR * kr / kg;
        const double coef[3][2] = {{0.0, crR}, {cbG, crG}, {cbB, 0.0}};
        for (int i = 0; i < 3; ++i) {
            rows[i][0] = float(ys);
            rows[i][1] = float(coef[i][0] * cs);
            rows[i][2] = float(coef[i][1] * cs);
            rows[i][3] = float(yo + (coef[i][0] + coef[i][1]) * co);
        }
    }

    // Rows that pass an RGB source through (sampled as (R, G, B) in the
    // (Y, Cb, Cr) slots).
    inline void identityRgbRows(float rows[3][4]) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 4; ++j) rows[i][j] = (j == i) ? 1.0f : 0.0f;
    }

}
