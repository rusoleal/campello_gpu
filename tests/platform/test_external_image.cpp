// Platform integration tests for copyExternalImageToTexture().
//
// Each test builds a synthetic frame with known YCbCr (or RGB) values in the
// platform's image type, copies it into an RGBA texture and compares the
// readback with a CPU reference conversion. Tests skip when the device does
// not support the platform's image type.

#include <gtest/gtest.h>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/external_image.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/validation_diagnostics.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace systems::leal::campello_gpu;

namespace {

std::shared_ptr<Device> createDevice() { return Device::createDefaultDevice(nullptr); }

#if defined(__APPLE__)
constexpr ExternalImageType kPlatformType = ExternalImageType::cvPixelBuffer;
#elif defined(__EMSCRIPTEN__)
constexpr ExternalImageType kPlatformType = ExternalImageType::webImageSource;
#elif defined(__ANDROID__)
constexpr ExternalImageType kPlatformType = ExternalImageType::androidHardwareBuffer;
#else
constexpr ExternalImageType kPlatformType = ExternalImageType::dmaBuf;
#endif

struct Rgb { int r, g, b; };

// Reference conversion (BT.601 / BT.709, limited or full range), 8-bit.
Rgb reference(int y, int cb, int cr, bool bt709, bool full) {
    const double kr = bt709 ? 0.2126 : 0.299, kb = bt709 ? 0.0722 : 0.114, kg = 1.0 - kr - kb;
    const double Y = full ? y / 255.0 : (y - 16) / 219.0;
    const double Cb = full ? (cb - 128) / 255.0 : (cb - 128) / 224.0;
    const double Cr = full ? (cr - 128) / 255.0 : (cr - 128) / 224.0;
    const double R = Y + 2 * (1 - kr) * Cr;
    const double B = Y + 2 * (1 - kb) * Cb;
    const double G = (Y - kr * R - kb * B) / kg;
    auto q = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return {q(R), q(G), q(B)};
}

// A frame description, independent of the platform image type.
struct Frame {
    uint32_t width = 0, height = 0;
    enum Layout { nv12, i420, bgra } layout = nv12;
    std::vector<uint8_t> y, cb, cr;  // full-size Y, half-size chroma (nv12/i420)
    std::vector<uint8_t> pixels;     // bgra layout, B G R A
    bool bt709 = true, full = false; // what the image's metadata says
};

// Y ramps across the image; chroma is constant so bilinear chroma
// upsampling doesn't blur the expected values.
Frame makeYuvFrame(uint32_t w, uint32_t h, Frame::Layout layout, int cb, int cr, bool bt709, bool full) {
    Frame f;
    f.width = w; f.height = h; f.layout = layout; f.bt709 = bt709; f.full = full;
    f.y.resize(size_t(w) * h);
    const int lo = full ? 0 : 16, hi = full ? 255 : 235;
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i) f.y[j * w + i] = uint8_t(lo + (hi - lo) * ((i + 3 * j) % 17) / 16);
    const size_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    f.cb.assign(cw * ch, uint8_t(cb));
    f.cr.assign(cw * ch, uint8_t(cr));
    return f;
}

Frame makeBgraFrame(uint32_t w, uint32_t h) {
    Frame f;
    f.width = w; f.height = h; f.layout = Frame::bgra;
    f.pixels.resize(size_t(w) * h * 4);
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i) {
            uint8_t *p = &f.pixels[(j * w + i) * 4];
            p[0] = uint8_t(i * 255 / std::max(1u, w - 1));  // B
            p[1] = uint8_t(j * 255 / std::max(1u, h - 1));  // G
            p[2] = uint8_t((i + j) * 7);                    // R
            p[3] = 255;
        }
    return f;
}

// Expected RGB of the source pixel (i, j).
Rgb expectedAt(const Frame &f, uint32_t i, uint32_t j) {
    if (f.layout == Frame::bgra) {
        const uint8_t *p = &f.pixels[(j * f.width + i) * 4];
        return {p[2], p[1], p[0]};
    }
    const size_t c = (j / 2) * ((f.width + 1) / 2) + i / 2;
    return reference(f.y[j * f.width + i], f.cb[c], f.cr[c], f.bt709, f.full);
}

// ---------------------------------------------------------------------------
// Platform image from a Frame
// ---------------------------------------------------------------------------

