// AC-18: the spectrum's analysis is the AnalyserNode's (Chromium's RealtimeAnalyser): Blackman window (alpha 0.16), FFT,
// magnitude times 1 / fftSize, smoothing, dB. Checked against a direct DFT in double, against the recursion written out,
// and against Edge's own getFloatFrequencyData (tests/golden/spectrum, made by tests/golden/gen-spectrum.mjs).
#include "Golden.h"
#include <doctest.h>
#include <lesson/Spectrum.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace lesson;

namespace {
constexpr int kN = SpectrumAnalyser::kFftSize;

// The scale the analyser must have (the golden decided 1 / N, see src/Spectrum.cpp).
constexpr double kScale = 1.0 / kN;

class Rng {
public:
    explicit Rng(std::uint32_t seed) : state_(seed == 0 ? 1u : seed) {}
    float uniform() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ >> 8) / 8388608.0f - 1.0f; // [-1, 1)
    }

private:
    std::uint32_t state_;
};

// A sine off the bins plus noise: every bin carries energy, so a relative error means something at every bin.
std::vector<float> sineAndNoise(double fs, double freq, float noise, std::uint32_t seed) {
    std::vector<float> x(kN);
    Rng rng(seed);
    for (int i = 0; i < kN; ++i)
        x[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * freq * i / fs)) + noise * rng.uniform();
    return x;
}

// The windowed DFT of `x` at bin `k`, by the definition: sum w[n] x[n] exp(-2 pi i k n / N), in double. The window is
// written out again here (Blackman, alpha 0.16), on its own.
class Dft {
public:
    Dft() : cos_(kN), sin_(kN), window_(kN) {
        for (int m = 0; m < kN; ++m) {
            cos_[static_cast<size_t>(m)] = std::cos(2 * std::numbers::pi * m / kN);
            sin_[static_cast<size_t>(m)] = std::sin(2 * std::numbers::pi * m / kN);
            const double alpha = 0.16;
            window_[static_cast<size_t>(m)] = (1 - alpha) / 2 - 0.5 * std::cos(2 * std::numbers::pi * m / kN) +
                alpha / 2 * std::cos(4 * std::numbers::pi * m / kN);
        }
    }
    // |X[k]| times the scale; bin 0 is the real part alone, as the analyser's (the packed Nyquist is blown away).
    double magnitude(const std::vector<float>& x, int k) const {
        double re = 0.0;
        double im = 0.0;
        for (int n = 0; n < kN; ++n) {
            const size_t m = static_cast<size_t>((static_cast<long long>(k) * n) % kN);
            const double v = static_cast<double>(x[static_cast<size_t>(n)]) * window_[static_cast<size_t>(n)];
            re += v * cos_[m];
            im -= v * sin_[m];
        }
        if (k == 0) im = 0.0;
        return std::hypot(re, im) * kScale;
    }

private:
    std::vector<double> cos_;
    std::vector<double> sin_;
    std::vector<double> window_;
};

std::string where(const std::string& what, int bin, double a, double b) {
    std::ostringstream s;
    s.precision(17);
    s << what << " bin " << bin << " measured " << a << " expected " << b;
    return s.str();
}
} // namespace

TEST_CASE("[spectrum] with no smoothing the magnitudes are the windowed DFT's to 1e-9") {
    const Dft dft;
    SpectrumAnalyser analyser;
    int checked = 0;
    for (const double fs : { 48000.0, 44100.0 }) {
        const std::vector<float> x = sineAndNoise(fs, 1234.5, 0.01f, 3u);
        analyser.reset();
        analyser.analyse(x.data(), 0.0);
        std::vector<int> bins;
        for (int k = 0; k < SpectrumAnalyser::kBins; k += 13) bins.push_back(k);
        bins.push_back(SpectrumAnalyser::kBins - 1);
        double worst = 0.0;
        std::vector<float> db(SpectrumAnalyser::kBins);
        analyser.decibels(db.data(), SpectrumAnalyser::kBins);
        for (const int k : bins) {
            const double expected = dft.magnitude(x, k);
            const double got = analyser.magnitude(k);
            worst = std::max(worst, std::fabs(got - expected) / expected);
            CHECK_MESSAGE(std::fabs(got - expected) <= 1e-9 * expected, where("magnitude", k, got, expected));
            // The dB is that magnitude's, as a float.
            CHECK_MESSAGE(std::fabs(db[static_cast<size_t>(k)] - 20 * std::log10(expected)) <= 2e-5, where("dB", k, db[static_cast<size_t>(k)], 20 * std::log10(expected)));
            ++checked;
        }
        MESSAGE("largest relative error of the magnitudes: " << worst);
    }
    CHECK(checked >= 300);
}

