#include <campello_gpu/surface.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/command_buffer.hpp>

using namespace systems::leal::campello_gpu;

// Placeholder until per-surface swapchains land for this backend
// (campello_widgets TODO.md, Phase 17 M0.2). Device::createSurface() returns
// nullptr, so callers fall back to the device's single implicit swapchain
// (createDefaultDevice(pd) / getSwapchainTextureView()). No Surface is ever
// constructed here, so the member functions below are unreachable, but they
// keep the public API linkable on every backend.

Surface::Surface(void *pd) : native(pd) {}
Surface::~Surface() = default;

std::shared_ptr<Surface> Device::createSurface(void *, uint32_t, uint32_t) {
    return nullptr;
}

std::shared_ptr<TextureView> Surface::acquireNextTexture() { return nullptr; }
bool Surface::schedulePresent(const std::shared_ptr<CommandBuffer> &) { return false; }
void Surface::resize(uint32_t, uint32_t) {}
uint32_t Surface::getWidth() const noexcept { return 0; }
uint32_t Surface::getHeight() const noexcept { return 0; }
PixelFormat Surface::getPixelFormat() const noexcept { return PixelFormat::invalid; }
