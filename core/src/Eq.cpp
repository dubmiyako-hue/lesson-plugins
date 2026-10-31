#include <lesson/Eq.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

namespace lesson {

namespace {
bool isPass(EqBandKind kind) noexcept {
    return kind == EqBandKind::Highpass || kind == EqBandKind::Lowpass;
}
BiquadType typeOf(EqBandKind kind) noexcept {
    switch (kind) {
    case EqBandKind::Highpass: return BiquadType::Highpass;
    case EqBandKind::LowShelf: return BiquadType::LowShelf;
    case EqBandKind::Bell: return BiquadType::Peaking;
    case EqBandKind::HighShelf: return BiquadType::HighShelf;
    case EqBandKind::Lowpass: return BiquadType::Lowpass;
    }
    return BiquadType::Peaking;
}
// The slope a lane runs: 0 when the band is off (EqFilters.apply uses no stage then).
int laneSlope(const EqBandSettings& band) noexcept {
    return band.on ? band.slope : 0;
}
// Blink's nyquist_: 0.5 times the context's sample rate, a float.
double nyquistOf(double sampleRate) noexcept {
    return 0.5 * static_cast<float>(sampleRate);
}
BiquadCoefficients parkedStage(double nyquist) noexcept {
    return webBiquadCoefficients(BiquadType::Peaking, kEqParkedFrequency, 1.0f, 0.0f, nyquist);
}
// The shelves and bells as EqFilters.apply sets them: off is 0 dB; the shelves keep the default Q.
float gainOf(const EqBandSettings& band) noexcept {
    return band.on ? band.gain : 0.0f;
}
float qOf(EqBandKind kind, const EqBandSettings& band) noexcept {
    return kind == EqBandKind::Bell ? band.q : 1.0f;
}
}

float butterworthQDb(int slope, int stage) noexcept {
    // butterworthQs(order) in src/dsp.ts, order = slope / 6, n = order / 2
    const int n = slope / 12;
    const double q = 1 / (2 * std::cos(((2 * stage + 1) * std::numbers::pi) / (4 * n)));
    return static_cast<float>(20 * std::log10(q));
}

void eqCoefficients(const EqBands& bands, double sampleRate, std::span<BiquadCoefficients, kEqStages> out) noexcept {
    const double nyquist = nyquistOf(sampleRate);
    const BiquadCoefficients parked = parkedStage(nyquist);
    int k = 0;
    for (int i = 0; i < kEqBands; ++i) {
        const EqBandKind kind = kEqBandKinds[static_cast<size_t>(i)];
        const EqBandSettings& band = bands[static_cast<size_t>(i)];
        if (isPass(kind)) {
            const int used = laneSlope(band) / 12;
            for (int stage = 0; stage < kEqPassStages; ++stage)
                out[static_cast<size_t>(k++)] = stage < used
                    ? webBiquadCoefficients(typeOf(kind), band.freq, butterworthQDb(band.slope, stage), 0.0f, nyquist)
                    : parked;
        } else {
            out[static_cast<size_t>(k++)] = webBiquadCoefficients(typeOf(kind), band.freq, qOf(kind, band), gainOf(band), nyquist);
        }
    }
}

int eqActiveCoefficients(const EqBands& bands, double sampleRate, std::span<BiquadCoefficients, kEqStages> out) noexcept {
    const double nyquist = nyquistOf(sampleRate);
    int k = 0;
    for (int i = 0; i < kEqBands; ++i) {
        const EqBandKind kind = kEqBandKinds[static_cast<size_t>(i)];
        const EqBandSettings& band = bands[static_cast<size_t>(i)];
        if (!band.on) continue;
        if (isPass(kind)) {
            for (int stage = 0; stage < band.slope / 12; ++stage)
                out[static_cast<size_t>(k++)] =
                    webBiquadCoefficients(typeOf(kind), band.freq, butterworthQDb(band.slope, stage), 0.0f, nyquist);
        } else if (band.gain != 0.0f) {
            out[static_cast<size_t>(k++)] = webBiquadCoefficients(typeOf(kind), band.freq, qOf(kind, band), band.gain, nyquist);
        }
    }
    return k;
}

namespace {
// |H| of a biquad at `w` radians per sample
double magnitudeAt(const BiquadCoefficients& c, double w) noexcept {
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return std::abs(c.b0 + c.b1 * z1 + c.b2 * z2) / std::abs(1.0 + c.a1 * z1 + c.a2 * z2);
}
}

double eqTailSeconds(const EqBands& bands, double sampleRate) noexcept {
    std::array<BiquadCoefficients, kEqStages> stages;
    const int count = eqActiveCoefficients(bands, sampleRate, stages);
    double seconds = 0;
    for (int k = 0; k < count; ++k) {
        const BiquadCoefficients& c = stages[static_cast<size_t>(k)];
        // the poles: a complex pair has the radius sqrt(a2) at the angle acos(-a1 / (2 sqrt(a2))); two real ones, the
        // larger root's magnitude
        const double d = c.a1 * c.a1 - 4 * c.a2;
        const double radius = d < 0 ? std::sqrt(c.a2) : (std::fabs(c.a1) + std::sqrt(d)) / 2;
        if (!(radius < 1)) return std::numeric_limits<double>::infinity();
        const double angle = d < 0 ? std::acos(std::clamp(-c.a1 / (2 * std::sqrt(c.a2)), -1.0, 1.0)) : 0.0;
        // the stage's largest gain: at DC or at Nyquist (the shelves), or at the poles' angle (a bell, a resonant HP or
        // LP stage)
        double peak = 1;
        for (const double w : { 0.0, angle, std::numbers::pi }) peak = std::max(peak, magnitudeAt(c, w));
        const double dbPerSecond = -20 * std::log10(radius) * sampleRate;
        seconds += kEqTailMargin * (kEqTailDb + 20 * std::log10(peak)) / dbPerSecond;
    }
    return seconds;
}

void Eq::prepare(double sampleRate, const EqBands& bands) noexcept {
    nyquist_ = nyquistOf(sampleRate);
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kFadeSeconds * sampleRate)));
    parked_ = parkedStage(nyquist_);
    size_t p = 0;
    size_t q = 0;
    for (int i = 0; i < kEqBands; ++i) {
        const EqBandKind kind = kEqBandKinds[static_cast<size_t>(i)];
        const EqBandSettings& settings = bands[static_cast<size_t>(i)];
        if (isPass(kind)) {
            PassBand& band = pass_[p++];
            band.type = typeOf(kind);
            band.freq.prepare(sampleRate);
            band.freq.snap(settings.freq);
            band.refresh = false;
            band.active = 0;
            band.targetSlope = laneSlope(settings);
            band.fadePos = 0;
            band.weight = 1.0;
            for (Lane& lane : band.lanes) setLane(lane, band.type, band.targetSlope, settings.freq);
        } else {
            ParamBand& band = param_[q++];
            band.type = typeOf(kind);
            for (Smoother* s : { &band.freq, &band.gain, &band.q }) s->prepare(sampleRate);
            band.freq.snap(settings.freq);
            band.gain.snap(gainOf(settings));
            band.q.snap(qOf(kind, settings));
            band.refresh = false;
            band.c = webBiquadCoefficients(band.type, band.freq.value(), band.q.value(), band.gain.value(), nyquist_);
            for (BiquadState& s : band.s) s.reset();
        }
    }
}

