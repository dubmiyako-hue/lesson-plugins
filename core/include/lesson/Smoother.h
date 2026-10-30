#pragma once
// AudioParam.setTargetAtTime(target, t, 0.005) as Chrome runs it (Blink's audio_param_timeline.cc): a float value that
// moves by (target - value) * c each sample, with c = float(1 - exp(-1 / (fs * tau))) (fdlibm's exp, as Blink's
// DiscreteTimeConstantForSampleRate), and snaps to the target 10 tau
// after the start or once it is within exp(-10) of it (relative to the value, or absolute when the target is 0).
// Chrome checks the snap once per render quantum and steps 4 samples at a time on x86; this checks it every sample and
// steps one at a time (DECISIONS.md). A stopped value is the target itself, so the sound after a change has settled is
// the same as without smoothing.
#include <lesson/Fdlibm.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lesson {

class Smoother {
public:
    static constexpr double kTimeConstant = 0.005;

    void prepare(double sampleRate) noexcept {
        coefficient_ = static_cast<float>(1.0 - fdlibm::exp(-1.0 / (sampleRate * kTimeConstant)));
        limit_ = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(10.0 * kTimeConstant * sampleRate)));
    }
    // Jumps to the value (no smoothing), as after prepare or a reset.
    void snap(float value) noexcept {
        value_ = target_ = value;
        left_ = 0;
    }
    // A new target starts a new event from the current value; the same target again changes nothing.
    void setTarget(float target) noexcept {
        if (target == target_) return;
        target_ = target;
        left_ = limit_;
    }
    bool moving() const noexcept { return left_ > 0; }
    float value() const noexcept { return value_; }
    float target() const noexcept { return target_; }
    float coefficient() const noexcept { return coefficient_; }

    // The value for this sample, then a step toward the target.
    float next() noexcept {
        const float current = value_;
        if (left_ > 0) {
            value_ += (target_ - value_) * coefficient_;
            if (--left_ == 0 || converged()) {
                value_ = target_;
                left_ = 0;
            }
        }
        return current;
    }

private:
    static constexpr double kThreshold = 4.539992976248485e-05; // exp(-10)
    static constexpr float kZeroThreshold = 4.539992976248485e-05f;

    // Blink's HasSetTargetConverged: the difference in float, compared in double.
    bool converged() const noexcept {
        if (target_ == 0.0f) return std::fabs(value_) < kZeroThreshold;
        return std::fabs(target_ - value_) < kThreshold * std::fabs(value_);
    }

    float coefficient_ = 1.0f;
    std::int64_t limit_ = 1; // before prepare: a new target is reached in one step
    std::int64_t left_ = 0;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace lesson
