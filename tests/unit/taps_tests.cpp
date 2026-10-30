// The screen's taps (design.md 2.6, 4): they are read-only copies of the signal, so the written output must be the same
// with and without them (bit for bit); the EQ's taps are the channels' mean before and after the 13 biquads, and the
// compressor's points are the worklet's (src/compressor.worklet.ts collect) per 256 samples. SignalRing and CompPoints are
// checked on their own. Every engine run is inside lesson::NoDenormals, as the plug-ins process.
#include <doctest.h>
#include <lesson/Compressor.h>
#include <lesson/Engine.h>
#include <lesson/Eq.h>
#include <lesson/NoDenormals.h>
#include <lesson/Taps.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
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

// Noise whose level steps between `loud` and `quiet` every 0.1 s (the compressor has gain reduction to take and to
// release), a different sequence in every channel.
Channels noiseSignal(int channels, double fs, size_t frames, float loud, float quiet, std::uint32_t seed) {
    Channels x(static_cast<size_t>(channels), std::vector<float>(frames));
    const size_t segment = static_cast<size_t>(std::lround(0.1 * fs));
    for (size_t c = 0; c < x.size(); ++c) {
        Rng rng(seed + 7919u * static_cast<std::uint32_t>(c));
        for (size_t i = 0; i < x[c].size(); ++i) x[c][i] = rng.uniform() * (((i / segment) % 2 == 0) ? loud : quiet);
    }
    return x;
}

// Block lengths from 1 to 700, the same sequence for the same seed.
std::vector<int> blockLengths(size_t frames, std::uint32_t seed) {
    Rng rng(seed);
    std::vector<int> lengths;
    for (size_t sum = 0; sum < frames;) {
        const int n = rng.range(1, 700);
        lengths.push_back(n);
        sum += static_cast<size_t>(n);
    }
    return lengths;
}

constexpr std::array<double, 3> kRates { 44100.0, 48000.0, 96000.0 };

EqBandSettings on(float freq, float gain, float q, int slope) { return { true, freq, gain, q, slope }; }

EqEngineSettings eqSettings(const EqBands& b, bool autoGain, bool bypass) {
    EqEngineSettings s;
    s.bands = b;
    s.autoGain = autoGain;
    s.bypass = bypass;
    return s;
}

CompEngineSettings compSettings(float threshold, float ratio, float attack, float release, bool autoGain, bool bypass) {
    CompEngineSettings s;
    s.comp = { threshold, ratio, attack, release };
    s.autoGain = autoGain;
    s.bypass = bypass;
    return s;
}

const EqBands kBusy { on(80, 0, 1, 24), on(120, 4, 1, 12), on(300, -5, 2, 12), on(1500, 6, 0.7f, 12), on(6000, -3, 4, 12),
    on(8000, 3, 1, 12), on(12000, 0, 1, 36) };
const EqBands kSteep { on(30, 0, 1, 48), on(200, -12, 1, 12), on(100, 12, 12, 12), on(2500, -18, 0.3f, 12), on(9000, 18, 12, 12),
    on(5000, -9, 1, 12), on(18000, 0, 1, 48) };
const EqBands kFlat {};

// Settings that change from block to block: `settings[(block / 6) % size]`.
std::vector<EqEngineSettings> eqSequence() {
    return { eqSettings(kBusy, true, false), eqSettings(kSteep, true, false), eqSettings(kBusy, false, true),
        eqSettings(kFlat, true, false), eqSettings(kBusy, false, false) };
}
std::vector<CompEngineSettings> compSequence() {
    return { compSettings(-24, 4, 10, 100, true, false), compSettings(-40, 1000, 0, 50, true, false),
        compSettings(-12, 2, 1, 500, false, true), compSettings(-30, 8, 30, 20, false, false) };
}

template <class Settings>
const Settings& settingsOf(const std::vector<Settings>& sequence, size_t block) {
    return sequence[(block / 6) % sequence.size()];
}

std::string describe(const char* what, double fs, int channels, const std::string& more = "") {
    std::ostringstream s;
    s << what << " " << static_cast<int>(fs) << " Hz " << channels << " ch " << more;
    return s.str();
}

