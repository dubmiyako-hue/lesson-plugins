// AC-11: the changes move as design.md 2.5 says (setTargetAtTime(v, t, 0.005) as Chrome runs it) and HP and LP On and
// Slope cross-fade with a 10 ms raised cosine (2.2). The formulas are written again here, and the outputs are compared
// with chains built from them.
#include <doctest.h>
#include <lesson/Compressor.h>
#include <lesson/Engine.h>
#include <lesson/Eq.h>
#include <lesson/NoDenormals.h>
#include <lesson/Smoother.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace lesson;

namespace {
// design.md 2.5: v += (target - v) * c in float, c = float(1 - exp(-1 / (fs * 0.005))); snaps to the target 10 tau
// after the start, or when |target - v| < exp(-10) |v| (|v| < exp(-10) for a target of 0).
struct Formula {
    float c;
    std::int64_t limit;
    float v;
    float t;
    std::int64_t left = 0;

    Formula(double fs, float start)
        : c(static_cast<float>(1.0 - std::exp(-1.0 / (fs * 0.005)))),
          limit(static_cast<std::int64_t>(std::ceil(0.05 * fs))), v(start), t(start) {}
    void target(float to) {
        if (to == t) return;
        t = to;
        left = limit;
    }
    bool moving() const { return left > 0; }
    float next() {
        const float out = v;
        if (left > 0) {
            v = v + (t - v) * c;
            --left;
            const bool near = t == 0.0f ? std::fabs(v) < 4.539992976248485e-05f
                                        : std::fabs(t - v) < 4.539992976248485e-05 * std::fabs(v);
            if (left == 0 || near) {
                v = t;
                left = 0;
            }
        }
        return out;
    }
};

// design.md 2.2: the weight of the lane faded in, k = 1..N, N = round(0.01 fs).
double fadeWeight(int k, int n) {
    return 0.5 - 0.5 * std::cos(std::numbers::pi * k / n);
}

std::vector<float> noise(int frames, std::uint32_t seed, float amp = 0.5f) {
    std::vector<float> x(static_cast<size_t>(frames));
    for (float& v : x) {
        seed = seed * 1664525u + 1013904223u;
        v = amp * static_cast<float>((static_cast<double>(seed >> 8) - 8388608.0) / 8388608.0);
    }
    return x;
}

EqBands defaults() {
    return { EqBandSettings { false, 80, 0, 1, 24 }, { true, 120, 0, 1, 12 }, { true, 300, 0, 1, 12 }, { true, 1000, 0, 1, 12 },
        { true, 4000, 0, 1, 12 }, { true, 8000, 0, 1, 12 }, { false, 18000, 0, 1, 12 } };
}

// The 13 stages with their own state, coefficients from eqCoefficients.
struct Chain {
    std::vector<BiquadState> s = std::vector<BiquadState>(kEqStages);
    std::array<BiquadCoefficients, kEqStages> c {};
    float run(float x, int from = 0, int to = kEqStages) {
        for (int k = from; k < to; ++k) x = s[static_cast<size_t>(k)].process(c[static_cast<size_t>(k)], x);
        return x;
    }
};
}

TEST_CASE("[smooth] the smoother is the formula and follows the exponential until it snaps") {
    double worst = 0;
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto [from, to] : { std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 0.0f }, std::pair { 1000.0f, 3000.0f },
                 std::pair { 20000.0f, 20.0f }, std::pair { -18.0f, 18.0f }, std::pair { 1.0f, 1.0001f } }) {
            CAPTURE(fs);
            CAPTURE(from);
            CAPTURE(to);
            Smoother s;
            s.prepare(fs);
            s.snap(from);
            Formula f(fs, from);
            CHECK(s.coefficient() == f.c);
            s.setTarget(to);
            f.target(to);
            const double c = f.c;
            bool snapped = false;
            for (int n = 0; n < static_cast<int>(0.06 * fs); ++n) {
                const bool was = f.moving();
                const float expected = f.next();
                REQUIRE(s.next() == expected);
                if (!snapped && was) {
                    // The float steps round by up to half an ulp each; the errors die out by (1 - c) a step, so they
                    // add up to at most ulp / (2c). Near the target a step smaller than half an ulp is lost, and the
                    // value stops that far away until the snap (Chrome's float value does the same). Both stay under
                    // 2^-23 |v| / c.
                    const double exact = to + (static_cast<double>(from) - to) * std::pow(1 - c, n);
                    const double scale = std::max(std::fabs(from), std::fabs(to));
                    const double d = std::fabs(expected - exact) / scale;
                    worst = std::max(worst, d * c);
                    CHECK(d <= std::ldexp(1.0, -23) / c);
                }
                if (!f.moving()) snapped = true;
            }
            CHECK_FALSE(s.moving());
            CHECK(s.value() == to);
        }
    MESSAGE("largest gap to the exponential before the snap, relative to the value, times c: " << worst << " (bound 2^-23 = 1.19e-7)");
}

