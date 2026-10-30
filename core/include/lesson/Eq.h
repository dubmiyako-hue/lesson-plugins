#pragma once
// The web app's EQ (mix-course src/chain.ts EqFilters): 13 Web Audio biquads in series, HP 4 stages, low shelf, three
// bells, high shelf, LP 4 stages. Settled, every stage is the BiquadFilterNode that EqFilters.apply(bands, false) makes
// on a fresh chain, so the output is Chrome's to the bit. Changes move as the web app's setTargetAtTime(v, t, 0.005);
// HP and LP On and Slope cross-fade between two lanes over 10 ms (design.md 2.2).
#include <lesson/Smoother.h>
#include <lesson/WebBiquad.h>
#include <array>
#include <span>

namespace lesson {

enum class EqBandKind { Highpass, LowShelf, Bell, HighShelf, Lowpass };

inline constexpr int kEqBands = 7;
inline constexpr int kEqPassStages = 4; // biquads per HP or LP, enough for 48 dB/oct
inline constexpr int kEqStages = 2 * kEqPassStages + 5;
inline constexpr std::array<EqBandKind, kEqBands> kEqBandKinds { EqBandKind::Highpass, EqBandKind::LowShelf,
    EqBandKind::Bell, EqBandKind::Bell, EqBandKind::Bell, EqBandKind::HighShelf, EqBandKind::Lowpass };
// A BiquadFilterNode's default frequency: the unused HP and LP stages are 0 dB peaking filters left there.
inline constexpr float kEqParkedFrequency = 350.0f;

struct EqBandSettings {
    bool on = false;
    float freq = 1000.0f; // Hz
    float gain = 0.0f;    // dB; shelves and bells
    float q = 1.0f;       // bells
    int slope = 12;       // dB/oct; HP and LP

    bool operator==(const EqBandSettings&) const = default;
};
using EqBands = std::array<EqBandSettings, kEqBands>;

// The Q AudioParam (dB) of HP or LP stage `stage` (< slope / 12): float(20 log10 q) of the web app's butterworthQs.
float butterworthQDb(int slope, int stage) noexcept;

// The 13 stages' coefficients as EqFilters.apply(bands, false) sets them on a fresh chain.
void eqCoefficients(const EqBands& bands, double sampleRate, std::span<BiquadCoefficients, kEqStages> out) noexcept;
// Only the stages that change the signal (EqFilters.active); returns how many it wrote.
int eqActiveCoefficients(const EqBands& bands, double sampleRate, std::span<BiquadCoefficients, kEqStages> out) noexcept;

class Eq {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr double kFadeSeconds = 0.01;

    // Starts afresh at `bands`: no smoothing, no fade, the filters' state 0.
    void prepare(double sampleRate, const EqBands& bands) noexcept;
    // The settings to move to, from the next sample on.
    void setTarget(const EqBands& bands) noexcept;
    // Advances the smoothing and the fades by one sample; call once per sample, before process for each channel.
    void beginSample() noexcept;
    float process(int channel, float x) noexcept;
    // beginSample and process for a whole block, in place.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    bool settled() const noexcept;
    // The weight of the lane being faded in for HP (0) or LP (1); 1 when no fade runs.
    double fadeWeight(int pass) const noexcept;

private:
    struct Lane {
        int slope = 0; // 0 is off: all four stages pass the signal
        std::array<BiquadCoefficients, kEqPassStages> c {};
        std::array<std::array<BiquadState, kEqPassStages>, kMaxChannels> s {};
    };
    struct PassBand {
        BiquadType type = BiquadType::Highpass;
        Smoother freq;
        bool refresh = false;
        std::array<Lane, 2> lanes {};
        int active = 0;
        int targetSlope = 0;
        int fadePos = 0; // 1..fade while fading into lanes[1 - active]; 0 when no fade runs
        double weight = 1.0;
    };
    struct ParamBand {
        BiquadType type = BiquadType::Peaking;
        Smoother freq;
        Smoother gain;
        Smoother q;
        bool refresh = false;
        BiquadCoefficients c {};
        std::array<BiquadState, kMaxChannels> s {};
    };

    void setLane(Lane& lane, BiquadType type, int slope, float freq) noexcept;
    void updateLane(Lane& lane, BiquadType type, float freq) noexcept;
    void beginPass(PassBand& band) noexcept;
    float processPass(PassBand& band, int channel, float x) noexcept;

    double nyquist_ = 24000.0;
    int fadeLength_ = 480;
    BiquadCoefficients parked_ {};
    std::array<PassBand, 2> pass_ {};   // HP, LP
    std::array<ParamBand, 5> param_ {}; // LS, bells, HS
};

} // namespace lesson
