#include <gtest/gtest.h>
#include <campello_gpu/constants/pixel_format.hpp>

// getPixelFormatSize and pixelFormatToString are declared in pixel_format.hpp
// and defined in src/pi/utils.cpp, which is compiled directly into this test
// target (see tests/CMakeLists.txt). They are NOT part of the macOS library
// build, so we cannot rely on the shared library for these functions.

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// getPixelFormatSize
// ---------------------------------------------------------------------------

TEST(GetPixelFormatSize, InvalidReturnsZero) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::invalid), 0u);
}

TEST(GetPixelFormatSize, EightBitFormats) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r8unorm),  8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r8snorm),  8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r8uint),   8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r8sint),   8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::stencil8), 8u);
}

TEST(GetPixelFormatSize, SixteenBitFormats) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r16unorm),   16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r16snorm),   16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r16uint),    16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r16sint),    16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r16float),   16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg8unorm),   16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg8snorm),   16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg8uint),    16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg8sint),    16u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::depth16unorm),16u);
}

TEST(GetPixelFormatSize, ThirtyTwoBitFormats) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r32uint),         32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r32sint),         32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::r32float),        32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba8unorm),      32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba8unorm_srgb), 32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bgra8unorm),      32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bgra8unorm_srgb), 32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgb9e5ufloat),    32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgb10a2uint),     32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgb10a2unorm),    32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg11b10ufloat),   32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::depth24plus_stencil8), 32u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::depth32float),    32u);
}

TEST(GetPixelFormatSize, SixtyFourBitFormats) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg32uint),    64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg32sint),    64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rg32float),   64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba16unorm), 64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba16snorm), 64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba16uint),  64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba16sint),  64u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba16float), 64u);
}

TEST(GetPixelFormatSize, OneHundredTwentyEightBitFormats) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba32uint),  128u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba32sint),  128u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::rgba32float), 128u);
}

TEST(GetPixelFormatSize, DepthStencilCombinedFormat) {
    // depth32float + 8-bit stencil = 40 bits
    EXPECT_EQ(getPixelFormatSize(PixelFormat::depth32float_stencil8), 40u);
}

TEST(GetPixelFormatSize, BCCompressedFormats) {
    // BC1: 4 bits/texel
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc1_rgba_unorm),     4u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc1_rgba_unorm_srgb),4u);
    // BC4: 4 bits/texel
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc4_r_unorm), 4u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc4_r_snorm), 4u);
    // BC2, BC3, BC5, BC6H, BC7: 8 bits/texel
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc2_rgba_unorm),     8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc3_rgba_unorm),     8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc5_rg_unorm),       8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc6h_rgb_ufloat),    8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc7_rgba_unorm),     8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::bc7_rgba_unorm_srgb),8u);
}

TEST(GetPixelFormatSize, ETC2CompressedFormats) {
    EXPECT_EQ(getPixelFormatSize(PixelFormat::etc2_rgb8unorm),      4u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::etc2_rgb8unorm_srgb), 4u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::etc2_rgb8a1unorm),    8u);
    EXPECT_EQ(getPixelFormatSize(PixelFormat::etc2_rgb8a1unorm_srgb),8u);
}

// ---------------------------------------------------------------------------
// pixelFormatToString
// ---------------------------------------------------------------------------

TEST(PixelFormatToString, InvalidFormat) {
    EXPECT_EQ(pixelFormatToString(PixelFormat::invalid), "invalid");
}

TEST(PixelFormatToString, CommonFormats) {
    EXPECT_EQ(pixelFormatToString(PixelFormat::r8unorm),        "r8unorm");
    EXPECT_EQ(pixelFormatToString(PixelFormat::rgba8unorm),     "rgba8unorm");
    EXPECT_EQ(pixelFormatToString(PixelFormat::bgra8unorm),     "bgra8unorm");
    EXPECT_EQ(pixelFormatToString(PixelFormat::rgba32float),    "rgba32float");
    EXPECT_EQ(pixelFormatToString(PixelFormat::depth32float),   "depth32float");
    EXPECT_EQ(pixelFormatToString(PixelFormat::stencil8),       "stencil8");
    EXPECT_EQ(pixelFormatToString(PixelFormat::bc7_rgba_unorm), "bc7_rgba_unorm");
}

TEST(PixelFormatToString, ReturnsNonEmptyString) {
    // Every named format should produce a non-empty string.
    const PixelFormat formats[] = {
        PixelFormat::r8unorm,   PixelFormat::rg8unorm,    PixelFormat::rgba8unorm,
        PixelFormat::r16float,  PixelFormat::rgba16float, PixelFormat::rgba32float,
        PixelFormat::depth16unorm, PixelFormat::depth32float,
        PixelFormat::bc1_rgba_unorm, PixelFormat::etc2_rgb8unorm,
        PixelFormat::astc_4x4_unorm_srgb,
    };
    for (auto fmt : formats) {
        EXPECT_FALSE(pixelFormatToString(fmt).empty())
            << "pixelFormatToString returned empty for format "
            << static_cast<int>(fmt);
    }
}