TEST_CASE("[smooth] the EQ's Freq and Gain and Q move per sample by the formula") {
    const NoDenormals noDenormals;
    const double fs = 48000;
    const int frames = 6000;
    const int change = 1000;
    struct Case {
        const char* name;
        EqBands to;
    };
    std::vector<Case> cases;
    {
        EqBands b = defaults();
        b[3] = { true, 3000, 12, 4, 12 };
        cases.push_back({ "bell 2: freq, gain and Q", b });
    }
    {
        EqBands b = defaults();
        b[1].gain = 6; // on at 0 dB, to 6 dB
        b[5] = { false, 2000, 9, 1, 12 }; // off: gain to 0 dB while the freq moves
        cases.push_back({ "shelves", b });
    }
    for (const auto& item : cases) {
        CAPTURE(item.name);
        EqBands from = defaults();
        if (std::string(item.name) == "shelves") from[5].gain = -6;
        Eq eq;
        eq.prepare(fs, from);
        Chain ref;
        // one formula per band value: freq, gain, q
        std::vector<Formula> f;
        for (const auto& b : from) {
            f.emplace_back(fs, b.freq);
            f.emplace_back(fs, b.on ? b.gain : 0.0f);
            f.emplace_back(fs, b.q);
        }
        const std::vector<float> x = noise(frames, 7);
        double diff = 0;
        for (int i = 0; i < frames; ++i) {
            if (i == change) {
                eq.setTarget(item.to);
                for (size_t b = 0; b < item.to.size(); ++b) {
                    f[3 * b].target(item.to[b].freq);
                    f[3 * b + 1].target(item.to[b].on ? item.to[b].gain : 0.0f);
                    f[3 * b + 2].target(kEqBandKinds[b] == EqBandKind::Bell ? item.to[b].q : 1.0f);
                }
            }
            EqBands now = i < change ? from : item.to;
            for (size_t b = 0; b < now.size(); ++b) {
                now[b].freq = f[3 * b].next();
                now[b].gain = f[3 * b + 1].next();
                now[b].on = true; // the gain is already the band's (0 dB when off)
                now[b].q = f[3 * b + 2].next();
                if (kEqBandKinds[b] == EqBandKind::Highpass || kEqBandKinds[b] == EqBandKind::Lowpass)
                    now[b].on = (i < change ? from : item.to)[b].on;
            }
            eqCoefficients(now, fs, ref.c);
            eq.beginSample();
            const float y = eq.process(0, x[static_cast<size_t>(i)]);
            diff = std::max(diff, std::fabs(static_cast<double>(y) - ref.run(x[static_cast<size_t>(i)])));
        }
        CHECK(diff <= 1e-6);
        CHECK(eq.settled());
    }
}

