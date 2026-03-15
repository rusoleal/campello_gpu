#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief The measurement type for a `QuerySet`.
     *
     * Specified in `QuerySetDescriptor::type` when creating a query set.
     */
    enum class QuerySetType {
        /**
         * @brief Counts the number of samples that pass all per-fragment tests.
         *
         * Useful for conditional rendering and performance analysis.  Results are
         * resolved to a `Buffer` via a resolve command.
         */
        occlusion,

        /**
         * @brief Records the GPU clock value at a specific point in the command stream.
         *
         * Used to measure GPU execution time of passes or individual commands.
         * Requires the `timestamp-query` feature to be available and enabled.
         */
        timestamp
    };

}
