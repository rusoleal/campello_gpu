// copyExternalImageToTexture() on backends without external image import yet
// (DirectX 12: planned). Every type is unsupported.

#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/device.hpp>

using namespace systems::leal::campello_gpu;

bool Device::supportsExternalImage(ExternalImageType) const { return false; }

bool Device::copyExternalImageToTexture(const ExternalImage &, std::shared_ptr<Texture>,
                                        const ExternalImageCopyOptions &) {
    return false;
}

bool CommandEncoder::copyExternalImageToTexture(const ExternalImage &, std::shared_ptr<Texture>,
                                                const ExternalImageCopyOptions &) {
    return false;
}
