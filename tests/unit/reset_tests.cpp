// A host's reset between two plays (VST3 setProcessing(false)): each engine starts afresh without allocating, so the
// next play is a freshly prepared engine's, bit for bit, whatever the last one left (an EQ ringing, a GR, a hold, an
// Auto Gain computation running). And the EQ's tail covers its ring.
#include <doctest.h>
#include <lesson/Engine.h>
#include <lesson/NoDenormals.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace lesson;

namespace {
std::vector<float> noise(int frames, std::uint32_t seed, float level) {
    std::vector<float> x(static_cast<size_t>(frames));
    for (float& v : x) {
        seed = seed * 1664525u + 1013904223u;
        v = level * static_cast<float>((static_cast<double>(seed >> 8) - 8388608.0) / 8388608.0);
    }
    return x;
}

template <class Engine, class Settings>
void run(Engine& engine, std::vector<std::vector<float>>& x, const Settings& settings, int block) {
    const int frames = static_cast<int>(x[0].size());
    std::vector<float*> p(x.size());
    for (int at = 0; at < frames; at += block) {
        for (size_t c = 0; c < x.size(); ++c) p[c] = x[c].data() + at;
        engine.process(p.data(), static_cast<int>(x.size()), std::min(block, frames - at), settings);
    }
}

std::vector<std::vector<float>> signal(int channels, int frames, std::uint32_t seed, float level) {
    std::vector<std::vector<float>> x;
    for (int c = 0; c < channels; ++c) x.push_back(noise(frames, seed + static_cast<std::uint32_t>(c), level));
    return x;
}

// 1 s of loud noise at `played`, one block at `moved` (a change still moving at the stop), reset to `next`: the next
// 0.1 s is a fresh engine's at `next`.
template <class Engine, class Settings>
void freshAfterReset(const Settings& played, const Settings& moved, const Settings& next, int channels, double rate) {
    Engine engine;
    engine.prepare(rate, played);
    auto loud = signal(channels, static_cast<int>(rate), 3, 0.99f);
    run(engine, loud, played, 512);
    auto one = signal(channels, 512, 5, 0.99f);
    run(engine, one, moved, 512);
    engine.reset(next);

    Engine fresh;
    fresh.prepare(rate, next);
    auto a = signal(channels, static_cast<int>(rate / 10), 11, 0.5f);
    auto b = a;
    run(engine, a, next, 480);
    run(fresh, b, next, 480);
    int different = 0;
    for (size_t c = 0; c < a.size(); ++c)
        for (size_t i = 0; i < a[c].size(); ++i) different += a[c][i] != b[c][i];
    CHECK_MESSAGE(different == 0, different << " samples differ from a fresh engine's");
}

EqBands off() {
    EqBands b {};
    for (auto& band : b) band.on = false;
    return b;
}
}

TEST_CASE("[reset] Lesson EQ: the next play is a fresh engine's") {
    const NoDenormals noDenormals;
    EqEngineSettings ringing;
    ringing.bands = off();
    ringing.bands[0] = { true, 20, 0, 1, 48 };
    ringing.bands[2] = { true, 20, 18, 12, 12 };
    ringing.bands[3] = { true, 3000, -12, 4, 12 };
    EqEngineSettings moved = ringing;
    moved.bands[0].slope = 24;          // an HP fade running
    moved.bands[2].freq = 400;          // a bell moving, the Auto Gain computing
    moved.bypass = true;                // the bypass fading
    EqEngineSettings other;             // the defaults
    for (const bool autoGain : { false, true })
        for (const int channels : { 1, 2 })
            for (const double rate : { 44100.0, 192000.0 }) {
                ringing.autoGain = moved.autoGain = autoGain;
                CAPTURE(autoGain);
                CAPTURE(channels);
                CAPTURE(rate);
                freshAfterReset<EqEngine>(ringing, moved, ringing, channels, rate);
                freshAfterReset<EqEngine>(ringing, moved, other, channels, rate);
            }
}