// Runs an engine over `x` in place, block by block (the lengths cycled); `after(block, begin, n, settings)` is called after
// each block with the block's position.
template <class Engine, class Settings, class Call, class After>
void runBlocks(Engine& engine, Channels& x, const std::vector<int>& lengths, const std::vector<Settings>& sequence, Call call,
    After after) {
    std::vector<float*> pointers;
    for (auto& channel : x) pointers.push_back(channel.data());
    const size_t frames = x[0].size();
    size_t pos = 0;
    size_t block = 0;
    while (pos < frames) {
        const size_t n = std::min(static_cast<size_t>(lengths[block % lengths.size()]), frames - pos);
        std::vector<float*> at;
        for (float* p : pointers) at.push_back(p + pos);
        const Settings& settings = settingsOf(sequence, block);
        call(engine, at.data(), static_cast<int>(at.size()), static_cast<int>(n), settings);
        after(block, pos, n, settings);
        pos += n;
        ++block;
    }
}

// The index of the first sample (of any channel) that differs in bits, or -1.
long long firstDifference(const Channels& a, const Channels& b) {
    if (a.size() != b.size()) return 0;
    for (size_t i = 0;; ++i) {
        bool any = false;
        for (size_t c = 0; c < a.size(); ++c) {
            if (i >= a[c].size() || i >= b[c].size()) continue;
            any = true;
            if (!(a[c][i] == b[c][i])) return static_cast<long long>(i);
        }
        if (!any) return -1;
    }
}

float meanOf(const Channels& x, size_t i) {
    return x.size() == 1 ? x[0][i] : 0.5f * (x[0][i] + x[1][i]);
}
} // namespace

TEST_CASE("[taps] the EQ engine writes the same output with and without taps") {
    NoDenormals noDenormals;
    const auto sequence = eqSequence();
    for (const double fs : kRates) {
        for (int channels = 1; channels <= 2; ++channels) {
            const size_t frames = static_cast<size_t>(fs * 0.4);
            const Channels input = noiseSignal(channels, fs, frames, 0.5f, 0.05f, 11u);
            const auto lengths = blockLengths(frames, 21u + static_cast<std::uint32_t>(channels));

            EqEngine plain;
            plain.prepare(fs, sequence[0]);
            Channels a = input;
            runBlocks(plain, a, lengths, sequence,
                [](EqEngine& e, float* const* c, int n, int len, const EqEngineSettings& s) { e.process(c, n, len, s); },
                [](size_t, size_t, size_t, const EqEngineSettings&) {});

            SignalRing pre;
            SignalRing post;
            const EqTaps taps { &pre, &post };
            EqEngine tapped;
            tapped.prepare(fs, sequence[0]);
            Channels b = input;
            runBlocks(tapped, b, lengths, sequence,
                [&](EqEngine& e, float* const* c, int n, int len, const EqEngineSettings& s) { e.process(c, n, len, s, &taps); },
                [](size_t, size_t, size_t, const EqEngineSettings&) {});

            const long long at = firstDifference(a, b);
            CHECK_MESSAGE(at < 0, describe("EQ", fs, channels, "first difference at sample " + std::to_string(at)));
            // A tap that is not given is skipped, the other one still works.
            SignalRing only;
            const EqTaps preOnly { &only, nullptr };
            EqEngine third;
            third.prepare(fs, sequence[0]);
            Channels c3 = input;
            runBlocks(third, c3, lengths, sequence,
                [&](EqEngine& e, float* const* c, int n, int len, const EqEngineSettings& s) { e.process(c, n, len, s, &preOnly); },
                [](size_t, size_t, size_t, const EqEngineSettings&) {});
            CHECK_MESSAGE(firstDifference(a, c3) < 0, describe("EQ", fs, channels, "with only the pre tap"));
        }
    }
}

