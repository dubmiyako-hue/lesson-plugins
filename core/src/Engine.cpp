#include <lesson/Engine.h>
#include <algorithm>
#include <cmath>

namespace lesson {

namespace {
// What goes out for a sample that is not a number (an Inf or a NaN in the input, or a filter that overflowed): silence.
// A finite sample is left as it is, so the sound of finite input does not change.
float finiteOr0(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }
}

float gainOfDb(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20));
}

void WetDry::prepare(double sampleRate, float makeup, bool bypass) noexcept {
    for (Smoother* s : { &makeup_, &wet_, &dry_ }) s->prepare(sampleRate);
    makeup_.snap(makeup);
    wet_.snap(bypass ? 0.0f : 1.0f);
    dry_.snap(bypass ? 1.0f : 0.0f);
    beginSample();
}

void WetDry::setBypass(bool bypass) noexcept {
    wet_.setTarget(bypass ? 0.0f : 1.0f);
    dry_.setTarget(bypass ? 1.0f : 0.0f);
}

void WetDry::beginSample() noexcept {
    const bool moving = wet_.moving() || dry_.moving();
    makeupGain_ = makeup_.next();
    wetGain_ = wet_.next();
    dryGain_ = dry_.next();
    if (moving) route_ = Route::Both;
    else route_ = wetGain_ == 0.0f && dryGain_ == 1.0f ? Route::Dry : wetGain_ == 1.0f && dryGain_ == 0.0f ? Route::Wet : Route::Both;
}

float WetDry::mix(float dry, float processed) const noexcept {
    const float made = processed * makeupGain_;
    switch (route_) {
    case Route::Dry: return dry;
    case Route::Wet: return made;
    case Route::Both: break;
    }
    return made * wetGain_ + dry * dryGain_;
}

void EqEngine::prepare(double sampleRate, const EqEngineSettings& settings) {
    sampleRate_ = sampleRate;
    loudness_.prepare(sampleRate, settings.bands);
    reset(settings);
}

void EqEngine::reset(const EqEngineSettings& settings) noexcept {
    eq_.prepare(sampleRate_, settings.bands);
    loudness_.restart(settings.bands);
    requested_ = settings.bands;
    autoGain_ = settings.autoGain;
    wetDry_.prepare(sampleRate_, autoGain_ ? gainOfDb(loudness_.makeupDb()) : 1.0f, settings.bypass);
}

void EqEngine::setTargets(const EqEngineSettings& settings) noexcept {
    eq_.setTarget(settings.bands);
    if (settings.bands != requested_) {
        requested_ = settings.bands;
        loudness_.request(settings.bands);
    }
    autoGain_ = settings.autoGain;
    wetDry_.setMakeup(autoGain_ ? gainOfDb(loudness_.makeupDb()) : 1.0f);
    wetDry_.setBypass(settings.bypass);
}

void EqEngine::process(float* const* channels, int numChannels, int numSamples, const EqEngineSettings& settings,
                       const EqTaps* taps) noexcept {
    numChannels = std::min(numChannels, kMaxChannels);
    setTargets(settings);
    for (int i = 0; i < numSamples; ++i) {
        if (loudness_.tick() && autoGain_) wetDry_.setMakeup(gainOfDb(loudness_.makeupDb()));
        eq_.beginSample();
        wetDry_.beginSample();
        float in[kMaxChannels] {};
        float biquads[kMaxChannels] {};
        for (int c = 0; c < numChannels; ++c) {
            const float x = finiteOr0(channels[c][i]); // an Inf or a NaN does not go into the filters' state
            const float y = eq_.process(c, x);
            in[c] = x;
            biquads[c] = y;
            channels[c][i] = finiteOr0(wetDry_.mix(x, y));
        }
        if (taps != nullptr && numChannels > 0) {
            // Web Audio's down-mix to mono: the channel itself, or 0.5 (L + R) in float.
            if (taps->pre != nullptr) taps->pre->push(numChannels == 1 ? in[0] : 0.5f * (in[0] + in[1]));
            if (taps->post != nullptr) taps->post->push(numChannels == 1 ? biquads[0] : 0.5f * (biquads[0] + biquads[1]));
        }
    }
    eq_.recoverFromNonFinite();
}

void CompEngine::prepare(double sampleRate, const CompEngineSettings& settings) {
    sampleRate_ = sampleRate;
    comp_.prepare(sampleRate, settings.comp);
    reset(settings);
}

void CompEngine::reset(const CompEngineSettings& settings) noexcept {
    comp_.reset(settings.comp);
    makeupDb_ = compMakeupDb(settings.comp.threshold, settings.comp.ratio);
    wetDry_.prepare(sampleRate_, settings.autoGain ? gainOfDb(makeupDb_) : 1.0f, settings.bypass);
}

void CompEngine::setTargets(const CompEngineSettings& settings) noexcept {
    comp_.setTarget(settings.comp);
    makeupDb_ = compMakeupDb(settings.comp.threshold, settings.comp.ratio);
    wetDry_.setMakeup(settings.autoGain ? gainOfDb(makeupDb_) : 1.0f);
    wetDry_.setBypass(settings.bypass);
}

void CompEngine::process(float* const* channels, int numChannels, int numSamples, const CompEngineSettings& settings,
                         CompPoints* points) noexcept {
    numChannels = std::min(numChannels, kMaxChannels);
    setTargets(settings);
    for (int i = 0; i < numSamples; ++i) {
        float peak = 0;
        for (int c = 0; c < numChannels; ++c) {
            const float a = std::fabs(channels[c][i]);
            if (a > peak) peak = a;
        }
        const double g = comp_.gainFor(peak);
        // The output before the makeup is float(x * g) per channel; rounding is monotonic, so its largest is float(peak * g).
        if (points != nullptr) points->add(peak, static_cast<float>(peak * g), comp_.gr());
        wetDry_.beginSample();
        for (int c = 0; c < numChannels; ++c) {
            const float x = channels[c][i];
            channels[c][i] = finiteOr0(wetDry_.mix(x, static_cast<float>(x * g)));
        }
    }
}

} // namespace lesson
