#include <gtest/gtest.h>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/compare_op.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// BufferUsage
// ---------------------------------------------------------------------------

TEST(BufferUsage, FlagValues) {
    EXPECT_EQ(static_cast<int>(BufferUsage::mapRead),      0x0001);
    EXPECT_EQ(static_cast<int>(BufferUsage::mapWrite),     0x0002);
    EXPECT_EQ(static_cast<int>(BufferUsage::copySrc),      0x0004);
    EXPECT_EQ(static_cast<int>(BufferUsage::copyDst),      0x0008);
    EXPECT_EQ(static_cast<int>(BufferUsage::index),        0x0010);
    EXPECT_EQ(static_cast<int>(BufferUsage::vertex),       0x0020);
    EXPECT_EQ(static_cast<int>(BufferUsage::uniform),      0x0040);
    EXPECT_EQ(static_cast<int>(BufferUsage::storage),      0x0080);
    EXPECT_EQ(static_cast<int>(BufferUsage::indirect),     0x0100);
    EXPECT_EQ(static_cast<int>(BufferUsage::queryResolve), 0x0200);
}

TEST(BufferUsage, FlagsAreUnique) {
    // Each flag must be a distinct power-of-two so they can be combined.
    const int flags[] = {
        static_cast<int>(BufferUsage::mapRead),
        static_cast<int>(BufferUsage::mapWrite),
        static_cast<int>(BufferUsage::copySrc),
        static_cast<int>(BufferUsage::copyDst),
        static_cast<int>(BufferUsage::index),
        static_cast<int>(BufferUsage::vertex),
        static_cast<int>(BufferUsage::uniform),
        static_cast<int>(BufferUsage::storage),
        static_cast<int>(BufferUsage::indirect),
        static_cast<int>(BufferUsage::queryResolve),
    };
    for (int i = 0; i < 10; ++i) {
        for (int j = i + 1; j < 10; ++j) {
            EXPECT_EQ(flags[i] & flags[j], 0)
                << "Flags at index " << i << " and " << j << " overlap";
        }
    }
}

// ---------------------------------------------------------------------------
// PixelFormat
// ---------------------------------------------------------------------------

TEST(PixelFormat, InvalidIsZero) {
    EXPECT_EQ(static_cast<int>(PixelFormat::invalid), 0);
}

TEST(PixelFormat, EightBitFormats) {
    EXPECT_EQ(static_cast<int>(PixelFormat::r8unorm), 10);
    EXPECT_EQ(static_cast<int>(PixelFormat::r8snorm), 12);
    EXPECT_EQ(static_cast<int>(PixelFormat::r8uint),  13);
    EXPECT_EQ(static_cast<int>(PixelFormat::r8sint),  14);
}

TEST(PixelFormat, ThirtyTwoBitFormats) {
    EXPECT_EQ(static_cast<int>(PixelFormat::rgba8unorm),     70);
    EXPECT_EQ(static_cast<int>(PixelFormat::rgba8unorm_srgb),71);
    EXPECT_EQ(static_cast<int>(PixelFormat::bgra8unorm),     80);
    EXPECT_EQ(static_cast<int>(PixelFormat::bgra8unorm_srgb),81);
}

TEST(PixelFormat, DepthStencilFormats) {
    EXPECT_EQ(static_cast<int>(PixelFormat::depth16unorm),         250);
    EXPECT_EQ(static_cast<int>(PixelFormat::depth32float),         252);
    EXPECT_EQ(static_cast<int>(PixelFormat::stencil8),             253);
    EXPECT_EQ(static_cast<int>(PixelFormat::depth24plus_stencil8), 255);
    EXPECT_EQ(static_cast<int>(PixelFormat::depth32float_stencil8),260);
}

TEST(PixelFormat, CompressedFormats) {
    EXPECT_EQ(static_cast<int>(PixelFormat::bc1_rgba_unorm),     130);
    EXPECT_EQ(static_cast<int>(PixelFormat::bc7_rgba_unorm),     152);
    EXPECT_EQ(static_cast<int>(PixelFormat::etc2_rgb8unorm),     180);
    EXPECT_EQ(static_cast<int>(PixelFormat::eac_r11unorm),       170);
    EXPECT_EQ(static_cast<int>(PixelFormat::astc_4x4_unorm_srgb),186);
}

// ---------------------------------------------------------------------------
// FilterMode
// ---------------------------------------------------------------------------

TEST(FilterMode, Values) {
    EXPECT_EQ(static_cast<int>(FilterMode::fmUnknown),             0);
    EXPECT_EQ(static_cast<int>(FilterMode::fmNearest),          9728);
    EXPECT_EQ(static_cast<int>(FilterMode::fmLinear),           9729);
    EXPECT_EQ(static_cast<int>(FilterMode::fmNearestMipmapNearest), 9984);
    EXPECT_EQ(static_cast<int>(FilterMode::fmLinearMipmapNearest),  9985);
    EXPECT_EQ(static_cast<int>(FilterMode::fmNearestMipmapLinear),  9986);
    EXPECT_EQ(static_cast<int>(FilterMode::fmLinearMipmapLinear),   9987);
}

// ---------------------------------------------------------------------------
// WrapMode
// ---------------------------------------------------------------------------

TEST(WrapMode, Values) {
    EXPECT_EQ(static_cast<int>(WrapMode::clampToEdge),  33071);
    EXPECT_EQ(static_cast<int>(WrapMode::repeat),       10497);
    EXPECT_EQ(static_cast<int>(WrapMode::mirrorRepeat), 33648);
}

// ---------------------------------------------------------------------------
// QuerySetType
// ---------------------------------------------------------------------------

TEST(QuerySetType, Enumerators) {
    // Values must be distinct; cast to int to compare easily.
    EXPECT_NE(static_cast<int>(QuerySetType::occlusion),
              static_cast<int>(QuerySetType::timestamp));
}
