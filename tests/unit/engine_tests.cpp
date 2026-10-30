// AC-09: the output does not depend on the block length. AC-12: bypassed, the original comes out unchanged. AC-15: extreme
// inputs and settings do not break the output. AC-10 (one item): Auto Gain off is a makeup of 0 dB. All of them run the
// whole engines (design.md 2.2 to 2.5 and 7) from the public API only. Every run is inside lesson::NoDenormals, as the
// plug-ins process.
#include <doctest.h>
#include <lesson/Engine.h>
#include <lesson/Eq.h>
#include <lesson/NoDenormals.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace lesson;

namespace {
using Channels = std::vector<std::vector<float>>;

// xorshift32: the same numbers on every run and every compiler.
class Rng {
public:
    explicit Rng(std::uint32_t seed) : state_(seed == 0 ? 1u : seed) {}
    std::uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    float uniform() { return static_cast<float>(next() >> 8) / 8388608.0f - 1.0f; } // [-1, 1)
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }

private:
    std::uint32_t state_;
};

size_t framesOf(double fs, double seconds) { return static_cast<size_t>(std::lround(seconds * fs)); }

// Noise whose level steps between `loud` and `quiet` every 0.4 s (the compressor's release has something to do), a
// different sequence in every channel.
Channels noiseSignal(int channels, double fs, double seconds, float loud, float quiet, std::uint32_t seed) {
    Channels x(static_cast<size_t>(channels), std::vector<float>(framesOf(fs, seconds)));
    const size_t segment = framesOf(fs, 0.4);
    for (size_t c = 0; c < x.size(); ++c) {
        Rng rng(seed + 7919u * static_cast<std::uint32_t>(c));
        for (size_t i = 0; i < x[c].size(); ++i) x[c][i] = rng.uniform() * (((i / segment) % 2 == 0) ? loud : quiet);
    }
    return x;
}

// 1 kHz in the first channel, 1.5 kHz in the second.
Channels sineSignal(int channels, double fs, double seconds, double amp) {
    Channels x(static_cast<size_t>(channels), std::vector<float>(framesOf(fs, seconds)));
    for (size_t c = 0; c < x.size(); ++c) {
        const double freq = 1000.0 + 500.0 * static_cast<double>(c);
        for (size_t i = 0; i < x[c].size(); ++i)
            x[c][i] = static_cast<float>(amp * std::sin(2 * std::numbers::pi * freq * static_cast<double>(i) / fs));
    }
    return x;
}

// `lengths` is cycled; the last block is cut to the frames that are left. The settings of every block are `settingsAt`
// (the first sample of the block).
template <class Engine, class Settings, class SettingsAt>
Channels run(double fs, const Settings& prepared, Channels x, const std::vector<int>& lengths, SettingsAt settingsAt) {
    Engine engine;
    engine.prepare(fs, prepared);
    std::vector<float*> pointers;
    for (auto& channel : x) pointers.push_back(channel.data());
    const size_t frames = x[0].size();
    size_t pos = 0;
    size_t turn = 0;
    while (pos < frames) {
        const size_t n = std::min(static_cast<size_t>(lengths[turn % lengths.size()]), frames - pos);
        ++turn;
        std::vector<float*> at;
        for (float* p : pointers) at.push_back(p + pos);
        engine.process(at.data(), static_cast<int>(at.size()), static_cast<int>(n), settingsAt(pos));
        pos += n;
    }
    return x;
}

template <class Engine, class Settings>
Channels run(double fs, const Settings& settings, Channels x, const std::vector<int>& lengths) {
    return run<Engine>(fs, settings, std::move(x), lengths, [&](size_t) -> const Settings& { return settings; });
}

struct Mismatch {
    bool found = false;
    int channel = 0;
    size_t index = 0;
    float a = 0;
    float b = 0;
};

