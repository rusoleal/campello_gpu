// copyExternalImageToTexture() on Vulkan: AHardwareBuffers on Android
// (android_hardware_buffer.cpp). Linux dma-buf import is planned; until then
// no type is supported there.

#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture.hpp>

#include "command_encoder_handle.hpp"
#include "common.hpp"
#include "texture_handle.hpp"

#include <unistd.h>

using namespace systems::leal::campello_gpu;

namespace {
    // A fence the call doesn't get to wait for is still owned by it.
    bool reject(const ExternalImage &source) {
        if (source.acquireFenceFd >= 0) close(source.acquireFenceFd);
        return false;
    }
}

bool Device::supportsExternalImage(ExternalImageType type) const {
#if defined(__ANDROID__)
    return type == ExternalImageType::androidHardwareBuffer && ((DeviceData *)native)->ahbImportEnabled;
#else
    (void)type;
    return false;
#endif
}

bool Device::copyExternalImageToTexture(const ExternalImage &source, std::shared_ptr<Texture> target,
                                        const ExternalImageCopyOptions &options) {
#if defined(__ANDROID__)
    if (native && target && source.type == ExternalImageType::androidHardwareBuffer) {
        return copyAhbSync((DeviceData *)native, source, (TextureHandle *)target->native, options.uvTransform);
    }
#else
    (void)target;
    (void)options;
#endif
    return reject(source);
}

bool CommandEncoder::copyExternalImageToTexture(const ExternalImage &source, std::shared_ptr<Texture> target,
                                                const ExternalImageCopyOptions &options) {
#if defined(__ANDROID__)
    if (native && target && source.type == ExternalImageType::androidHardwareBuffer) {
        return recordAhbCopy((CommandEncoderHandle *)native, source, (TextureHandle *)target->native,
                             options.uvTransform);
    }
#else
    (void)target;
    (void)options;
#endif
    return reject(source);
}
