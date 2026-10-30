#pragma once
// Sets flush-to-zero and denormals-are-zero on this thread for the object's lifetime (the audio thread of Chrome's renderer
// runs with both). The core cannot use JUCE's ScopedNoDenormals.
#include <xmmintrin.h>

namespace lesson {

class NoDenormals {
public:
    NoDenormals() noexcept : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | kFlushToZero | kDenormalsAreZero); }
    ~NoDenormals() { _mm_setcsr(saved_); }
    NoDenormals(const NoDenormals&) = delete;
    NoDenormals& operator=(const NoDenormals&) = delete;

private:
    static constexpr unsigned kFlushToZero = 0x8000;
    static constexpr unsigned kDenormalsAreZero = 0x0040;
    unsigned saved_;
};

} // namespace lesson