// The first sample in [from, to) of any channel where `a == b` does not hold (a NaN is a mismatch).
Mismatch firstMismatch(const Channels& a, const Channels& b, size_t from = 0, size_t to = static_cast<size_t>(-1)) {
    Mismatch m;
    if (a.size() != b.size()) {
        m.found = true;
        return m;
    }
    for (size_t i = from; !m.found; ++i) {
        bool inRange = false;
        for (size_t c = 0; c < a.size() && !m.found; ++c) {
            if (i >= a[c].size() || i >= b[c].size() || i >= to) continue;
            inRange = true;
            if (!(a[c][i] == b[c][i])) {
                m.found = true;
                m.channel = static_cast<int>(c);
                m.index = i;
                m.a = a[c][i];
                m.b = b[c][i];
            }
        }
        if (!inRange) break;
    }
    return m;
}

// The first sample that is NaN or Inf.
Mismatch firstNonFinite(const Channels& x) {
    Mismatch m;
    for (size_t c = 0; c < x.size(); ++c)
        for (size_t i = 0; i < x[c].size(); ++i)
            if (!std::isfinite(x[c][i])) {
                m.found = true;
                m.channel = static_cast<int>(c);
                m.index = i;
                m.a = x[c][i];
                return m;
            }
    return m;
}

std::string where(const Mismatch& m) {
    std::ostringstream s;
    s.precision(9);
    s << "channel " << m.channel << " sample " << m.index << " measured " << m.a << " expected " << m.b;
    return s.str();
}

std::string label(const std::string& what, double fs, int channels, const std::string& more) {
    std::ostringstream s;
    s << what << " " << static_cast<int>(fs) << " Hz " << channels << " ch " << more;
    return s.str();
}

constexpr std::array<double, 3> kRates { 44100.0, 48000.0, 96000.0 };

EqBands bands(const EqBandSettings& hp, const EqBandSettings& ls, const EqBandSettings& b1, const EqBandSettings& b2,
    const EqBandSettings& b3, const EqBandSettings& hs, const EqBandSettings& lp) {
    return { hp, ls, b1, b2, b3, hs, lp };
}

// A band that is on: freq (Hz), gain (dB), q, slope (dB/oct).
EqBandSettings on(float freq, float gain, float q, int slope) { return { true, freq, gain, q, slope }; }

struct NamedEq {
    const char* name;
    EqEngineSettings settings;
};

struct NamedComp {
    const char* name;
    CompEngineSettings settings;
};

CompEngineSettings comp(float threshold, float ratio, float attack, float release, bool autoGain = true, bool bypass = false) {
    CompEngineSettings s;
    s.comp = { threshold, ratio, attack, release };
    s.autoGain = autoGain;
    s.bypass = bypass;
    return s;
}

EqEngineSettings eqSettings(const EqBands& b, bool autoGain = true, bool bypass = false) {
    EqEngineSettings s;
    s.bands = b;
    s.autoGain = autoGain;
    s.bypass = bypass;
    return s;
}

// Settings that change the sound in every band.
const EqBands kEqBusy = bands(on(80, 0, 1, 24), on(120, 4, 1, 12), on(300, -5, 2, 12), on(1500, 6, 0.7f, 12), on(6000, -3, 4, 12),
    on(8000, 3, 1, 12), on(12000, 0, 1, 36));
// Steep passes, the widest and narrowest bells, large gains.
const EqBands kEqSteep = bands(on(30, 0, 1, 48), on(200, -12, 1, 12), on(100, 12, 12, 12), on(2500, -18, 0.3f, 12),
    on(9000, 18, 12, 12), on(5000, -9, 1, 12), on(18000, 0, 1, 48));

const std::vector<int> kFixedBlocks { 1, 64, 128, 480, 4096 };

// Block lengths drawn from 1..4096 until they cover `frames`, cycled by run.
std::vector<int> randomBlocks(size_t frames) {
    Rng rng(20261031u);
    std::vector<int> lengths;
    size_t sum = 0;
    while (sum < frames) {
        lengths.push_back(rng.range(1, 4096));
        sum += static_cast<size_t>(lengths.back());
    }
    return lengths;
}

