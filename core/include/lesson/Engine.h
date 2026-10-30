#pragma once
// Each plug-in's whole sound, as the web app's chain wires it (src/chain.ts StemChain): the EQ's 13 biquads or the
// compressor, then the Auto Gain makeup and the bypass's wet, summed with the dry input. Every gain is a Web Audio
// GainNode: a float AudioParam moved by setTargetAtTime(v, t, 0.005) and a float product (design.md 2.2, 7).
#include <lesson/AutoGain.h>
#include <lesson/Compressor.h>
#include <lesson/Eq.h>
#include <lesson/Smoother.h>

namespace lesson {

// makeup -> wet, summed with dry. Settled at bypass the output is the input itself; settled at work it is the makeup's
// output (Chrome skips a GainNode at gain 0 when it sums).
class WetDry {
public:
    void prepare(double sampleRate, float makeup, bool bypass) noexcept;
    void setMakeup(float gain) noexcept { makeup_.setTarget(gain); }
    void setBypass(bool bypass) noexcept;
    // Steps the gains by one sample; call once per sample, before mix for each channel.
    void beginSample() noexcept;
    float mix(float dry, float processed) const noexcept;

    float makeup() const noexcept { return makeup_.target(); }
    bool settled() const noexcept { return !makeup_.moving() && !wet_.moving() && !dry_.moving(); }

private:
    enum class Route { Dry, Wet, Both };
    Smoother makeup_;
    Smoother wet_;
    Smoother dry_;
    float makeupGain_ = 1.0f;
    float wetGain_ = 1.0f;
    float dryGain_ = 0.0f;
    Route route_ = Route::Wet;
};

// 10^(db / 20) as the AudioParam holds it.
float gainOfDb(double db) noexcept;

struct EqEngineSettings {
    EqBands bands {};
    bool autoGain = true;
    bool bypass = false;
};

class EqEngine {
public:
    static constexpr int kMaxChannels = Eq::kMaxChannels;

    // Allocates the Auto Gain tables and starts afresh at `settings` (the Auto Gain computed at once).
    void prepare(double sampleRate, const EqEngineSettings& settings);
    // In place. `settings` are this block's; the changes move from its first sample.
    void process(float* const* channels, int numChannels, int numSamples, const EqEngineSettings& settings) noexcept;
    // The Auto Gain computed for the settings, also while the switch is off (the screen shows it).
    double makeupDb() const noexcept { return loudness_.makeupDb(); }

    const Eq& eq() const noexcept { return eq_; }
    const WetDry& wetDry() const noexcept { return wetDry_; }

private:
    void setTargets(const EqEngineSettings& settings) noexcept;

    Eq eq_;
    EqLoudness loudness_;
    WetDry wetDry_;
    EqBands requested_ {};
    bool autoGain_ = true;
};

struct CompEngineSettings {
    CompSettings comp {};
    bool autoGain = true;
    bool bypass = false;
};

class CompEngine {
public:
    static constexpr int kMaxChannels = 2;

    // Allocates the compressor's ring and starts afresh at `settings`.
    void prepare(double sampleRate, const CompEngineSettings& settings);
    void process(float* const* channels, int numChannels, int numSamples, const CompEngineSettings& settings) noexcept;
    // The Auto Gain for the settings, also while the switch is off.
    double makeupDb() const noexcept { return makeupDb_; }
    double gr() const noexcept { return comp_.gr(); }

    const Compressor& compressor() const noexcept { return comp_; }
    const WetDry& wetDry() const noexcept { return wetDry_; }

private:
    void setTargets(const CompEngineSettings& settings) noexcept;

    Compressor comp_;
    WetDry wetDry_;
    double makeupDb_ = 0.0;
};

} // namespace lesson