void Eq::setTarget(const EqBands& bands) noexcept {
    size_t p = 0;
    size_t q = 0;
    for (int i = 0; i < kEqBands; ++i) {
        const EqBandKind kind = kEqBandKinds[static_cast<size_t>(i)];
        const EqBandSettings& settings = bands[static_cast<size_t>(i)];
        if (isPass(kind)) {
            PassBand& band = pass_[p++];
            band.freq.setTarget(settings.freq);
            band.targetSlope = laneSlope(settings);
        } else {
            ParamBand& band = param_[q++];
            band.freq.setTarget(settings.freq);
            band.gain.setTarget(gainOf(settings));
            band.q.setTarget(qOf(kind, settings));
        }
    }
}

void Eq::setLane(Lane& lane, BiquadType type, int slope, float freq) noexcept {
    lane.slope = slope;
    for (int stage = 0; stage < kEqPassStages; ++stage) {
        lane.c[static_cast<size_t>(stage)] = parked_;
        lane.qDb[static_cast<size_t>(stage)] = stage < slope / 12 ? butterworthQDb(slope, stage) : 0.0f;
    }
    updateLane(lane, type, freq);
    for (auto& channel : lane.s)
        for (BiquadState& s : channel) s.reset();
}

void Eq::updateLane(Lane& lane, BiquadType type, float freq) noexcept {
    for (int stage = 0; stage < lane.slope / 12; ++stage)
        lane.c[static_cast<size_t>(stage)] =
            webBiquadCoefficients(type, freq, lane.qDb[static_cast<size_t>(stage)], 0.0f, nyquist_);
}