// AC-09 for one engine, one setting and one input: the block lengths 1 64 128 480 4096 and a random mix all give the
// output of the block of 4096, to the bit.
template <class Engine, class Settings>
void checkBlockLengths(const std::string& what, double fs, const Settings& settings, const Channels& input) {
    const Channels reference = run<Engine>(fs, settings, input, { 4096 });
    CHECK_MESSAGE(!firstNonFinite(reference).found, what, ": the reference output holds a NaN or Inf");
    CHECK_MESSAGE(firstMismatch(reference, input).found, what, ": the settings do not change the sound");
    for (const int block : kFixedBlocks) {
        if (block == 4096) continue;
        const Mismatch m = firstMismatch(run<Engine>(fs, settings, input, { block }), reference);
        CHECK_MESSAGE(!m.found, what, " block ", block, " against 4096: ", where(m));
    }
    const Mismatch m = firstMismatch(run<Engine>(fs, settings, input, randomBlocks(input[0].size())), reference);
    CHECK_MESSAGE(!m.found, what, " random block lengths against 4096: ", where(m));
}

// AC-12 for one engine and one setting: input -> active (A) -> bypassed (B) -> active (C), the bypass flag switched at a
// block boundary. 100 ms after the switch into B the output is the input; 100 ms after the switch back it is not.
template <class Engine, class Settings>
void checkBypass(const std::string& what, double fs, int channels, const Settings& active) {
    constexpr int block = 480;
    const auto blocksOf = [&](double seconds) { return static_cast<size_t>(std::lround(seconds * fs / block)) * block; };
    const size_t startB = blocksOf(0.5);
    const size_t startC = startB + blocksOf(0.6);
    const size_t end = startC + blocksOf(0.5);
    const size_t settle = static_cast<size_t>(std::lround(0.1 * fs));

    Settings bypassed = active;
    bypassed.bypass = true;
    const Channels input = noiseSignal(channels, fs, static_cast<double>(end) / fs, 0.6f, 0.05f, 4242u);
    const Channels output = run<Engine>(fs, active, input, { block }, [&](size_t pos) -> const Settings& {
        return (pos >= startB && pos < startC) ? bypassed : active;
    });

    CHECK_MESSAGE(!firstNonFinite(output).found, what, ": a NaN or Inf in the output");
    // The settings do change the sound while not bypassed.
    CHECK_MESSAGE(firstMismatch(output, input, 0, startB).found, what, ": the settings do not change the sound before the bypass");
    const Mismatch same = firstMismatch(output, input, startB + settle, startC);
    CHECK_MESSAGE(!same.found, what, " 100 ms after the bypass is on: ", where(same));
    CHECK_MESSAGE(firstMismatch(output, input, startC + settle, end).found, what, ": bypass off again gives the input unchanged");
}

// AC-15: a run whose output must hold no NaN and no Inf.
template <class Engine, class Settings>
void checkFinite(const std::string& what, double fs, const Settings& settings, const Channels& input, int block) {
    const Channels output = run<Engine>(fs, settings, input, { block });
    const Mismatch m = firstNonFinite(output);
    CHECK_MESSAGE(!m.found, what, " block ", block, ": ", where(m));
}

} // namespace

TEST_CASE("[blocks] the EQ engine gives the same output for every block length") {
    NoDenormals noDenormals;
    const NamedEq configs[] = { { "busy", eqSettings(kEqBusy) }, { "steep", eqSettings(kEqSteep) } };
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            for (const NamedEq& config : configs) {
                checkBlockLengths<EqEngine>(label("EQ noise", fs, channels, config.name), fs, config.settings,
                    noiseSignal(channels, fs, 2.0, 0.5f, 0.05f, 1u));
                checkBlockLengths<EqEngine>(label("EQ sine", fs, channels, config.name), fs, config.settings,
                    sineSignal(channels, fs, 2.0, 0.5));
            }
}

TEST_CASE("[blocks] the EQ engine gives the same output for every block length with Auto Gain off") {
    NoDenormals noDenormals;
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            checkBlockLengths<EqEngine>(label("EQ noise Auto Gain off", fs, channels, "busy"), fs, eqSettings(kEqBusy, false),
                noiseSignal(channels, fs, 2.0, 0.5f, 0.05f, 2u));
}

