#pragma once
// The plug-ins' Auto Gain, computed from the settings (requirements.md "Auto Gain の式"). The comp adds half the GR a
// 0 dBFS sound gets; the EQ cancels the change in K-weighted loudness of pink noise through its curve.
#include <lesson/Eq.h>
#include <lesson/WebBiquad.h>
#include <array>
#include <vector>

namespace lesson {

inline constexpr double kMaxMakeupDb = 24.0;

// min(24, targetGr(0, threshold, ratio) / 2)
double compMakeupDb(float threshold, float ratio) noexcept;

// ITU-R BS.1770 K-weighting at the sample rate (src/dsp.ts kWeighting): the shelf, then the highpass.
std::array<BiquadCoefficients, 2> kWeighting(double sampleRate) noexcept;

// The EQ's Auto Gain: -10 log10(sum |H|^2 |K|^2 / sum |K|^2) over the centres of 2048 equal log steps from 20 Hz to
// 20 kHz, clamped to ±24 dB. H is the target settings' curve (the stages that change the signal). The audio thread
// spreads the sum over the samples, 4 points a sample (design.md 2.4); compute does it at once.
class EqLoudness {
public:
    static constexpr int kPoints = 2048;
    static constexpr int kPointsPerSample = 4;
    static constexpr double kLowHz = 20.0;
    static constexpr double kHighHz = 20000.0;

    // Builds the tables for the sample rate (allocates) and computes `bands` at once.
    void prepare(double sampleRate, const EqBands& bands);
    double compute(const EqBands& bands) const noexcept;

    // Starts computing `bands`; while a computation runs, it is computed after that one.
    void request(const EqBands& bands) noexcept;
    // Computes the next points of the running computation; true when it has just finished.
    bool tick() noexcept;
    bool busy() const noexcept { return busy_; }
    // The last finished result.
    double makeupDb() const noexcept { return result_; }

private:
    void start(const EqBands& bands) noexcept;
    double point(int i) const noexcept;
    static double finish(double weighted, double total) noexcept;

    double sampleRate_ = 48000.0;
    std::vector<double> cosw_, cos2w_, k2_;
    double kSum_ = 1.0;
    std::array<BiquadCoefficients, kEqStages> stages_ {};
    int stageCount_ = 0;
    EqBands pending_ {};
    bool hasPending_ = false;
    bool busy_ = false;
    int next_ = 0;
    double sum_ = 0.0;
    double result_ = 0.0;
};

} // namespace lesson