struct PlatformImage {
    ExternalImage image;
    void release();
};

#if defined(__APPLE__)

PlatformImage makePlatformImage(const Frame &f) {
    OSType format = kCVPixelFormatType_32BGRA;
    if (f.layout == Frame::nv12)
        format = f.full ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    if (f.layout == Frame::i420)
        format = f.full ? kCVPixelFormatType_420YpCbCr8PlanarFullRange : kCVPixelFormatType_420YpCbCr8Planar;
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef empty = CFDictionaryCreate(kCFAllocatorDefault, nullptr, nullptr, 0,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, empty);
    CFDictionarySetValue(attrs, kCVPixelBufferMetalCompatibilityKey, kCFBooleanTrue);
    CVPixelBufferRef buffer = nullptr;
    CVPixelBufferCreate(kCFAllocatorDefault, f.width, f.height, format, attrs, &buffer);
    CFRelease(empty);
    CFRelease(attrs);
    EXPECT_NE(buffer, nullptr);
    if (!buffer) return {};

    CVPixelBufferLockBaseAddress(buffer, 0);
    auto copyPlane = [&](size_t plane, const uint8_t *src, size_t rowBytes, size_t rows) {
        auto *dst = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(buffer, plane));
        const size_t stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, plane);
        for (size_t r = 0; r < rows; ++r) std::copy(src + r * rowBytes, src + (r + 1) * rowBytes, dst + r * stride);
    };
    const size_t cw = (f.width + 1) / 2, ch = (f.height + 1) / 2;
    if (f.layout == Frame::bgra) {
        auto *dst = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
        const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        for (uint32_t r = 0; r < f.height; ++r)
            std::copy(&f.pixels[r * f.width * 4], &f.pixels[(r + 1) * f.width * 4], dst + r * stride);
    } else if (f.layout == Frame::nv12) {
        copyPlane(0, f.y.data(), f.width, f.height);
        std::vector<uint8_t> uv(cw * ch * 2);
        for (size_t k = 0; k < cw * ch; ++k) { uv[2 * k] = f.cb[k]; uv[2 * k + 1] = f.cr[k]; }
        copyPlane(1, uv.data(), cw * 2, ch);
    } else {
        copyPlane(0, f.y.data(), f.width, f.height);
        copyPlane(1, f.cb.data(), cw, ch);
        copyPlane(2, f.cr.data(), cw, ch);
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    if (f.layout != Frame::bgra) {
        CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey,
                              f.bt709 ? kCVImageBufferYCbCrMatrix_ITU_R_709_2 : kCVImageBufferYCbCrMatrix_ITU_R_601_4,
                              kCVAttachmentMode_ShouldPropagate);
    }
    PlatformImage p;
    p.image.type = ExternalImageType::cvPixelBuffer;
    p.image.handle = buffer;
    return p;
}

void PlatformImage::release() {
    if (image.handle) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(image.handle));
    image.handle = nullptr;
}

long retainCount(const PlatformImage &p) { return CFGetRetainCount(p.image.handle); }

#elif defined(__EMSCRIPTEN__)

// A VideoFrame in NV12 / I420 / BGRA. Its color space says sRGB transfer, so
// the browser applies only the YCbCr matrix and range (no transfer change).
EM_JS(int, campello_test_make_video_frame,
      (int layout, int width, int height, const uint8_t *data, int bytes, int bt709, int full), {
    const format = layout == 0 ? 'NV12' : layout == 1 ? 'I420' : 'BGRA';
    const init = {
        format, codedWidth: width, codedHeight: height, timestamp: 0,
        colorSpace: { matrix: bt709 ? 'bt709' : 'smpte170m', fullRange: !!full,
                      primaries: 'bt709', transfer: 'iec61966-2-1' },
    };
    const frame = new VideoFrame(HEAPU8.slice(data, data + bytes), init);
    Module.campelloTestFrames = Module.campelloTestFrames || {};
    const handle = Module.campelloGpu.importExternalImage(frame);
    Module.campelloTestFrames[handle] = frame;
    return handle;
});

EM_JS(void, campello_test_close_video_frame, (int handle), {
    const frame = Module.campelloTestFrames && Module.campelloTestFrames[handle];
    if (frame) { frame.close(); delete Module.campelloTestFrames[handle]; }
});

