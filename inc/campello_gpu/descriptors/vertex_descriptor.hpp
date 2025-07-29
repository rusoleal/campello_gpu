#pragma once

#include <memory>
#include <string>
#include <map>
#include <any>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    /**
     * The datatype of the accessor’s components.
     */
    enum class ComponentType
    {
        ctByte = 5120,
        ctUnsignedByte = 5121,
        ctShort = 5122,
        ctUnsignedShort = 5123,
        ctUnsignedInt = 5125,
        ctFloat = 5126,
    };

    /**
     * Specifies if the accessor’s elements are scalars, vectors, or matrices.
     */
    enum class AccessorType
    {
        acScalar,
        acVec2,
        acVec3,
        acVec4,
        acMat2,
        acMat3,
        acMat4
    };

    enum class StepMode {

        instance,
        vertex
    };

    struct VertexAttribute {

        ComponentType componentType;

        AccessorType accessorType;

        uint32_t offset;

        uint32_t shaderLocation;

    };

    struct VertexLayout {

        double arrayStride;

        std::vector<VertexAttribute> attributes;

        StepMode stepMode;

    };

    struct VertexDescriptor {
        
        /**
         * A ShaderModule object containing the shader code that this programmable stage will execute.
         */
        std::shared_ptr<ShaderModule> module;

        /**
         * The name of the function in the module that this stage will use to perform its work.
         */
        std::string entryPoint;

        //std::unordered_map<std::string, std::any> uniforms;

        std::vector<VertexLayout> buffers;
    };

}