TEST_CASE("[spectrum] bin 0 is the real part of the DC alone and the Nyquist component stays out") {
    const Dft dft;
    SpectrumAnalyser analyser;
    std::vector<float> dc(kN, 0.5f);
    analyser.analyse(dc.data(), 0.0);
    CHECK(analyser.magnitude(0) == doctest::Approx(dft.magnitude(dc, 0)).epsilon(1e-12));
    CHECK(analyser.magnitude(0) > 0.1);
    // A sample-rate / 2 wave is all Nyquist; the windowed one leaks only into the last bins, and bin 0 is its tiny DC.
    std::vector<float> nyquist(kN);
    for (int i = 0; i < kN; ++i) nyquist[static_cast<size_t>(i)] = (i % 2 == 0) ? 1.0f : -1.0f;
    analyser.reset();
    analyser.analyse(nyquist.data(), 0.0);
    CHECK(analyser.magnitude(0) == doctest::Approx(dft.magnitude(nyquist, 0)).epsilon(1e-6));
    CHECK(analyser.magnitude(0) < 1e-3);
}

TEST_CASE("[spectrum] smoothing is m = k m + (1 - k) |X|") {
    const std::vector<float> a = sineAndNoise(48000.0, 800.0, 0.05f, 5u);
    const std::vector<float> b = sineAndNoise(48000.0, 5000.0, 0.2f, 6u);
    const std::vector<float> c = sineAndNoise(48000.0, 300.0, 0.001f, 7u);
    // |X| of each, with no smoothing.
    SpectrumAnalyser plain;
    std::vector<double> xa(SpectrumAnalyser::kBins);
    std::vector<double> xb(SpectrumAnalyser::kBins);
    std::vector<double> xc(SpectrumAnalyser::kBins);
    auto magnitudes = [&](const std::vector<float>& x, std::vector<double>& out) {
        plain.reset();
        plain.analyse(x.data(), 0.0);
        for (int i = 0; i < SpectrumAnalyser::kBins; ++i) out[static_cast<size_t>(i)] = plain.magnitude(i);
    };
    magnitudes(a, xa);
    magnitudes(b, xb);
    magnitudes(c, xc);

    for (const double k : { 0.8, 0.5, 0.0, 1.0 }) {
        SpectrumAnalyser s;
        s.analyse(a.data(), k); // from 0
        s.analyse(b.data(), k);
        s.analyse(c.data(), k);
        std::vector<float> db(SpectrumAnalyser::kBins);
        s.decibels(db.data(), SpectrumAnalyser::kBins);
        double worst = 0.0;
        for (int i = 0; i < SpectrumAnalyser::kBins; ++i) {
            const size_t at = static_cast<size_t>(i);
            double m = (1 - k) * xa[at]; // k * 0 + (1 - k) |X|
            m = k * m + (1 - k) * xb[at];
            m = k * m + (1 - k) * xc[at];
            worst = std::max(worst, m == 0.0 ? std::fabs(s.magnitude(i)) : std::fabs(s.magnitude(i) - m) / m);
            if (m == 0.0) CHECK(db[at] == SpectrumAnalyser::kFloorDb);
            else CHECK(std::fabs(db[at] - 20 * std::log10(m)) <= 2e-5);
        }
        CHECK_MESSAGE(worst <= 1e-12, "smoothing " << k << ": largest relative error " << worst);
    }
    // Values outside 0 to 1 are clamped: 2 is 1 (nothing new comes in), -1 is 0 (only the new).
    SpectrumAnalyser high;
    high.analyse(a.data(), 2.0);
    CHECK(high.magnitude(100) == 0.0);
    SpectrumAnalyser low;
    low.analyse(a.data(), 0.8);
    low.analyse(b.data(), -1.0);
    CHECK(low.magnitude(100) == xb[100]);
}

TEST_CASE("[spectrum] a bin of magnitude 0 reads the floor") {
    SpectrumAnalyser analyser;
    std::vector<float> db(SpectrumAnalyser::kBins + 5, 1.0f);
    analyser.decibels(db.data(), SpectrumAnalyser::kBins + 5); // count is cut to kBins
    for (int i = 0; i < SpectrumAnalyser::kBins; ++i) CHECK(db[static_cast<size_t>(i)] == SpectrumAnalyser::kFloorDb);
    for (int i = SpectrumAnalyser::kBins; i < SpectrumAnalyser::kBins + 5; ++i) CHECK(db[static_cast<size_t>(i)] == 1.0f);

    const std::vector<float> silence(kN, 0.0f);
    analyser.analyse(silence.data(), 0.0);
    analyser.decibels(db.data(), 16);
    for (int i = 0; i < 16; ++i) CHECK(db[static_cast<size_t>(i)] == SpectrumAnalyser::kFloorDb);

    const std::vector<float> x = sineAndNoise(48000.0, 1000.0, 0.1f, 9u);
    analyser.analyse(x.data(), 0.0);
    analyser.decibels(db.data(), 16);
    for (int i = 0; i < 16; ++i) CHECK(db[static_cast<size_t>(i)] > SpectrumAnalyser::kFloorDb);
    analyser.reset();
    analyser.decibels(db.data(), 16);
    for (int i = 0; i < 16; ++i) CHECK(db[static_cast<size_t>(i)] == SpectrumAnalyser::kFloorDb);
}