TEST_CASE("[smooth] the HP's Freq moves both lanes and On and Slope cross-fade over 10 ms") {
    const NoDenormals noDenormals;
    for (const double fs : { 44100.0, 96000.0 }) {
        CAPTURE(fs);
        const int n = static_cast<int>(std::lround(0.01 * fs));
        const int frames = 8 * n; // the HP freq takes up to 10 tau = 5 n
        const int change = n / 2;
        EqBands from = defaults();
        from[0] = { false, 200, 0, 1, 24 };
        from[6] = { true, 5000, 0, 1, 12 };
        EqBands to = from;
        to[0].on = true;   // HP off -> 24 dB/oct
        to[6].slope = 48;  // LP 12 -> 48 dB/oct
        to[0].freq = 400;  // and the HP's freq moves while it fades

        Eq eq;
        eq.prepare(fs, from);
        Chain oldChain;
        Chain newChain;
        eqCoefficients(from, fs, oldChain.c);
        Formula hpFreq(fs, from[0].freq);
        const std::vector<float> x = noise(frames, 11);
        double diff = 0;
        double weightDiff = 0;
        for (int i = 0; i < frames; ++i) {
            if (i == change) {
                eq.setTarget(to);
                hpFreq.target(to[0].freq);
            }
            EqBands oldNow = from;
            EqBands newNow = to;
            const float freq = hpFreq.next();
            oldNow[0].freq = newNow[0].freq = freq;
            eqCoefficients(oldNow, fs, oldChain.c);
            eqCoefficients(newNow, fs, newChain.c);
            eq.beginSample();
            const float y = eq.process(0, x[static_cast<size_t>(i)]);
            float expected;
            if (i < change) {
                expected = oldChain.run(x[static_cast<size_t>(i)]);
                CHECK(eq.fadeWeight(0) == 1.0);
            } else {
                // HP: the off lane and the new lane (state 0 at the change), mixed; then the bands; then LP the same.
                const int k = std::min(i - change + 1, n);
                const double w = fadeWeight(k, n);
                weightDiff = std::max({ weightDiff, std::fabs(eq.fadeWeight(0) - (i - change < n ? w : 1.0)),
                    std::fabs(eq.fadeWeight(1) - (i - change < n ? w : 1.0)) });
                const float a = oldChain.run(x[static_cast<size_t>(i)], 0, kEqPassStages);
                const float b = newChain.run(x[static_cast<size_t>(i)], 0, kEqPassStages);
                float v = i - change < n ? static_cast<float>((1.0 - w) * a + w * b) : b;
                // the five bands: the same in both chains (their state is the new chain's)
                v = newChain.run(v, kEqPassStages, kEqPassStages + 5);
                const float la = oldChain.run(v, kEqPassStages + 5, kEqStages);
                const float lb = newChain.run(v, kEqPassStages + 5, kEqStages);
                expected = i - change < n ? static_cast<float>((1.0 - w) * la + w * lb) : lb;
            }
            if (i == change - 1) {
                // the five bands carry their state over: hand the old chain's to the new one
                for (int k = kEqPassStages; k < kEqPassStages + 5; ++k)
                    newChain.s[static_cast<size_t>(k)] = oldChain.s[static_cast<size_t>(k)];
            }
            diff = std::max(diff, std::fabs(static_cast<double>(y) - expected));
        }
        CHECK(diff <= 1e-6);
        CHECK(weightDiff <= 1e-6);
        CHECK(eq.settled());
    }
}

TEST_CASE("[smooth] a change during a fade starts its own fade when the first ends") {
    const double fs = 48000;
    const int n = 480;
    EqBands from = defaults();
    Eq eq;
    eq.prepare(fs, from);
    EqBands on = from;
    on[0].on = true;
    eq.setTarget(on);
    for (int k = 1; k <= 100; ++k) {
        eq.beginSample();
        CHECK(eq.fadeWeight(0) == doctest::Approx(fadeWeight(k, n)).epsilon(1e-12));
    }
    eq.setTarget(from); // off again, 100 samples into the first fade
    for (int k = 101; k <= n; ++k) {
        eq.beginSample();
        CHECK(eq.fadeWeight(0) == doctest::Approx(fadeWeight(k, n)).epsilon(1e-12));
    }
    for (int k = 1; k <= n; ++k) {
        eq.beginSample();
        CHECK(eq.fadeWeight(0) == doctest::Approx(fadeWeight(k, n)).epsilon(1e-12));
    }
    eq.beginSample();
    CHECK(eq.fadeWeight(0) == 1.0);
    CHECK(eq.settled());
}

TEST_CASE("[smooth] the makeup and wet and dry gains move by the formula") {
    const double fs = 48000;
    WetDry mix;
    mix.prepare(fs, 1.0f, false);
    Formula makeup(fs, 1.0f);
    Formula wet(fs, 1.0f);
    Formula dry(fs, 0.0f);
    const std::vector<float> x = noise(9000, 3);
    const std::vector<float> y = noise(9000, 5);
    double diff = 0;
    for (int i = 0; i < 9000; ++i) {
        if (i == 100) {
            mix.setMakeup(2.0f);
            makeup.target(2.0f);
        }
        if (i == 3000) {
            mix.setBypass(true);
            wet.target(0.0f);
            dry.target(1.0f);
        }
        if (i == 6000) {
            mix.setBypass(false);
            wet.target(1.0f);
            dry.target(0.0f);
        }
        const float mk = makeup.next();
        const float w = wet.next();
        const float d = dry.next();
        const float made = y[static_cast<size_t>(i)] * mk;
        const float expected = made * w + x[static_cast<size_t>(i)] * d;
        mix.beginSample();
        diff = std::max(diff, std::fabs(static_cast<double>(mix.mix(x[static_cast<size_t>(i)], y[static_cast<size_t>(i)])) - expected));
    }
    CHECK(diff <= 1e-6);
    CHECK(mix.settled());
}