TEST_CASE("[taps] the EQ taps are the channels' mean of the input and of the biquads' output") {
    NoDenormals noDenormals;
    const auto sequence = eqSequence();
    for (const double fs : kRates) {
        for (int channels = 1; channels <= 2; ++channels) {
            const size_t frames = static_cast<size_t>(fs * 0.4);
            const Channels input = noiseSignal(channels, fs, frames, 0.5f, 0.05f, 31u);
            const auto lengths = blockLengths(frames, 41u);

            // The reference: a separate Eq prepared and moved the way the engine moves its own (setTarget per block).
            Eq eq;
            eq.prepare(fs, sequence[0].bands);
            Channels biquads(input.size(), std::vector<float>(frames));

            SignalRing pre;
            SignalRing post;
            const EqTaps taps { &pre, &post };
            EqEngine engine;
            engine.prepare(fs, sequence[0]);
            Channels x = input;
            long long preBad = -1;
            long long postBad = -1;
            std::vector<float> got(700);
            runBlocks(engine, x, lengths, sequence,
                [&](EqEngine& e, float* const* c, int n, int len, const EqEngineSettings& s) { e.process(c, n, len, s, &taps); },
                [&](size_t, size_t begin, size_t n, const EqEngineSettings& s) {
                    // The reference for the block just processed.
                    eq.setTarget(s.bands);
                    for (size_t i = begin; i < begin + n; ++i) {
                        eq.beginSample();
                        for (size_t c = 0; c < input.size(); ++c) biquads[c][i] = eq.process(static_cast<int>(c), input[c][i]);
                    }
                    // The latest n samples of each ring are the block's, oldest first.
                    pre.latest(got.data(), static_cast<int>(n));
                    for (size_t i = 0; i < n && preBad < 0; ++i)
                        if (!(got[i] == meanOf(input, begin + i))) preBad = static_cast<long long>(begin + i);
                    post.latest(got.data(), static_cast<int>(n));
                    for (size_t i = 0; i < n && postBad < 0; ++i)
                        if (!(got[i] == meanOf(biquads, begin + i))) postBad = static_cast<long long>(begin + i);
                });
            CHECK_MESSAGE(preBad < 0, describe("EQ pre", fs, channels, "first difference at sample " + std::to_string(preBad)));
            CHECK_MESSAGE(postBad < 0, describe("EQ post", fs, channels, "first difference at sample " + std::to_string(postBad)));
        }
    }
}

TEST_CASE("[taps] the EQ taps are taken while bypassed too") {
    NoDenormals noDenormals;
    const double fs = 48000.0;
    const size_t frames = 4000;
    const Channels input = noiseSignal(2, fs, frames, 0.5f, 0.5f, 5u);
    const EqEngineSettings bypassed = eqSettings(kBusy, true, true);
    EqEngine engine;
    engine.prepare(fs, bypassed);
    SignalRing pre;
    SignalRing post;
    const EqTaps taps { &pre, &post };
    Channels x = input;
    float* ptrs[2] { x[0].data(), x[1].data() };
    engine.process(ptrs, 2, 480, bypassed, &taps);
    std::vector<float> a(480);
    std::vector<float> b(480);
    pre.latest(a.data(), 480);
    post.latest(b.data(), 480);
    // Bypassed from the start, the output is the input, but the engine runs the EQ all the same: the pre tap is the input's
    // mean and the post tap the biquads' output, which is not.
    bool preIsInput = true;
    bool different = false;
    for (size_t i = 0; i < 480; ++i) {
        if (!(a[i] == meanOf(input, i))) preIsInput = false;
        if (a[i] != b[i]) different = true;
    }
    CHECK(preIsInput);
    CHECK(different);
}

TEST_CASE("[taps] the compressor engine writes the same output with and without points") {
    NoDenormals noDenormals;
    const auto sequence = compSequence();
    for (const double fs : kRates) {
        for (int channels = 1; channels <= 2; ++channels) {
            const size_t frames = static_cast<size_t>(fs * 0.5);
            const Channels input = noiseSignal(channels, fs, frames, 0.9f, 0.02f, 51u);
            const auto lengths = blockLengths(frames, 61u + static_cast<std::uint32_t>(channels));

            CompEngine plain;
            plain.prepare(fs, sequence[0]);
            Channels a = input;
            runBlocks(plain, a, lengths, sequence,
                [](CompEngine& e, float* const* c, int n, int len, const CompEngineSettings& s) { e.process(c, n, len, s); },
                [](size_t, size_t, size_t, const CompEngineSettings&) {});

            CompPoints points;
            CompEngine tapped;
            tapped.prepare(fs, sequence[0]);
            Channels b = input;
            runBlocks(tapped, b, lengths, sequence,
                [&](CompEngine& e, float* const* c, int n, int len, const CompEngineSettings& s) { e.process(c, n, len, s, &points); },
                [](size_t, size_t, size_t, const CompEngineSettings&) {});

            const long long at = firstDifference(a, b);
            CHECK_MESSAGE(at < 0, describe("comp", fs, channels, "first difference at sample " + std::to_string(at)));
            CHECK(plain.gr() == tapped.gr());
        }
    }
}