TEST_CASE("[spectrum] a NaN or an Inf in the input does not stay in the smoothing") {
    const std::vector<float> x = sineAndNoise(48000.0, 1000.0, 0.1f, 9u);
    for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
        SpectrumAnalyser analyser;
        std::vector<float> db(SpectrumAnalyser::kBins);
        analyser.analyse(x.data(), 0.64);
        std::vector<float> spoiled = x;
        spoiled[1234] = bad;
        analyser.analyse(spoiled.data(), 0.64);
        analyser.decibels(db.data(), SpectrumAnalyser::kBins);
        CHECK(std::all_of(db.begin(), db.end(), [](float v) { return std::isfinite(v); }));

        // The input is finite again: every bin shows it again.
        analyser.analyse(x.data(), 0.64);
        analyser.decibels(db.data(), SpectrumAnalyser::kBins);
        CHECK(std::all_of(db.begin(), db.end(), [](float v) { return std::isfinite(v) && v > SpectrumAnalyser::kFloorDb; }));
    }
}

TEST_CASE("[spectrum] binsFor is up to 20 kHz times 2^(1/24) and two more and at most kBins") {
    CHECK(SpectrumAnalyser::binsFor(44100.0) == 3827);
    CHECK(SpectrumAnalyser::binsFor(48000.0) == 3516);
    CHECK(SpectrumAnalyser::binsFor(96000.0) == 1759);
    CHECK(SpectrumAnalyser::binsFor(192000.0) == 881);
    CHECK(SpectrumAnalyser::binsFor(32000.0) == SpectrumAnalyser::kBins); // 5273 would be past the last bin
    CHECK(SpectrumAnalyser::binsFor(22050.0) == SpectrumAnalyser::kBins);
}

TEST_CASE("[spectrum] AC-18 the dB values are Edge's AnalyserNode's on the bins the screen reads") {
    // 暫定: 0.1 dB, and the floor below. Measured on Edge 156 (tests/golden/spectrum), the difference over the bins the
    // screen reads is under 0.007 dB at every level down to -130 dB, for a signal with noise (the 0.01 noise sets the
    // floor of three signals near -125 dB) and for two tones with none; in the two tones' data Chromium's float FFT shows
    // its own noise from about -140 dB on (0.011 dB above -140, 0.06 dB above -150, tens of dB above -200).
    // A bin is compared when Edge reads more than kFloorDb: 10 dB above where its float FFT starts to differ.
    constexpr double kToleranceDb = 0.1;
    constexpr double kFloorDb = -130.0;
    const auto cases = golden::rows("spectrum/cases.tsv");
    SpectrumAnalyser analyser;
    double worst = 0.0;
    size_t compared = 0;
    for (const auto& row : cases) {
        REQUIRE(row.size() >= 2);
        const std::string& name = row[0];
        const double fs = golden::number(row[1]);
        const auto input = golden::planar("spectrum/" + name + ".in.f32", 1, kN);
        const auto edge = golden::planar("spectrum/" + name + ".db.f32", 1, SpectrumAnalyser::kBins);
        const int bins = SpectrumAnalyser::binsFor(fs);

        analyser.reset();
        analyser.analyse(input[0].data(), 0.0);
        std::vector<float> mine(SpectrumAnalyser::kBins);
        analyser.decibels(mine.data(), SpectrumAnalyser::kBins);

        double worstHere = 0.0;
        int at = -1;
        size_t used = 0;
        for (int k = 0; k < bins; ++k) {
            const double e = edge[0][static_cast<size_t>(k)];
            if (!(e > kFloorDb)) continue; // also Edge's -Infinity
            const double diff = std::fabs(mine[static_cast<size_t>(k)] - e);
            if (diff > worstHere) {
                worstHere = diff;
                at = k;
            }
            ++used;
        }
        CHECK_MESSAGE(used > 0, name);
        CHECK_MESSAGE(worstHere <= kToleranceDb, name << " " << fs << " Hz: bin " << at << " differs by " << worstHere << " dB");
        MESSAGE(name << ": " << used << " of " << bins << " bins compared, largest difference " << worstHere << " dB (bin " << at << ")");
        worst = std::max(worst, worstHere);
        compared += used;
    }
    MESSAGE("largest difference over all signals: " << worst << " dB, " << compared << " bins");
    CHECK(compared > 5000);
}