TEST_CASE("[reset] Lesson Comp: the next play is a fresh engine's") {
    const NoDenormals noDenormals;
    CompEngineSettings squashing;
    squashing.comp = { -40.0f, 1000.0f, 0.1f, 1000.0f };
    CompEngineSettings moved = squashing;
    moved.comp.threshold = -10.0f;      // the threshold and the ratio moving
    moved.comp.ratio = 2.0f;
    moved.bypass = true;
    CompEngineSettings other;           // the defaults
    for (const bool autoGain : { false, true })
        for (const int channels : { 1, 2 })
            for (const double rate : { 44100.0, 192000.0 }) {
                squashing.autoGain = moved.autoGain = autoGain;
                CAPTURE(autoGain);
                CAPTURE(channels);
                CAPTURE(rate);
                freshAfterReset<CompEngine>(squashing, moved, squashing, channels, rate);
                freshAfterReset<CompEngine>(squashing, moved, other, channels, rate);
            }
    CompEngine engine;
    engine.prepare(48000, squashing);
    auto loud = signal(1, 48000, 3, 0.99f);
    run(engine, loud, squashing, 512);
    REQUIRE(engine.gr() > 30);
    engine.reset(squashing);
    CHECK(engine.gr() == 0);
}

namespace {
// The seconds from the end of 2 s of a 0 dBFS sine at `f0` until the EQ's output stays under -120 dBFS.
double measuredTail(const EqBands& bands, double rate, double f0) {
    EqEngineSettings s;
    s.bands = bands;
    s.autoGain = false;
    EqEngine engine;
    engine.prepare(rate, s);
    const int on = static_cast<int>(2 * rate);
    const int total = static_cast<int>(40 * rate);
    std::vector<std::vector<float>> x(1, std::vector<float>(static_cast<size_t>(total), 0.0f));
    for (int i = 0; i < on; ++i) x[0][static_cast<size_t>(i)] = static_cast<float>(std::sin(2 * std::numbers::pi * f0 * i / rate));
    run(engine, x, s, 512);
    const float floor = static_cast<float>(std::pow(10.0, -120.0 / 20));
    int last = on;
    for (int i = on; i < total; ++i)
        if (std::fabs(x[0][static_cast<size_t>(i)]) >= floor) last = i;
    REQUIRE(last < total - 1); // it has died away within the 38 s
    return (last - on) / rate;
}
}

TEST_CASE("[reset] the EQ's tail covers its ring") {
    const NoDenormals noDenormals;
    struct Case {
        const char* name;
        EqBands bands;
        double f0;
    };
    EqBands bell = off();
    bell[2] = { true, 20, 18, 12, 12 };
    EqBands bell1k = off();
    bell1k[2] = { true, 1000, 18, 12, 12 };
    EqBands hp = off();
    hp[0] = { true, 20, 0, 1, 48 };
    EqBands pile = off();
    pile[0] = { true, 20, 0, 1, 48 };
    pile[1] = { true, 20, 18, 1, 12 };
    for (int k = 2; k <= 4; ++k) pile[static_cast<size_t>(k)] = { true, 20, 18, 12, 12 };
    pile[5] = { true, 20, 18, 1, 12 };
    pile[6] = { true, 20, 0, 1, 48 };
    const Case cases[] = { { "Bell 20 Hz Q 12 +18", bell, 20 }, { "Bell 1 kHz Q 12 +18", bell1k, 1000 },
        { "HP 20 Hz 48 dB/oct", hp, 20 }, { "all 7 bands at 20 Hz", pile, 20 } };
    for (const Case& c : cases)
        for (const double rate : { 44100.0, 48000.0, 192000.0 }) {
            CAPTURE(c.name);
            CAPTURE(rate);
            const double tail = eqTailSeconds(c.bands, rate);
            const double measured = measuredTail(c.bands, rate, c.f0);
            CAPTURE(tail);
            CAPTURE(measured);
            CHECK(measured <= tail);
            CHECK(tail <= 3 * measured + 0.05); // not far over (the sum over the stages and the margin are the loose part)
        }
    // nothing changes the signal at the defaults: no tail
    CHECK(eqTailSeconds(EqEngineSettings {}.bands, 48000) == 0.0);
}
