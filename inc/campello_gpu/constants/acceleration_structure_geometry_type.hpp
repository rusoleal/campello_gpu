#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief The kind of geometry stored in a bottom-level acceleration structure.
     *
     * Passed inside `AccelerationStructureGeometryDescriptor` when building a BLAS
     * via `CommandEncoder::buildAccelerationStructure()`.
     */
    enum class AccelerationStructureGeometryType {
        triangles,    ///< Triangle mesh geometry (vertex + optional index buffer).
        boundingBoxes ///< Axis-aligned bounding boxes for procedural / custom primitives.
    };

}