TEST_CASE("[blocks] the compressor engine gives the same output for every block length") {
    NoDenormals noDenormals;
    // Threshold well under the signal (about -6 dBFS peak); ratio 4 and the infinite ratio; attack and release both
    // ordinary and 0 = fastest.
    const NamedComp configs[] = {
        { "ratio 4 attack 10 release 100", comp(-30, 4, 10, 100) },
        { "ratio inf attack 10 release 100", comp(-30, 1000, 10, 100) },
        { "ratio 4 attack 0 release 0", comp(-30, 4, 0, 0) },
        { "ratio inf attack 0 release 0", comp(-30, 1000, 0, 0) },
        { "ratio 4 attack 0 release 100", comp(-30, 4, 0, 100) },
        { "ratio inf attack 5 release 0", comp(-30, 1000, 5, 0) },
    };
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            for (const NamedComp& config : configs) {
                checkBlockLengths<CompEngine>(label("comp noise", fs, channels, config.name), fs, config.settings,
                    noiseSignal(channels, fs, 2.0, 0.5f, 0.02f, 3u));
                checkBlockLengths<CompEngine>(label("comp sine", fs, channels, config.name), fs, config.settings,
                    sineSignal(channels, fs, 2.0, 0.5));
            }
}

TEST_CASE("[blocks] the compressor settings of the block tests do take gain reduction") {
    NoDenormals noDenormals;
    for (const double fs : kRates)
        for (const float ratio : { 4.0f, 1000.0f }) {
            CompEngine engine;
            const CompEngineSettings settings = comp(-30, ratio, 10, 100);
            engine.prepare(fs, settings);
            Channels x = sineSignal(2, fs, 1.0, 0.5);
            std::vector<float*> pointers { x[0].data(), x[1].data() };
            engine.process(pointers.data(), 2, static_cast<int>(x[0].size()), settings);
            CHECK_MESSAGE(engine.gr() > 0.0, label("comp sine", fs, 2, ratio >= 1000.0f ? "ratio inf" : "ratio 4"), ": gr ", engine.gr());
        }
}

TEST_CASE("[blocks] the compressor engine gives the same output for every block length with Auto Gain off") {
    NoDenormals noDenormals;
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            checkBlockLengths<CompEngine>(label("comp noise Auto Gain off", fs, channels, "ratio 4"), fs,
                comp(-30, 4, 10, 100, false), noiseSignal(channels, fs, 2.0, 0.5f, 0.02f, 4u));
}

TEST_CASE("[bypass] the EQ engine outputs the input unchanged from 100 ms after the bypass is on") {
    NoDenormals noDenormals;
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            for (const bool autoGain : { true, false }) {
                const std::string what = label("EQ", fs, channels, autoGain ? "Auto Gain on" : "Auto Gain off");
                checkBypass<EqEngine>(what + " busy", fs, channels, eqSettings(kEqBusy, autoGain));
                checkBypass<EqEngine>(what + " steep", fs, channels, eqSettings(kEqSteep, autoGain));
            }
}

TEST_CASE("[bypass] the compressor engine outputs the input unchanged from 100 ms after the bypass is on") {
    NoDenormals noDenormals;
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            for (const bool autoGain : { true, false }) {
                const std::string what = label("comp", fs, channels, autoGain ? "Auto Gain on" : "Auto Gain off");
                checkBypass<CompEngine>(what + " ratio 4", fs, channels, comp(-30, 4, 10, 100, autoGain));
                checkBypass<CompEngine>(what + " ratio inf fastest", fs, channels, comp(-30, 1000, 0, 0, autoGain));
            }
}

TEST_CASE("[bypass] engines prepared with the bypass on output the input from the first sample") {
    NoDenormals noDenormals;
    for (const double fs : kRates)
        for (const int channels : { 1, 2 }) {
            const Channels input = noiseSignal(channels, fs, 0.5, 0.6f, 0.05f, 99u);
            const Mismatch eq = firstMismatch(run<EqEngine>(fs, eqSettings(kEqBusy, true, true), input, { 128 }), input);
            CHECK_MESSAGE(!eq.found, label("EQ", fs, channels, "prepared bypassed"), ": ", where(eq));
            const Mismatch cp = firstMismatch(run<CompEngine>(fs, comp(-30, 4, 10, 100, true, true), input, { 128 }), input);
            CHECK_MESSAGE(!cp.found, label("comp", fs, channels, "prepared bypassed"), ": ", where(cp));
        }
}