PlatformImage makePlatformImage(const Frame &f) {
    std::vector<uint8_t> data;
    const size_t cw = (f.width + 1) / 2, ch = (f.height + 1) / 2;
    if (f.layout == Frame::bgra) {
        data = f.pixels;
    } else if (f.layout == Frame::nv12) {
        data = f.y;
        for (size_t k = 0; k < cw * ch; ++k) { data.push_back(f.cb[k]); data.push_back(f.cr[k]); }
    } else {
        data = f.y;
        data.insert(data.end(), f.cb.begin(), f.cb.end());
        data.insert(data.end(), f.cr.begin(), f.cr.end());
    }
    PlatformImage p;
    p.image.type = ExternalImageType::webImageSource;
    p.image.handle = reinterpret_cast<void *>(intptr_t(campello_test_make_video_frame(
        int(f.layout), int(f.width), int(f.height), data.data(), int(data.size()), f.bt709, f.full)));
    return p;
}

void PlatformImage::release() {
    if (image.handle) campello_test_close_video_frame(int(reinterpret_cast<intptr_t>(image.handle)));
    image.handle = nullptr;
}

#else

// No test image factory yet (Android: AHardwareBuffers with Y8Cb8Cr8_420
// written through AHardwareBuffer_lockPlanes; Linux, Windows: no import yet).
#define CAMPELLO_TEST_NO_IMAGE_FACTORY 1
PlatformImage makePlatformImage(const Frame &) { return {}; }
void PlatformImage::release() {}

#endif


std::shared_ptr<Texture> makeTarget(const std::shared_ptr<Device> &device, uint32_t w, uint32_t h,
                                    PixelFormat format = PixelFormat::rgba8unorm) {
    return device->createTexture(TextureType::tt2d, format, w, h, 1, 1, 1,
                                 static_cast<TextureUsage>(static_cast<int>(TextureUsage::renderTarget) |
                                                           static_cast<int>(TextureUsage::textureBinding) |
                                                           static_cast<int>(TextureUsage::copySrc)));
}

std::vector<uint8_t> readback(const std::shared_ptr<Texture> &t) {
    std::vector<uint8_t> pixels(size_t(t->getWidth()) * t->getHeight() * 4);
    EXPECT_TRUE(t->download(0, 0, pixels.data(), pixels.size()));
    return pixels;
}

// Compares every target pixel with the source pixel it maps to; `map`
// returns the source pixel for target pixel (i, j).
template <typename Map>
void expectPixels(const Frame &f, const std::vector<uint8_t> &pixels, uint32_t w, uint32_t h, bool bgraTarget,
                  int tolerance, Map map) {
    int worst = 0, reported = 0;
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i) {
            uint32_t si, sj;
            map(i, j, si, sj);
            const Rgb e = expectedAt(f, si, sj);
            const uint8_t *p = &pixels[(size_t(j) * w + i) * 4];
            const Rgb got = bgraTarget ? Rgb{p[2], p[1], p[0]} : Rgb{p[0], p[1], p[2]};
            const int d = std::max({std::abs(got.r - e.r), std::abs(got.g - e.g), std::abs(got.b - e.b)});
            worst = std::max(worst, d);
            if (d > tolerance && reported++ < 5) {
                ADD_FAILURE() << "pixel (" << i << ", " << j << "): got (" << got.r << ", " << got.g << ", " << got.b
                              << "), expected (" << e.r << ", " << e.g << ", " << e.b << ")";
            }
        }
    EXPECT_LE(worst, tolerance);
}

void identityMap(uint32_t i, uint32_t j, uint32_t &si, uint32_t &sj) { si = i; sj = j; }

bool copyWithEncoder(const std::shared_ptr<Device> &device, const ExternalImage &image,
                     const std::shared_ptr<Texture> &target, const ExternalImageCopyOptions &options = {}) {
    auto encoder = device->createCommandEncoder();
    if (!encoder || !encoder->copyExternalImageToTexture(image, target, options)) return false;
    device->submit(encoder->finish());
    return true;
}

// The web converts in the browser, whose rounding differs slightly.
#if defined(__EMSCRIPTEN__)
constexpr int kTolerance = 3;
#else
constexpr int kTolerance = 2;
#endif

class ExternalImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        device = createDevice();
        if (!device) GTEST_SKIP() << "no device";
        if (!device->supportsExternalImage(kPlatformType)) GTEST_SKIP() << "external images unsupported here";
