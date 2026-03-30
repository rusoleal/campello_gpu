#pragma once

#include <array>
#include <cstdint>

namespace systems::leal::campello_gpu
{

/**
 * @brief Circular-buffer frame-time sampler.
 *
 * Records the wall-clock delta between successive calls to `record()` and
 * exposes them as a fixed-capacity circular buffer for UI overlays and
 * profiling tools.
 *
 * Thread-safety: not thread-safe. Call only from the render thread.
 */
class FrameTimeSampler
{
public:
    static constexpr int kCapacity = 64; ///< Maximum number of stored samples.

    /**
     * @brief Record a new frame timestamp.
     *
     * Computes the delta from the previous call and stores it.
     * The very first call only initialises the baseline; no sample is stored.
     *
     * @param now_ms  Current wall time in milliseconds
     *                (e.g. from `std::chrono::steady_clock`).
     */
    void record(uint64_t now_ms) noexcept
    {
        if (last_ms_ != 0)
        {
            const float delta   = static_cast<float>(now_ms - last_ms_);
            samples_[head_]     = delta;
            head_               = (head_ + 1) % kCapacity;
            if (count_ < kCapacity) ++count_;
        }
        last_ms_ = now_ms;
    }

    /// Number of valid samples stored (0 … kCapacity).
    int count() const noexcept { return count_; }

    /**
     * @brief Return the i-th sample in chronological order.
     *
     * at(0) is the oldest sample, at(count()-1) is the most recent.
     * Behaviour is undefined if i is out of range [0, count()).
     */
    float at(int i) const noexcept
    {
        const int idx = (head_ - count_ + i + kCapacity) % kCapacity;
        return samples_[idx];
    }

    /// Arithmetic mean of all stored samples, or 0 if no samples exist.
    float averageMs() const noexcept
    {
        if (count_ == 0) return 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < count_; ++i)
            sum += at(i);
        return sum / static_cast<float>(count_);
    }

    /// Clear all samples and reset the baseline timestamp.
    void reset() noexcept
    {
        samples_ = {};
        head_    = 0;
        count_   = 0;
        last_ms_ = 0;
    }

private:
    std::array<float, kCapacity> samples_{};
    int      head_    = 0;  ///< Next write slot.
    int      count_   = 0;  ///< Number of valid samples.
    uint64_t last_ms_ = 0;  ///< Timestamp of the previous record() call.
};

} // namespace systems::leal::campello_gpu