namespace {
// The settings of AC-15: every gain +18 dB, bell Q 12 and 0.3, HP 48 dB/oct at 20 kHz, LP 48 dB/oct at 20 Hz, frequencies at
// 20 Hz and 20 kHz.
const EqBands kEqExtremeA = bands(on(20000, 0, 1, 48), on(20, 18, 1, 12), on(20, 18, 12, 12), on(20000, 18, 0.3f, 12),
    on(1000, 18, 12, 12), on(20000, 18, 1, 12), on(20, 0, 1, 48));
// The same extremes the other way round.
const EqBands kEqExtremeB = bands(on(20, 0, 1, 48), on(20000, 18, 1, 12), on(20000, 18, 0.3f, 12), on(20, 18, 12, 12),
    on(20, 18, 0.3f, 12), on(20, 18, 1, 12), on(20000, 0, 1, 48));
// Everything on one frequency at the narrowest Q and +18 dB, the passes at 48 dB/oct.
const EqBands kEqExtremeC = bands(on(1000, 0, 1, 48), on(1000, 18, 1, 12), on(1000, 18, 12, 12), on(1000, 18, 12, 12),
    on(1000, 18, 12, 12), on(1000, 18, 1, 12), on(1000, 0, 1, 48));
// Every gain at -18 dB.
const EqBands kEqExtremeCut = bands(on(20, 0, 1, 48), on(20000, -18, 1, 12), on(20, -18, 12, 12), on(20000, -18, 0.3f, 12),
    on(1000, -18, 12, 12), on(20, -18, 1, 12), on(20000, 0, 1, 48));

struct Input {
    const char* name;
    Channels x;
};

constexpr std::array<double, 4> kExtremeRates { 44100.0, 48000.0, 96000.0, 192000.0 };

std::vector<Input> extremeInputs(double fs, int channels) {
    const double amp = std::pow(10.0, 24.0 / 20.0); // +24 dBFS
    std::vector<Input> inputs;
    inputs.push_back({ "sine +24 dBFS", sineSignal(channels, fs, 1.0, amp) });
    inputs.push_back({ "noise +24 dBFS", noiseSignal(channels, fs, 1.0, static_cast<float>(amp), static_cast<float>(amp), 5u) });
    inputs.push_back({ "silence", Channels(static_cast<size_t>(channels), std::vector<float>(framesOf(fs, 1.0), 0.0f)) });

    // Denormal values, both signs, in a different order in each channel.
    Channels denormal(static_cast<size_t>(channels), std::vector<float>(framesOf(fs, 1.0)));
    constexpr float kValues[] = { 1e-40f, -1e-40f, 1e-45f, -1e-45f, 1e-41f, 0.0f };
    for (size_t c = 0; c < denormal.size(); ++c)
        for (size_t i = 0; i < denormal[c].size(); ++i) denormal[c][i] = kValues[(i + c) % std::size(kValues)];
    inputs.push_back({ "denormal", std::move(denormal) });

    // A loud burst and then silence: the filters' state decays through the denormal range.
    Channels tail = sineSignal(channels, fs, 1.0, amp);
    for (auto& channel : tail) std::fill(channel.begin() + static_cast<std::ptrdiff_t>(framesOf(fs, 0.1)), channel.end(), 0.0f);
    inputs.push_back({ "burst then silence", std::move(tail) });
    return inputs;
}
} // namespace

TEST_CASE("[extreme] the EQ engine gives no NaN and no Inf for extreme inputs") {
    NoDenormals noDenormals;
    const NamedEq configs[] = {
        { "default bands", eqSettings(EqBands {}) },
        { "busy", eqSettings(kEqBusy) },
        { "extreme A", eqSettings(kEqExtremeA) },
        { "extreme B", eqSettings(kEqExtremeB) },
        { "extreme C", eqSettings(kEqExtremeC) },
        { "extreme cut", eqSettings(kEqExtremeCut) },
        { "extreme A Auto Gain off", eqSettings(kEqExtremeA, false) },
        { "extreme B Auto Gain off", eqSettings(kEqExtremeB, false) },
        { "extreme C Auto Gain off", eqSettings(kEqExtremeC, false) },
        { "extreme A bypassed", eqSettings(kEqExtremeA, true, true) },
    };
    for (const double fs : kExtremeRates)
        for (const int channels : { 1, 2 })
            for (const Input& input : extremeInputs(fs, channels))
                for (const NamedEq& config : configs)
                    for (const int block : { 1, 480 })
                        checkFinite<EqEngine>(label(std::string("EQ ") + input.name, fs, channels, config.name), fs,
                            config.settings, input.x, block);
}

