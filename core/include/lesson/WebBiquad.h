#pragma once
// One Web Audio BiquadFilterNode as Chrome computes it (Blink's platform/audio/biquad.cc and
// modules/webaudio/biquad_filter_handler.cc): the AudioParams are floats, the coefficients and the state doubles, and each
// output sample is rounded to float.

namespace lesson {

enum class BiquadType { Lowpass, Highpass, LowShelf, HighShelf, Peaking };

struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

// Blink's NormalizeFrequency (no detune) and Biquad::Set*Params. `nyquist` is 0.5 times the context's float sample rate;
// `q` is the Q AudioParam (in dB for lowpass and highpass, unused by the shelves) and `gain` the gain AudioParam in dB.
BiquadCoefficients webBiquadCoefficients(BiquadType type, float frequency, float q, float gain, double nyquist) noexcept;

// |H(e^jw)|^2 from cos w and cos 2w.
inline double magnitudeSquared(const BiquadCoefficients& c, double cosw, double cos2w) noexcept {
    const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2 + 2 * (c.b0 * c.b1 + c.b1 * c.b2) * cosw + 2 * c.b0 * c.b2 * cos2w;
    const double den = 1 + c.a1 * c.a1 + c.a2 * c.a2 + 2 * (c.a1 + c.a1 * c.a2) * cosw + 2 * c.a2 * cos2w;
    return num / den;
}

// One channel's state of Biquad::Process (direct form I).
class BiquadState {
public:
    void reset() noexcept { x1_ = x2_ = y1_ = y2_ = 0.0; }

    // One sample: the sum in Blink's order, rounded to float.
    float process(const BiquadCoefficients& c, float x) noexcept {
        const float y = static_cast<float>(c.b0 * x + c.b1 * x1_ + c.b2 * x2_ - c.a1 * y1_ - c.a2 * y2_);
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    double x1_ = 0.0;
    double x2_ = 0.0;
    double y1_ = 0.0;
    double y2_ = 0.0;
};

} // namespace lesson
