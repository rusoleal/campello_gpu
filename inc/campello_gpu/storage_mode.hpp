#pragma once

namespace systems::leal::campello_gpu
{

    enum class StorageMode
    {
        hostVisible,
        devicePrivate,
        deviceTransient
    };
}