#if defined(CAMPELLO_TEST_NO_IMAGE_FACTORY)
        GTEST_SKIP() << "no test images for this platform's external image type yet";
#endif
        resetValidationErrorCount();
    }
    void TearDown() override {
        if (device) EXPECT_EQ(validationErrorCount(), 0u) << "GPU validation errors";
    }
    std::shared_ptr<Device> device;
};

} // namespace

TEST(ExternalImage, UnsupportedTypesAreRejected) {
    auto device = createDevice();
    if (!device) GTEST_SKIP() << "no device";
    for (auto type : {ExternalImageType::androidHardwareBuffer, ExternalImageType::cvPixelBuffer,
                      ExternalImageType::d3d12Resource, ExternalImageType::d3dSharedHandle,
                      ExternalImageType::dmaBuf, ExternalImageType::webImageSource}) {
        if (device->supportsExternalImage(type)) continue;
        auto target = makeTarget(device, 4, 4);
        ExternalImage image;
        image.type = type;
        image.handle = reinterpret_cast<void *>(intptr_t(1));  // never dereferenced
        EXPECT_FALSE(device->copyExternalImageToTexture(image, target));
        auto encoder = device->createCommandEncoder();
        EXPECT_FALSE(encoder->copyExternalImageToTexture(image, target));
    }
}

TEST_F(ExternalImageTest, Nv12Bt709LimitedRange) {
    for (auto [cb, cr] : {std::pair{128, 128}, std::pair{90, 200}, std::pair{220, 60}}) {
        const Frame f = makeYuvFrame(64, 32, Frame::nv12, cb, cr, true, false);
        auto image = makePlatformImage(f);
        auto target = makeTarget(device, f.width, f.height);
        ASSERT_TRUE(copyWithEncoder(device, image.image, target));
        image.release();
        expectPixels(f, readback(target), f.width, f.height, false, kTolerance, identityMap);
    }
}

TEST_F(ExternalImageTest, Nv12Bt601FullRangeSynchronous) {
    const Frame f = makeYuvFrame(48, 40, Frame::nv12, 70, 180, false, true);
    auto image = makePlatformImage(f);
    auto target = makeTarget(device, f.width, f.height);
    ASSERT_TRUE(device->copyExternalImageToTexture(image.image, target));
    image.release();
    expectPixels(f, readback(target), f.width, f.height, false, kTolerance, identityMap);
}

TEST_F(ExternalImageTest, ThreePlaneYuv) {
    const Frame f = makeYuvFrame(32, 32, Frame::i420, 160, 100, true, false);
    auto image = makePlatformImage(f);
    auto target = makeTarget(device, f.width, f.height);
    ASSERT_TRUE(copyWithEncoder(device, image.image, target));
    image.release();
    expectPixels(f, readback(target), f.width, f.height, false, kTolerance, identityMap);
}

TEST_F(ExternalImageTest, BgraSourceIntoBgraAndSrgbTargets) {
    const Frame f = makeBgraFrame(40, 24);
    for (auto format : {PixelFormat::bgra8unorm, PixelFormat::rgba8unorm_srgb}) {
        auto image = makePlatformImage(f);
        auto target = makeTarget(device, f.width, f.height, format);
        ASSERT_TRUE(copyWithEncoder(device, image.image, target));
        image.release();
        // An _srgb target stores the source's bytes, as a plain one does.
        expectPixels(f, readback(target), f.width, f.height, format == PixelFormat::bgra8unorm,
                     format == PixelFormat::bgra8unorm ? 0 : 1, identityMap);
    }
}

TEST_F(ExternalImageTest, ColorOverridesWinOverMetadata) {
    // The image says BT.709 limited; the caller says BT.601 full.
    Frame f = makeYuvFrame(32, 16, Frame::nv12, 100, 190, true, false);
    auto image = makePlatformImage(f);
    image.image.matrix = YCbCrMatrix::bt601;
    image.image.range = YCbCrRange::full;
    auto target = makeTarget(device, f.width, f.height);
    const bool ok = copyWithEncoder(device, image.image, target);
    image.release();
#if defined(__EMSCRIPTEN__)
    // The browser converts from the frame's own color space: overrides are ignored.
    ASSERT_TRUE(ok);
    expectPixels(f, readback(target), f.width, f.height, false, kTolerance, identityMap);
#else
    ASSERT_TRUE(ok);
    f.bt709 = false;
    f.full = true;
    expectPixels(f, readback(target), f.width, f.height, false, kTolerance, identityMap);
#endif
}