TEST_CASE("[smooth] the EQ's Auto Gain moves to a new amount 512 samples after the change") {
    const double fs = 48000;
    EqEngine engine;
    EqEngineSettings s;
    s.bands = defaults();
    engine.prepare(fs, s);
    CHECK(engine.makeupDb() == 0.0);
    s.bands[2].gain = 12; // bell 300 Hz +12 dB Q 1: about -3.08 dB
    std::vector<float> x(1, 0.0f);
    float* channels[] = { x.data() };
    for (int i = 0; i < 511; ++i) engine.process(channels, 1, 1, s);
    CHECK(engine.makeupDb() == 0.0);
    CHECK(engine.wetDry().makeup() == 1.0f);
    engine.process(channels, 1, 1, s);
    CHECK(engine.makeupDb() == doctest::Approx(-3.08).epsilon(0.005));
    CHECK(engine.wetDry().makeup() == gainOfDb(engine.makeupDb()));
    s.autoGain = false; // off is 0 dB, the amount stays
    engine.process(channels, 1, 1, s);
    CHECK(engine.wetDry().makeup() == 1.0f);
    CHECK(engine.makeupDb() == doctest::Approx(-3.08).epsilon(0.005));
}

TEST_CASE("[smooth] the comp's Threshold and Ratio move by the formula but Attack and Release do not") {
    const NoDenormals noDenormals;
    const double fs = 48000;
    const int frames = 64 * 188;
    // CompressorCore with the threshold and 1/ratio from the formula; the web app's targetGr once they have settled.
    struct Reference {
        double fs;
        std::vector<float> ring;
        size_t pos = 0;
        float ringMax = 0, partial = 0;
        int count = 0;
        double gr = 0;
        Formula threshold, inverse;
        float ratio;
        double att, rel;
        Reference(double rate, CompSettings s)
            : fs(rate), ring(20, 0.0f), threshold(rate, s.threshold), inverse(rate, 1.0f / s.ratio), ratio(s.ratio),
              att(timeCoef(s.attack, rate)), rel(timeCoef(s.release, rate)) {}
        void set(CompSettings s) {
            threshold.target(s.threshold);
            inverse.target(s.ratio >= 1000 ? 0.0f : static_cast<float>(1.0 / s.ratio));
            ratio = s.ratio;
            att = timeCoef(s.attack, fs);
            rel = timeCoef(s.release, fs);
        }
        double gain(float peak) {
            if (peak > partial) partial = peak;
            const float level = partial > ringMax ? partial : ringMax;
            if (++count == 48) {
                ring[pos] = partial;
                pos = (pos + 1) % ring.size();
                ringMax = *std::max_element(ring.begin(), ring.end());
                partial = 0;
                count = 0;
            }
            const double db = 20 * std::log10(level > 1e-9 ? static_cast<double>(level) : 1e-9);
            const bool moving = threshold.moving() || inverse.moving();
            const double t = threshold.next();
            const double inv = inverse.next();
            const double over = std::max(0.0, db - t);
            const double target = moving ? over - over * inv : (ratio >= 1000 ? over : over - over / ratio);
            gr = target + (target > gr ? att : rel) * (gr - target);
            return std::pow(10.0, -gr / 20);
        }
    };
    std::vector<float> x(frames);
    for (int i = 0; i < frames; ++i) x[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(0.05 * i));
    const CompSettings first { -6.0f, 2.0f, 0.0f, 50.0f };
    const CompSettings second { -24.0f, 1000.0f, 0.0f, 50.0f }; // and ∞:1
    const CompSettings third { -24.0f, 1000.0f, 100.0f, 5.0f }; // Attack and Release at once
    Compressor comp;
    comp.prepare(fs, first);
    Reference ref(fs, first);
    double diff = 0;
    std::vector<float> y = x;
    for (int start = 0; start < frames; start += 64) {
        if (start == 2048) {
            comp.setTarget(second);
            ref.set(second);
        }
        if (start == 8192) {
            comp.setTarget(third);
            ref.set(third);
        }
        float* channels[] = { y.data() + start };
        comp.process(channels, 1, 64);
        for (int i = start; i < start + 64; ++i) {
            const float v = x[static_cast<size_t>(i)];
            const float expected = static_cast<float>(v * ref.gain(std::fabs(v)));
            diff = std::max(diff, std::fabs(static_cast<double>(y[static_cast<size_t>(i)]) - expected));
        }
        if (start == 2048 + 64) {
            CHECK_FALSE(comp.settled());
        }
    }
    CHECK(diff <= 1e-6);
    CHECK(comp.settled());
    CHECK(comp.threshold() == -24.0f);
    CHECK(comp.inverseRatio() == 0.0f);
}
