#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/storage_mode.hpp>
#include <campello_gpu/texture_usage.hpp>
#include <campello_gpu/pixel_format.hpp>
#include <campello_gpu/feature.hpp>

namespace systems::leal::campello_gpu
{

    class Device
    {
    private:
        void *native;

        Device(void *data);

    public:
        ~Device();
        std::string getName();
        std::set<Feature> getFeatures();

        std::shared_ptr<Texture> createTexture(
            StorageMode storageMode,
            uint32_t width,
            uint32_t height,
            PixelFormat pixelFormat,
            // TextureCoordinateSystem textureCoordinateSystem,
            TextureUsage usageMode);

        std::shared_ptr<Buffer> createBuffer(uint64_t size, StorageMode storageMode);

        static std::shared_ptr<Device> getDefaultDevice();
        static std::vector<std::shared_ptr<Device>> getDevices();

        static std::string getEngineVersion();
    };

}