TEST_F(ExternalImageTest, UvTransformMirrorsAndRotates) {
    const Frame f = makeBgraFrame(32, 32);
    // Horizontal mirror: u' = 1 - u.
    {
        const float mirror[6] = {-1, 0, 1, 0, 1, 0};
        ExternalImageCopyOptions options;
        options.uvTransform = mirror;
        auto image = makePlatformImage(f);
        auto target = makeTarget(device, 32, 32);
        ASSERT_TRUE(copyWithEncoder(device, image.image, target, options));
        image.release();
        expectPixels(f, readback(target), 32, 32, false, 0,
                     [](uint32_t i, uint32_t j, uint32_t &si, uint32_t &sj) { si = 31 - i; sj = j; });
    }
    // 90° rotation: target (u, v) samples source (v, 1 - u).
    {
        const float rotate[6] = {0, 1, 0, -1, 0, 1};
        ExternalImageCopyOptions options;
        options.uvTransform = rotate;
        auto image = makePlatformImage(f);
        auto target = makeTarget(device, 32, 32);
        ASSERT_TRUE(copyWithEncoder(device, image.image, target, options));
        image.release();
        expectPixels(f, readback(target), 32, 32, false, 0,
                     [](uint32_t i, uint32_t j, uint32_t &si, uint32_t &sj) { si = j; sj = 31 - i; });
    }
}

TEST_F(ExternalImageTest, CropAndScaleIntoASmallerTarget) {
    // The source's right half, scaled to a target half as wide: one texel each.
    const Frame f = makeBgraFrame(64, 16);
    const float crop[6] = {0.5f, 0, 0.5f, 0, 1, 0};
    ExternalImageCopyOptions options;
    options.uvTransform = crop;
    auto image = makePlatformImage(f);
    auto target = makeTarget(device, 32, 16);
    ASSERT_TRUE(copyWithEncoder(device, image.image, target, options));
    image.release();
    expectPixels(f, readback(target), 32, 16, false, 0,
                 [](uint32_t i, uint32_t j, uint32_t &si, uint32_t &sj) { si = 32 + i; sj = j; });
}

TEST_F(ExternalImageTest, OddSizes) {
    const Frame f = makeYuvFrame(37, 23, Frame::nv12, 128, 128, false, false);
    auto image = makePlatformImage(f);
    auto target = makeTarget(device, f.width, f.height);
    ASSERT_TRUE(copyWithEncoder(device, image.image, target));
    image.release();
    expectPixels(f, readback(target), f.width, f.height, false, kTolerance, identityMap);
}

TEST_F(ExternalImageTest, InvalidTargetsAreRejected) {
    const Frame f = makeBgraFrame(8, 8);
    auto image = makePlatformImage(f);
    // No renderTarget usage.
    auto sampledOnly = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, 8, 8, 1, 1, 1,
                                             TextureUsage::textureBinding);
    EXPECT_FALSE(device->copyExternalImageToTexture(image.image, sampledOnly));
    // Not an 8-bit RGBA/BGRA format.
    auto floatTarget = makeTarget(device, 8, 8, PixelFormat::rgba32float);
    EXPECT_FALSE(device->copyExternalImageToTexture(image.image, floatTarget));
    image.release();
}

#if defined(__APPLE__)
TEST_F(ExternalImageTest, SourceIsKeptAliveUntilTheGpuFinishes) {
    const Frame f = makeYuvFrame(64, 64, Frame::nv12, 128, 128, true, false);
    auto image = makePlatformImage(f);
    auto target = makeTarget(device, 64, 64);
    const long before = retainCount(image);
    auto encoder = device->createCommandEncoder();
    ASSERT_TRUE(encoder->copyExternalImageToTexture(image.image, target));
    EXPECT_GT(retainCount(image), before);  // the encoder holds a reference
    device->submit(encoder->finish());
    expectPixels(f, readback(target), 64, 64, false, kTolerance, identityMap);  // waits for the GPU
    for (int i = 0; i < 100 && retainCount(image) > before; ++i) {
        // Completion handlers run on a Metal thread shortly after the GPU is done.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
    }
    EXPECT_EQ(retainCount(image), before);
    image.release();
}
#endif