TEST_CASE("[taps] the compressor points are the worklet's per 256 samples") {
    NoDenormals noDenormals;
    // Constant settings that take gain reduction on the loud steps and release it on the quiet ones.
    const std::vector<CompEngineSettings> settings { compSettings(-24, 4, 5, 80, true, false) };
    for (const double fs : kRates) {
        for (int channels = 1; channels <= 2; ++channels) {
            const size_t count = 300; // points
            const size_t frames = count * CompPoints::kSamplesPerPoint + 100; // and a partial one that is not a point yet
            const Channels input = noiseSignal(channels, fs, frames, 0.9f, 0.02f, 71u);
            const auto lengths = blockLengths(frames, 81u);

            CompPoints points;
            CompEngine engine;
            engine.prepare(fs, settings[0]);
            Channels x = input;
            std::vector<CompPoint> got;
            std::array<CompPoint, 7> buffer {};
            runBlocks(engine, x, lengths, settings,
                [&](CompEngine& e, float* const* c, int n, int len, const CompEngineSettings& s) { e.process(c, n, len, s, &points); },
                [&](size_t, size_t, size_t, const CompEngineSettings&) {
                    for (int n; (n = points.take(buffer.data(), static_cast<int>(buffer.size()))) > 0;)
                        got.insert(got.end(), buffer.begin(), buffer.begin() + n);
                });
            REQUIRE(got.size() == count);

            // The reference: Compressor::process in 128-sample blocks (the worklet's), two to a point.
            Compressor reference;
            reference.prepare(fs, settings[0].comp);
            Channels y = input;
            std::vector<float*> ptrs;
            for (auto& channel : y) ptrs.push_back(channel.data());
            size_t wrongIn = 0;
            size_t wrongOut = 0;
            size_t wrongGr = 0;
            float grSeen = 0.0f;
            for (size_t p = 0; p < count; ++p) {
                float in = 0.0f;
                float out = 0.0f;
                double gr = 0.0;
                for (int half = 0; half < 2; ++half) {
                    const size_t begin = p * CompPoints::kSamplesPerPoint + static_cast<size_t>(half) * 128;
                    for (size_t c = 0; c < y.size(); ++c)
                        for (size_t i = begin; i < begin + 128; ++i) in = std::max(in, std::fabs(input[c][i]));
                    reference.setTarget(settings[0].comp);
                    float* at[2] = { ptrs[0] + begin, ptrs.back() + begin };
                    gr = std::max(gr, reference.process(at, static_cast<int>(y.size()), 128));
                    for (size_t c = 0; c < y.size(); ++c)
                        for (size_t i = begin; i < begin + 128; ++i) out = std::max(out, std::fabs(y[c][i]));
                }
                if (!(got[p].in == in)) ++wrongIn;
                if (!(got[p].out == out)) ++wrongOut;
                if (!(got[p].gr == static_cast<float>(gr))) ++wrongGr;
                grSeen = std::max(grSeen, got[p].gr);
            }
            CHECK_MESSAGE(wrongIn == 0, describe("comp points", fs, channels, std::to_string(wrongIn) + " wrong input peaks"));
            CHECK_MESSAGE(wrongOut == 0, describe("comp points", fs, channels, std::to_string(wrongOut) + " wrong output peaks"));
            CHECK_MESSAGE(wrongGr == 0, describe("comp points", fs, channels, std::to_string(wrongGr) + " wrong GR"));
            CHECK_MESSAGE(grSeen > 3.0f, describe("comp points", fs, channels, "the signal takes some gain reduction"));
        }
    }
}

