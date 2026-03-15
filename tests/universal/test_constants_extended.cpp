#include <gtest/gtest.h>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/storage_mode.hpp>
#include <campello_gpu/constants/aspect.hpp>
#include <campello_gpu/constants/color_space.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// ShaderStage
// ---------------------------------------------------------------------------

TEST(ShaderStage, FlagValues) {
    EXPECT_EQ(static_cast<int>(ShaderStage::vertex),   0x01);
    EXPECT_EQ(static_cast<int>(ShaderStage::fragment), 0x02);
    EXPECT_EQ(static_cast<int>(ShaderStage::compute),  0x04);
}

TEST(ShaderStage, FlagsAreUnique) {
    const int flags[] = {
        static_cast<int>(ShaderStage::vertex),
        static_cast<int>(ShaderStage::fragment),
        static_cast<int>(ShaderStage::compute),
    };
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            EXPECT_EQ(flags[i] & flags[j], 0)
                << "ShaderStage flags at index " << i << " and " << j << " overlap";
}

TEST(ShaderStage, CanBeCombined) {
    int combined = static_cast<int>(ShaderStage::vertex) | static_cast<int>(ShaderStage::fragment);
    EXPECT_EQ(combined & static_cast<int>(ShaderStage::vertex),   static_cast<int>(ShaderStage::vertex));
    EXPECT_EQ(combined & static_cast<int>(ShaderStage::fragment), static_cast<int>(ShaderStage::fragment));
    EXPECT_EQ(combined & static_cast<int>(ShaderStage::compute),  0);
}

// ---------------------------------------------------------------------------
// TextureUsage
// ---------------------------------------------------------------------------

TEST(TextureUsage, FlagValues) {
    EXPECT_EQ(static_cast<int>(TextureUsage::copySrc),       0x01);
    EXPECT_EQ(static_cast<int>(TextureUsage::copyDst),       0x02);
    EXPECT_EQ(static_cast<int>(TextureUsage::textureBinding),0x04);
    EXPECT_EQ(static_cast<int>(TextureUsage::storageBinding),0x08);
    EXPECT_EQ(static_cast<int>(TextureUsage::renderTarget),  0x10);
}

TEST(TextureUsage, FlagsAreUnique) {
    const int flags[] = {
        static_cast<int>(TextureUsage::copySrc),
        static_cast<int>(TextureUsage::copyDst),
        static_cast<int>(TextureUsage::textureBinding),
        static_cast<int>(TextureUsage::storageBinding),
        static_cast<int>(TextureUsage::renderTarget),
    };
    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j)
            EXPECT_EQ(flags[i] & flags[j], 0)
                << "TextureUsage flags at index " << i << " and " << j << " overlap";
}

// ---------------------------------------------------------------------------
// TextureType
// ---------------------------------------------------------------------------

TEST(TextureType, Values) {
    EXPECT_EQ(static_cast<int>(TextureType::tt1d), 0);
    EXPECT_EQ(static_cast<int>(TextureType::tt2d), 1);
    EXPECT_EQ(static_cast<int>(TextureType::tt3d), 2);
}

TEST(TextureType, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(TextureType::tt1d), static_cast<int>(TextureType::tt2d));
    EXPECT_NE(static_cast<int>(TextureType::tt2d), static_cast<int>(TextureType::tt3d));
    EXPECT_NE(static_cast<int>(TextureType::tt1d), static_cast<int>(TextureType::tt3d));
}

// ---------------------------------------------------------------------------
// IndexFormat
// ---------------------------------------------------------------------------

TEST(IndexFormat, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(IndexFormat::uint16), static_cast<int>(IndexFormat::uint32));
}

// ---------------------------------------------------------------------------
// CullMode
// ---------------------------------------------------------------------------

TEST(CullMode, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(CullMode::none),  static_cast<int>(CullMode::front));
    EXPECT_NE(static_cast<int>(CullMode::none),  static_cast<int>(CullMode::back));
    EXPECT_NE(static_cast<int>(CullMode::front), static_cast<int>(CullMode::back));
}

// ---------------------------------------------------------------------------
// FrontFace
// ---------------------------------------------------------------------------

TEST(FrontFace, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(FrontFace::ccw), static_cast<int>(FrontFace::cw));
}

// ---------------------------------------------------------------------------
// CompareOp
// ---------------------------------------------------------------------------

TEST(CompareOp, ValuesAreDistinct) {
    const CompareOp ops[] = {
        CompareOp::never, CompareOp::less, CompareOp::equal, CompareOp::lessEqual,
        CompareOp::greater, CompareOp::notEqual, CompareOp::greaterEqual, CompareOp::always
    };
    for (int i = 0; i < 8; ++i)
        for (int j = i + 1; j < 8; ++j)
            EXPECT_NE(static_cast<int>(ops[i]), static_cast<int>(ops[j]))
                << "CompareOp values at index " << i << " and " << j << " collide";
}

// ---------------------------------------------------------------------------
// StencilOp
// ---------------------------------------------------------------------------

