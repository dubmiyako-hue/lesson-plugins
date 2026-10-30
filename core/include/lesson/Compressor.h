#pragma once
// The web app's compressor (mix-course src/dsp.ts CompressorCore, run by src/compressor.worklet.ts): peak detection over
// all channels with a 20 ms hold, no knee, Attack and Release smoothing the GR in dB. The parameters are floats, as the
// worklet reads them from Float32Arrays. Threshold and 1/Ratio move with a 5 ms time constant (requirements.md); once they
// have settled, the GR is the web app's formula with the target values, to the bit.
#include <lesson/Smoother.h>
#include <vector>

namespace lesson {

// The ratio that stands for ∞:1 (src/dsp.ts RATIO_INF).
inline constexpr double kRatioInfinite = 1000.0;

// dB the compressor wants to take away at `levelDb` (src/dsp.ts targetGr).
double targetGr(double levelDb, double threshold, double ratio) noexcept;
// One-pole coefficient for a time constant in ms; 0 ms means instant (src/dsp.ts timeCoef).
double timeCoef(double ms, double sampleRate) noexcept;

struct CompSettings {
    float threshold = 0.0f; // dB
    float ratio = 4.0f;     // kRatioInfinite is ∞:1
    float attack = 10.0f;   // ms, 0 = fastest
    float release = 100.0f; // ms, 0 = fastest

    bool operator==(const CompSettings&) const = default;
};

class Compressor {
public:
    static constexpr int kSub = 48;         // samples per sub-block of the hold window
    static constexpr double kHoldMs = 20.0;

    // Allocates the hold ring and starts afresh at `settings`: GR 0, nothing held, no smoothing.
    void prepare(double sampleRate, const CompSettings& settings);
    // The settings to move to. Attack and Release take effect at once (the worklet reads them per block).
    void setTarget(const CompSettings& settings) noexcept;
    // One sample: `peak` is the largest |x| of the channels; returns the gain to multiply them by.
    double gainFor(float peak) noexcept;
    // In place, as CompressorCore.process; returns the largest GR (dB) of the block.
    double process(float* const* channels, int numChannels, int numSamples) noexcept;

    double gr() const noexcept { return gr_; }
    bool settled() const noexcept { return !threshold_.moving() && !inverseRatio_.moving(); }
    float threshold() const noexcept { return threshold_.value(); }
    float inverseRatio() const noexcept { return inverseRatio_.value(); }

private:
    void pushPartial() noexcept;

    double sampleRate_ = 48000.0;
    Smoother threshold_;
    Smoother inverseRatio_; // 0 at ∞:1
    float ratio_ = 4.0f;    // the target, for the settled formula
    double attack_ = 0.0;
    double release_ = 0.0;
    double gr_ = 0.0;
    std::vector<float> ring_ { 0.0f }; // one sub-block until prepare
    size_t ringPos_ = 0;
    float ringMax_ = 0.0f;
    float partial_ = 0.0f;
    int partialCount_ = 0;
};

} // namespace lesson
