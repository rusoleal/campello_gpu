#include <vulkan/vulkan.h>
#include <campello_gpu/acceleration_structure.hpp>
#include "acceleration_structure_handle.hpp"

using namespace systems::leal::campello_gpu;

// Defined in device.cpp (loaded by loadRayTracingFunctions).
extern PFN_vkDestroyAccelerationStructureKHR pfnDestroyAccelerationStructureKHR;

AccelerationStructure::AccelerationStructure(void *data) : native(data) {}

AccelerationStructure::~AccelerationStructure() {
    if (!native) return;
    auto *h = (AccelerationStructureHandle *)native;
    if (pfnDestroyAccelerationStructureKHR && h->accelerationStructure != VK_NULL_HANDLE)
        pfnDestroyAccelerationStructureKHR(h->device, h->accelerationStructure, nullptr);
    if (h->compactedSizeQuery != VK_NULL_HANDLE)
        vkDestroyQueryPool(h->device, h->compactedSizeQuery, nullptr);
    if (h->memory != VK_NULL_HANDLE)
        vkFreeMemory(h->device, h->memory, nullptr);
    if (h->buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(h->device, h->buffer, nullptr);
    delete h;
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    return native ? ((AccelerationStructureHandle *)native)->buildScratchSize : 0;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    return native ? ((AccelerationStructureHandle *)native)->updateScratchSize : 0;
}

uint64_t AccelerationStructure::getCompactedSize() const {
    auto *h = (AccelerationStructureHandle *)native;
    if (!h || h->compactedSizeQuery == VK_NULL_HANDLE || !h->compactedSizeRecorded) return 0;
    // No WAIT flag: VK_NOT_READY until the build's command buffer has finished.
    uint64_t size = 0;
    VkResult r = vkGetQueryPoolResults(h->device, h->compactedSizeQuery, 0, 1, sizeof(size), &size,
                                       sizeof(size), VK_QUERY_RESULT_64_BIT);
    return r == VK_SUCCESS ? size : 0;
}
