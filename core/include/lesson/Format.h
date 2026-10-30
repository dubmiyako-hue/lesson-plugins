#pragma once
// The web app's value formats (mix-course src/params.ts) with JavaScript's rounding: toFixed rounds the exact value of the
// double half away from zero and Math.round rounds half up, where printf would round half to even.
#include "Params.h"
#include <string>

namespace lesson {

// Number.prototype.toFixed(digits) for finite values below 1e21.
std::string toFixed(double v, int digits);
// Math.round.
double jsRound(double v) noexcept;

std::string fmtDb(double v);    // '-18.0 dB', '0.0 dB', '+6.0 dB'
std::string fmtHz(double v);    // '47.5 Hz', '2.50 kHz'
std::string fmtMs(double v);    // '10.0 ms', '1.00 s'
std::string fmtRatio(double v); // '4.0:1'
std::string fmtQ(double v);     // '1.00'
std::string formatUnit(Unit unit, double v);
// params.ts showValue: the zero and inf texts at their notches, otherwise the unit's format.
std::string showValue(const Range& r, Unit unit, double v);

} // namespace lesson