TEST_CASE("[extreme] the compressor engine gives no NaN and no Inf for extreme inputs") {
    NoDenormals noDenormals;
    const NamedComp configs[] = {
        { "default", comp(0, 4, 10, 100) },
        { "threshold -60 ratio inf attack 0 release 0", comp(-60, 1000, 0, 0) },
        { "threshold -60 ratio 4 attack 0 release 0", comp(-60, 4, 0, 0) },
        { "threshold -60 ratio inf attack 10 release 100", comp(-60, 1000, 10, 100) },
        { "threshold 0 ratio 1 attack 0 release 0", comp(0, 1, 0, 0) },
        { "threshold -60 ratio inf attack 0 release 0 Auto Gain off", comp(-60, 1000, 0, 0, false) },
        { "threshold -60 ratio inf attack 0 release 0 bypassed", comp(-60, 1000, 0, 0, true, true) },
    };
    for (const double fs : kExtremeRates)
        for (const int channels : { 1, 2 })
            for (const Input& input : extremeInputs(fs, channels))
                for (const NamedComp& config : configs)
                    for (const int block : { 1, 480 })
                        checkFinite<CompEngine>(label(std::string("comp ") + input.name, fs, channels, config.name), fs,
                            config.settings, input.x, block);
}

TEST_CASE("[extreme] the engines give no NaN and no Inf while the settings jump between extremes every block") {
    NoDenormals noDenormals;
    const std::vector<EqEngineSettings> eqSequence {
        eqSettings(EqBands {}), eqSettings(kEqExtremeA), eqSettings(kEqExtremeB, false), eqSettings(kEqExtremeC, true, true),
        eqSettings(kEqExtremeCut), eqSettings(kEqExtremeC), eqSettings(kEqBusy, false),
    };
    const std::vector<CompEngineSettings> compSequence {
        comp(0, 4, 10, 100), comp(-60, 1000, 0, 0), comp(-60, 4, 0, 0, false), comp(-60, 1000, 0, 0, true, true),
        comp(0, 1, 0, 0), comp(-30, 1000, 10, 100),
    };
    constexpr int block = 64;
    for (const double fs : kExtremeRates)
        for (const int channels : { 1, 2 })
            for (const Input& input : extremeInputs(fs, channels)) {
                const Channels eq = run<EqEngine>(fs, eqSequence[0], input.x, { block }, [&](size_t pos) -> const EqEngineSettings& {
                    return eqSequence[(pos / block) % eqSequence.size()];
                });
                const Mismatch e = firstNonFinite(eq);
                CHECK_MESSAGE(!e.found, label(std::string("EQ ") + input.name, fs, channels, "jumping settings"), ": ", where(e));
                const Channels cp = run<CompEngine>(fs, compSequence[0], input.x, { block }, [&](size_t pos) -> const CompEngineSettings& {
                    return compSequence[(pos / block) % compSequence.size()];
                });
                const Mismatch c = firstNonFinite(cp);
                CHECK_MESSAGE(!c.found, label(std::string("comp ") + input.name, fs, channels, "jumping settings"), ": ", where(c));
            }
}

namespace {
// The Eq alone on `x`, in blocks of 128.
Channels eqAlone(double fs, const EqBands& b, Channels x) {
    Eq eq;
    eq.prepare(fs, b);
    std::vector<float*> pointers;
    for (auto& channel : x) pointers.push_back(channel.data());
    const size_t frames = x[0].size();
    for (size_t pos = 0; pos < frames; pos += 128) {
        const size_t n = std::min<size_t>(128, frames - pos);
        std::vector<float*> at;
        for (float* p : pointers) at.push_back(p + pos);
        eq.process(at.data(), static_cast<int>(at.size()), static_cast<int>(n));
    }
    return x;
}
} // namespace