TEST_CASE("[taps] SignalRing returns the latest samples oldest first and zeros for what was never written") {
    SignalRing ring;
    std::vector<float> out(SignalRing::kSize);
    ring.latest(out.data(), 8);
    for (int i = 0; i < 8; ++i) CHECK(out[static_cast<size_t>(i)] == 0.0f);

    for (int i = 1; i <= 3; ++i) ring.push(static_cast<float>(i));
    ring.latest(out.data(), 8);
    const float expected[8] = { 0, 0, 0, 0, 0, 1, 2, 3 };
    for (int i = 0; i < 8; ++i) CHECK(out[static_cast<size_t>(i)] == expected[i]);
    ring.latest(out.data(), 2);
    CHECK(out[0] == 2.0f);
    CHECK(out[1] == 3.0f);

    // Past kSize: the samples written longer ago than the ring holds are gone, the latest ones are all there.
    SignalRing lapped;
    const int total = 3 * SignalRing::kSize + 123;
    for (int i = 1; i <= total; ++i) lapped.push(static_cast<float>(i));
    lapped.latest(out.data(), SignalRing::kSize);
    bool ordered = true;
    for (int i = 0; i < SignalRing::kSize; ++i)
        if (out[static_cast<size_t>(i)] != static_cast<float>(total - SignalRing::kSize + 1 + i)) ordered = false;
    CHECK(ordered);
    lapped.latest(out.data(), 8192);
    CHECK(out[0] == static_cast<float>(total - 8192 + 1));
    CHECK(out[8191] == static_cast<float>(total));

    // Exactly kSize written: the whole ring, no zeros.
    SignalRing full;
    for (int i = 1; i <= SignalRing::kSize; ++i) full.push(static_cast<float>(i));
    full.latest(out.data(), SignalRing::kSize);
    CHECK(out[0] == 1.0f);
    CHECK(out[SignalRing::kSize - 1] == static_cast<float>(SignalRing::kSize));
    // One short of kSize written, asked for kSize: one zero first.
    SignalRing nearly;
    for (int i = 1; i < SignalRing::kSize; ++i) nearly.push(static_cast<float>(i));
    nearly.latest(out.data(), SignalRing::kSize);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 1.0f);
}

namespace {
// Feeds `samples` samples whose peaks are (in, out, gr) at the first sample and 0 elsewhere.
void feed(CompPoints& points, int samples, float in, float out, double gr) {
    for (int i = 0; i < samples; ++i) points.add(i == 0 ? in : 0.0f, i == 0 ? out : 0.0f, i == 0 ? gr : 0.0);
}
} // namespace

TEST_CASE("[taps] CompPoints makes a point per 256 samples from the largest in and out and gr") {
    CompPoints points;
    std::array<CompPoint, 8> out {};
    for (int i = 0; i < 255; ++i) points.add(0.1f, 0.05f, 0.0);
    CHECK(points.take(out.data(), 8) == 0);
    // The largest of each comes at a different sample; gr starts at 0, so a point of no GR is 0.
    CompPoints mixed;
    for (int i = 0; i < 256; ++i)
        mixed.add(i == 17 ? 0.9f : 0.1f, i == 200 ? 0.7f : 0.2f, i == 255 ? 6.5 : (i == 3 ? 2.0 : 0.0));
    REQUIRE(mixed.take(out.data(), 8) == 1);
    CHECK(out[0].in == 0.9f);
    CHECK(out[0].out == 0.7f);
    CHECK(out[0].gr == 6.5f);
    // The next point starts from nothing, not from the last one's maxima.
    feed(mixed, 256, 0.0f, 0.0f, 0.0);
    REQUIRE(mixed.take(out.data(), 8) == 1);
    CHECK(out[0].in == 0.0f);
    CHECK(out[0].out == 0.0f);
    CHECK(out[0].gr == 0.0f);
    // The 256th sample completes it, the 257th starts the next.
    points.add(0.1f, 0.05f, 0.0);
    REQUIRE(points.take(out.data(), 8) == 1);
    for (int i = 0; i < 255; ++i) points.add(0.1f, 0.05f, 0.0);
    CHECK(points.take(out.data(), 8) == 0);
    points.add(0.1f, 0.05f, 0.0);
    CHECK(points.take(out.data(), 8) == 1);
}

TEST_CASE("[taps] CompPoints take returns the oldest first and at most max") {
    CompPoints points;
    for (int p = 1; p <= 5; ++p) feed(points, 256, static_cast<float>(p), 0.0f, 0.0);
    std::array<CompPoint, 8> out {};
    REQUIRE(points.take(out.data(), 2) == 2);
    CHECK(out[0].in == 1.0f);
    CHECK(out[1].in == 2.0f);
    REQUIRE(points.take(out.data(), 8) == 3);
    CHECK(out[0].in == 3.0f);
    CHECK(out[2].in == 5.0f);
    CHECK(points.take(out.data(), 8) == 0);
    CHECK(points.take(out.data(), 0) == 0);
}