void Eq::beginPass(PassBand& band) noexcept {
    // A fade that reached weight 1 ends; a change waits for the running fade to end.
    if (band.fadePos == fadeLength_ && band.fadePos > 0) {
        band.active = 1 - band.active;
        band.fadePos = 0;
    }
    if (band.fadePos == 0 && band.targetSlope != band.lanes[static_cast<size_t>(band.active)].slope)
        setLane(band.lanes[static_cast<size_t>(1 - band.active)], band.type, band.targetSlope, band.freq.value());
    const bool fading = band.fadePos > 0 || band.targetSlope != band.lanes[static_cast<size_t>(band.active)].slope;
    if (fading) {
        ++band.fadePos;
        band.weight = 0.5 - 0.5 * std::cos(std::numbers::pi * band.fadePos / fadeLength_);
    } else {
        band.weight = 1.0;
    }
    const bool moving = band.freq.moving();
    if (moving || band.refresh) {
        const float freq = band.freq.next();
        updateLane(band.lanes[static_cast<size_t>(band.active)], band.type, freq);
        if (fading) updateLane(band.lanes[static_cast<size_t>(1 - band.active)], band.type, freq);
        band.refresh = moving;
    }
}

void Eq::beginSample() noexcept {
    for (PassBand& band : pass_) beginPass(band);
    for (ParamBand& band : param_) {
        const bool moving = band.freq.moving() || band.gain.moving() || band.q.moving();
        if (!moving && !band.refresh) continue;
        const float freq = band.freq.next();
        const float gain = band.gain.next();
        const float q = band.q.next();
        band.c = webBiquadCoefficients(band.type, freq, q, gain, nyquist_);
        band.refresh = moving;
    }
}

float Eq::processPass(PassBand& band, int channel, float x) noexcept {
    const auto run = [channel](Lane& lane, float v) noexcept {
        auto& states = lane.s[static_cast<size_t>(channel)];
        for (size_t stage = 0; stage < kEqPassStages; ++stage) v = states[stage].process(lane.c[stage], v);
        return v;
    };
    Lane& active = band.lanes[static_cast<size_t>(band.active)];
    if (band.fadePos == 0) return run(active, x);
    const float from = run(active, x);
    const float to = run(band.lanes[static_cast<size_t>(1 - band.active)], x);
    return static_cast<float>((1.0 - band.weight) * from + band.weight * to);
}

float Eq::process(int channel, float x) noexcept {
    x = processPass(pass_[0], channel, x);
    for (ParamBand& band : param_) x = band.s[static_cast<size_t>(channel)].process(band.c, x);
    return processPass(pass_[1], channel, x);
}

void Eq::process(float* const* channels, int numChannels, int numSamples) noexcept {
    numChannels = std::min(numChannels, kMaxChannels);
    for (int i = 0; i < numSamples; ++i) {
        beginSample();
        for (int c = 0; c < numChannels; ++c) channels[c][i] = process(c, channels[c][i]);
    }
    recoverFromNonFinite();
}

bool Eq::recoverFromNonFinite() noexcept {
    bool finite = true;
    for (const PassBand& band : pass_)
        for (const Lane& lane : band.lanes)
            for (const auto& channel : lane.s)
                for (const BiquadState& s : channel) finite = finite && s.finite();
    for (const ParamBand& band : param_)
        for (const BiquadState& s : band.s) finite = finite && s.finite();
    if (finite) return false;
    for (PassBand& band : pass_)
        for (Lane& lane : band.lanes)
            for (auto& channel : lane.s)
                for (BiquadState& s : channel) s.reset();
    for (ParamBand& band : param_)
        for (BiquadState& s : band.s) s.reset();
    return true;
}

bool Eq::settled() const noexcept {
    for (const PassBand& band : pass_)
        if (band.freq.moving() || band.refresh || band.fadePos > 0 ||
            band.targetSlope != band.lanes[static_cast<size_t>(band.active)].slope)
            return false;
    for (const ParamBand& band : param_)
        if (band.freq.moving() || band.gain.moving() || band.q.moving() || band.refresh) return false;
    return true;
}

double Eq::fadeWeight(int pass) const noexcept {
    return pass_[static_cast<size_t>(pass)].weight;
}

} // namespace lesson