TEST_CASE("[autogain-off] the EQ engine with Auto Gain off is the Eq alone to the bit") {
    NoDenormals noDenormals;
    const NamedEq configs[] = {
        { "busy", eqSettings(kEqBusy, false) },
        { "steep", eqSettings(kEqSteep, false) },
        { "default bands", eqSettings(EqBands {}, false) },
    };
    for (const double fs : kRates)
        for (const int channels : { 1, 2 })
            for (const NamedEq& config : configs) {
                const Channels noise = noiseSignal(channels, fs, 1.0, 0.5f, 0.05f, 11u);
                const Channels sine = sineSignal(channels, fs, 1.0, 0.5);
                for (const Channels* input : { &noise, &sine }) {
                    // The engine in blocks of 480, the Eq in blocks of 128: the Eq is the same to the bit at any length.
                    const Mismatch m = firstMismatch(run<EqEngine>(fs, config.settings, *input, { 480 }),
                        eqAlone(fs, config.settings.bands, *input));
                    CHECK_MESSAGE(!m.found, label("EQ engine against Eq", fs, channels, config.name), ": ", where(m));
                }
            }
}

TEST_CASE("[autogain-off] with Auto Gain on and boosting bands the output differs and the amount is the same either way") {
    NoDenormals noDenormals;
    const EqBands boost = bands(EqBandSettings {}, on(100, 6, 1, 12), on(500, 9, 1, 12), on(2000, 12, 2, 12), on(5000, 6, 1, 12),
        on(8000, 6, 1, 12), EqBandSettings {});
    for (const double fs : kRates)
        for (const int channels : { 1, 2 }) {
            const Channels input = noiseSignal(channels, fs, 1.0, 0.5f, 0.5f, 12u);
            const EqEngineSettings onSettings = eqSettings(boost, true);
            const EqEngineSettings offSettings = eqSettings(boost, false);
            const std::string what = label("EQ boosting", fs, channels, "");

            // The makeup is not 0 dB, so the output is not the Eq's.
            const Mismatch same = firstMismatch(run<EqEngine>(fs, onSettings, input, { 128 }), eqAlone(fs, boost, input));
            CHECK_MESSAGE(same.found, what, ": Auto Gain on gives the Eq alone");

            // The amount is the same value with the switch on or off, before and after processing.
            EqEngine withOn;
            EqEngine withOff;
            withOn.prepare(fs, onSettings);
            withOff.prepare(fs, offSettings);
            CHECK_MESSAGE(withOn.makeupDb() != 0.0, what, ": the amount is 0 dB (", withOn.makeupDb(), ")");
            CHECK_MESSAGE(withOn.makeupDb() == withOff.makeupDb(), what, ": on ", withOn.makeupDb(), " off ", withOff.makeupDb());
            const double before = withOn.makeupDb();
            for (EqEngine* engine : { &withOn, &withOff }) {
                Channels x = input;
                std::vector<float*> pointers;
                for (auto& channel : x) pointers.push_back(channel.data());
                const EqEngineSettings& s = (engine == &withOn) ? onSettings : offSettings;
                engine->process(pointers.data(), channels, static_cast<int>(x[0].size()), s);
            }
            CHECK_MESSAGE(withOn.makeupDb() == before, what, ": the amount moved while processing (", before, " to ", withOn.makeupDb(), ")");
            CHECK_MESSAGE(withOn.makeupDb() == withOff.makeupDb(), what, ": after processing on ", withOn.makeupDb(), " off ", withOff.makeupDb());

            // Flipping the switch between blocks does not change the amount either.
            EqEngine flipped;
            flipped.prepare(fs, onSettings);
            Channels x = input;
            std::vector<float*> pointers;
            for (auto& channel : x) pointers.push_back(channel.data());
            for (int i = 0; i < 4; ++i) {
                flipped.process(pointers.data(), channels, 4096, (i % 2 == 0) ? offSettings : onSettings);
                for (float*& p : pointers) p += 4096;
            }
            CHECK_MESSAGE(flipped.makeupDb() == before, what, ": flipping the switch changed the amount (", before, " to ", flipped.makeupDb(), ")");
        }
}