TEST(StencilOp, ValuesAreDistinct) {
    const StencilOp ops[] = {
        StencilOp::keep, StencilOp::zero, StencilOp::replace,
        StencilOp::incrementClamp, StencilOp::decrementClamp,
        StencilOp::invert,
        StencilOp::incrementWrap, StencilOp::decrementWrap,
    };
    for (int i = 0; i < 8; ++i)
        for (int j = i + 1; j < 8; ++j)
            EXPECT_NE(static_cast<int>(ops[i]), static_cast<int>(ops[j]))
                << "StencilOp values at index " << i << " and " << j << " collide";
}

// ---------------------------------------------------------------------------
// PrimitiveTopology
// ---------------------------------------------------------------------------

TEST(PrimitiveTopology, ValuesAreDistinct) {
    const PrimitiveTopology topos[] = {
        PrimitiveTopology::pointList,
        PrimitiveTopology::lineList,
        PrimitiveTopology::lineStrip,
        PrimitiveTopology::triangleList,
        PrimitiveTopology::triangleStrip,
    };
    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j)
            EXPECT_NE(static_cast<int>(topos[i]), static_cast<int>(topos[j]))
                << "PrimitiveTopology values at index " << i << " and " << j << " collide";
}

// ---------------------------------------------------------------------------
// StorageMode
// ---------------------------------------------------------------------------

TEST(StorageMode, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(StorageMode::hostVisible),
              static_cast<int>(StorageMode::devicePrivate));
    EXPECT_NE(static_cast<int>(StorageMode::devicePrivate),
              static_cast<int>(StorageMode::deviceTransient));
    EXPECT_NE(static_cast<int>(StorageMode::hostVisible),
              static_cast<int>(StorageMode::deviceTransient));
}

// ---------------------------------------------------------------------------
// Aspect
// ---------------------------------------------------------------------------

TEST(Aspect, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(Aspect::all),        static_cast<int>(Aspect::depthOnly));
    EXPECT_NE(static_cast<int>(Aspect::all),        static_cast<int>(Aspect::stencilOnly));
    EXPECT_NE(static_cast<int>(Aspect::depthOnly),  static_cast<int>(Aspect::stencilOnly));
}

// ---------------------------------------------------------------------------
// ColorSpace
// ---------------------------------------------------------------------------

TEST(ColorSpace, UndefinedIsFirst) {
    EXPECT_EQ(static_cast<int>(ColorSpace::undefined), 0);
}

TEST(ColorSpace, CommonValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(ColorSpace::srgbNonLinear),
              static_cast<int>(ColorSpace::displayP3NonLinear));
    EXPECT_NE(static_cast<int>(ColorSpace::srgbNonLinear),
              static_cast<int>(ColorSpace::hdr10St2084));
    EXPECT_NE(static_cast<int>(ColorSpace::displayP3NonLinear),
              static_cast<int>(ColorSpace::hdr10St2084));
}

// ---------------------------------------------------------------------------
// ComponentType (vertex_descriptor)
// ---------------------------------------------------------------------------

TEST(ComponentType, GltfValues) {
    EXPECT_EQ(static_cast<int>(ComponentType::ctByte),          5120);
    EXPECT_EQ(static_cast<int>(ComponentType::ctUnsignedByte),  5121);
    EXPECT_EQ(static_cast<int>(ComponentType::ctShort),         5122);
    EXPECT_EQ(static_cast<int>(ComponentType::ctUnsignedShort), 5123);
    EXPECT_EQ(static_cast<int>(ComponentType::ctUnsignedInt),   5125);
    EXPECT_EQ(static_cast<int>(ComponentType::ctFloat),         5126);
}

TEST(ComponentType, ValuesAreDistinct) {
    const ComponentType types[] = {
        ComponentType::ctByte, ComponentType::ctUnsignedByte,
        ComponentType::ctShort, ComponentType::ctUnsignedShort,
        ComponentType::ctUnsignedInt, ComponentType::ctFloat,
    };
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            EXPECT_NE(static_cast<int>(types[i]), static_cast<int>(types[j]));
}

// ---------------------------------------------------------------------------
// AccessorType (vertex_descriptor)
// ---------------------------------------------------------------------------

TEST(AccessorType, ValuesAreDistinct) {
    const AccessorType types[] = {
        AccessorType::acScalar, AccessorType::acVec2, AccessorType::acVec3,
        AccessorType::acVec4,   AccessorType::acMat2, AccessorType::acMat3,
        AccessorType::acMat4,
    };
    for (int i = 0; i < 7; ++i)
        for (int j = i + 1; j < 7; ++j)
            EXPECT_NE(static_cast<int>(types[i]), static_cast<int>(types[j]));
}

// ---------------------------------------------------------------------------
// StepMode (vertex_descriptor)
// ---------------------------------------------------------------------------

TEST(StepMode, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(StepMode::vertex), static_cast<int>(StepMode::instance));
}
