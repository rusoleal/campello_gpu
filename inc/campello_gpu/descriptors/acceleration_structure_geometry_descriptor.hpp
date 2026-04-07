#pragma once

#include <memory>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Describes a single geometry within a bottom-level acceleration structure.
     *
     * Set `type` to select the geometry kind, then populate the matching fields.
     * For `triangles`, fill the vertex/index/transform fields. For `boundingBoxes`,
     * fill the AABB buffer fields. Fields belonging to the inactive geometry kind
     * are ignored by the backend.
     *
     * **Triangle geometry example:**
     * @code
     * AccelerationStructureGeometryDescriptor geo{};
     * geo.type          = AccelerationStructureGeometryType::triangles;
     * geo.opaque        = true;
     * geo.vertexBuffer  = positionBuffer;
     * geo.vertexOffset  = 0;
     * geo.vertexStride  = sizeof(float) * 3;
     * geo.vertexCount   = 3;
     * geo.componentType = ComponentType::ctFloat; // position components are float
     * @endcode
     *
     * **Bounding-box geometry example:**
     * @code
     * AccelerationStructureGeometryDescriptor geo{};
     * geo.type       = AccelerationStructureGeometryType::boundingBoxes;
     * geo.opaque     = false;
     * geo.aabbBuffer = aabbBuffer;  // packed float[6]: minX,minY,minZ,maxX,maxY,maxZ per entry
     * geo.aabbStride = sizeof(float) * 6;
     * geo.aabbCount  = numPrimitives;
     * @endcode
     */
    struct AccelerationStructureGeometryDescriptor {

        // ------------------------------------------------------------------
        // Common fields
        // ------------------------------------------------------------------

        /** @brief Whether this geometry is triangles or bounding boxes. */
        AccelerationStructureGeometryType type = AccelerationStructureGeometryType::triangles;

        /**
         * @brief When true, any-hit shaders are never invoked for this geometry.
         *
         * Marking geometry opaque allows the hardware to skip any-hit testing, which
         * can significantly improve traversal performance for fully solid objects.
         */
        bool opaque = true;

        // ------------------------------------------------------------------
        // Triangle geometry fields (active when type == triangles)
        // ------------------------------------------------------------------

        /**
         * @brief Buffer holding per-vertex position data.
         *
         * The GPU reads 3-component positions at `vertexOffset + vertexIndex * vertexStride`.
         * Only the first `sizeof(componentType) * 3` bytes at each step are used as
         * the XYZ position; remaining bytes in the stride are ignored.
         */
        std::shared_ptr<Buffer> vertexBuffer;

        /** @brief Byte offset within `vertexBuffer` to the first vertex position. */
        uint64_t vertexOffset = 0;

        /**
         * @brief Byte stride between consecutive vertex positions in `vertexBuffer`.
         *
         * Set to `sizeof(componentType) * 3` for tightly-packed XYZ positions.
         * May be larger when positions are interleaved with other per-vertex data.
         */
        uint64_t vertexStride = 0;

        /** @brief Total number of vertices (not triangles) in `vertexBuffer`. */
        uint32_t vertexCount = 0;

        /**
         * @brief Scalar type of each position component.
         *
         * Position components are always interpreted as 3-component (XYZ).
         * Use `ComponentType::ctFloat` for standard 32-bit float positions.
         */
        ComponentType componentType = ComponentType::ctFloat;

        /**
         * @brief Optional index buffer. Set to `nullptr` for non-indexed geometry.
         *
         * When non-null, the GPU reads triangle indices from this buffer and uses
         * them to look up vertices in `vertexBuffer`.
         */
        std::shared_ptr<Buffer> indexBuffer;

        /** @brief Width of each index value. Ignored when `indexBuffer` is null. */
        IndexFormat indexFormat = IndexFormat::uint32;

        /**
         * @brief Number of indices in `indexBuffer` (i.e. triangleCount × 3).
         *
         * Set to 0 for non-indexed geometry.
         */
        uint32_t indexCount = 0;

        /**
         * @brief Optional per-geometry transform buffer.
         *
         * When non-null, the GPU reads a packed 3×4 row-major float matrix starting
         * at `transformOffset` and applies it to each vertex position before building
         * the BVH. Set to `nullptr` to use the identity transform.
         */
        std::shared_ptr<Buffer> transformBuffer;

        /** @brief Byte offset within `transformBuffer` to the 3×4 float matrix. */
        uint64_t transformOffset = 0;

        // ------------------------------------------------------------------
        // Bounding-box geometry fields (active when type == boundingBoxes)
        // ------------------------------------------------------------------

        /**
         * @brief Buffer containing axis-aligned bounding boxes.
         *
         * Each AABB is six packed floats: `minX, minY, minZ, maxX, maxY, maxZ`.
         */
        std::shared_ptr<Buffer> aabbBuffer;

        /** @brief Byte offset within `aabbBuffer` to the first AABB. */
        uint64_t aabbOffset = 0;

        /**
         * @brief Byte stride between consecutive AABBs.
         *
         * Set to `sizeof(float) * 6` for tightly-packed AABBs.
         */
        uint64_t aabbStride = 0;

        /** @brief Number of AABBs (and therefore procedural primitives) in `aabbBuffer`. */
        uint32_t aabbCount = 0;
    };

}
