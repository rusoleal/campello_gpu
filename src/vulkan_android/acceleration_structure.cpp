#include <android/log.h>
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
    if (h->memory != VK_NULL_HANDLE)
        vkFreeMemory(h->device, h->memory, nullptr);
    if (h->buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(h->device, h->buffer, nullptr);
    delete h;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "AccelerationStructure::~AccelerationStructure()");
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    return native ? ((AccelerationStructureHandle *)native)->buildScratchSize : 0;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    return native ? ((AccelerationStructureHandle *)native)->updateScratchSize : 0;
}