TEST_CASE("[taps] CompPoints drops new points while the queue is full and takes them again once there is room") {
    CompPoints points;
    const int extra = 10;
    for (int p = 1; p <= CompPoints::kCapacity + extra; ++p) feed(points, 256, static_cast<float>(p), 0.0f, 0.0);
    std::vector<CompPoint> out(CompPoints::kCapacity + 20);
    REQUIRE(points.take(out.data(), static_cast<int>(out.size())) == CompPoints::kCapacity);
    // The oldest are kept and the newest dropped.
    bool kept = true;
    for (int p = 0; p < CompPoints::kCapacity; ++p)
        if (out[static_cast<size_t>(p)].in != static_cast<float>(p + 1)) kept = false;
    CHECK(kept);
    CHECK(points.take(out.data(), 1) == 0);
    // Room again; the dropped points are not coming back, and the numbering goes on from here.
    feed(points, 256, 99.0f, 0.0f, 0.0);
    REQUIRE(points.take(out.data(), 4) == 1);
    CHECK(out[0].in == 99.0f);
    // A queue that wrapped past its capacity many times keeps its order.
    for (int round = 0; round < 3; ++round) {
        for (int p = 1; p <= CompPoints::kCapacity - 1; ++p) feed(points, 256, static_cast<float>(p), 0.0f, 0.0);
        REQUIRE(points.take(out.data(), static_cast<int>(out.size())) == CompPoints::kCapacity - 1);
        CHECK(out[0].in == 1.0f);
        CHECK(out[CompPoints::kCapacity - 2].in == static_cast<float>(CompPoints::kCapacity - 1));
    }
}

TEST_CASE("[taps] CompPoints restart forgets only the point being gathered") {
    CompPoints points;
    feed(points, 256, 1.0f, 0.5f, 3.0);       // a point in the queue
    feed(points, 100, 5.0f, 4.0f, 9.0);       // a partial one
    points.restart();
    feed(points, 255, 0.25f, 0.125f, 1.0);    // 255 samples toward a new point: not a point yet
    std::array<CompPoint, 4> out {};
    REQUIRE(points.take(out.data(), 4) == 1); // the queued one stayed
    CHECK(out[0].in == 1.0f);
    CHECK(out[0].out == 0.5f);
    CHECK(out[0].gr == 3.0f);
    points.add(0.0f, 0.0f, 0.0);              // the 256th sample of the new point
    REQUIRE(points.take(out.data(), 4) == 1);
    CHECK(out[0].in == 0.25f); // nothing of the partial point (5, 4, 9) came along
    CHECK(out[0].out == 0.125f);
    CHECK(out[0].gr == 1.0f);
}

TEST_CASE("[taps] CompPoints works between two threads") {
    CompPoints points;
    const int total = 20000;
    std::atomic<bool> done { false };
    std::vector<float> seen;
    std::thread reader([&] {
        std::array<CompPoint, 64> buffer {};
        for (;;) {
            const bool last = done.load(std::memory_order_acquire);
            const int n = points.take(buffer.data(), static_cast<int>(buffer.size()));
            for (int i = 0; i < n; ++i) seen.push_back(buffer[static_cast<size_t>(i)].in);
            if (n == 0 && last) break;
            if (n == 0) std::this_thread::yield();
        }
    });
    for (int p = 1; p <= total; ++p) feed(points, 256, static_cast<float>(p), 0.0f, 0.0);
    done.store(true, std::memory_order_release);
    reader.join();
    // A point may be dropped when the reader is slow, but never reordered, repeated or torn.
    REQUIRE(!seen.empty());
    bool increasing = true;
    for (size_t i = 1; i < seen.size(); ++i)
        if (!(seen[i] > seen[i - 1])) increasing = false;
    CHECK(increasing);
    CHECK(seen.front() >= 1.0f);
    CHECK(seen.back() <= static_cast<float>(total));
    CHECK(std::all_of(seen.begin(), seen.end(), [](float v) { return v == std::floor(v); }));
}
