#pragma once
// The bridge between the page (plugin/web/src/host.ts) and the plug-in. The editor owns it. Message thread only, except
// parameterValueChanged, which may come from any thread and only sets an atomic mark.
//
// Native functions (page -> plug-in, each answers a Promise): ready, setParam, parseValue, setScale, scales, setTheme,
// setHints. Events (plug-in -> page, 30 times a second while the window is open): params (the parameters that changed),
// tick (Auto Gain, playing, sample rate), spectrum (Lesson EQ), points (Lesson Comp).
#include "LessonProcessor.h"
#include "UiSettings.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <lesson/Spectrum.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace lesson::plugin {

class Bridge final : private juce::Timer, private juce::AudioProcessorParameter::Listener {
public:
    static constexpr int kTimerHz = 30;
    // Ticks without a processBlock after which the audio is taken as stopped (200 ms).
    static constexpr int kStillTicks = 6;
    // The spectrum's smoothing per tick: the web app's 0.8 per 60 Hz frame, squared for 30 Hz (design.md 2.6).
    static constexpr double kSpectrumSmoothing = 0.64;
    // Where the events go: (event id, payload). The editor sets it to the web view; a test sets it to a recorder.
    using Sink = std::function<void(const juce::Identifier&, const juce::var&)>;

    // `settings` is the editor's (the window's scale, the theme, the Hint); it must outlive the Bridge.
    Bridge(LessonProcessor&, UiSettings& settings);
    ~Bridge() override;

    void setSink(Sink);
    // Adds the native functions to the web view's options. The Bridge must outlive the web view.
    juce::WebBrowserComponent::Options addTo(const juce::WebBrowserComponent::Options&);
    // The window opened: the taps on, the timer running. stop() is the reverse, and ends a gesture left open.
    void start();
    void stop();

    // The window scale. The editor gives the two calls; without them (no editor: a test) the scale is 1 and the only
    // choice is 100 %, enabled. Both run on the message thread; the editor clears them before it dies.
    //   state: the scale in use and the choices for the screen the window is on.
    //   set: applies a scale when it is one of the four and enabled (else does nothing); answers the state after.
    struct ScaleHooks {
        std::function<ScaleState()> state;
        std::function<ScaleState(double)> set;
    };
    void setScaleHooks(ScaleHooks);

    // ---- the native functions, callable without a web view ----
    // { plugin, version, sampleRate, values, names, makeup, scale, scales, theme, hints }: values are the parameters'
    // plain values by id (Hz, dB, ms, the choice index, 0 or 1), names the host's names.
    juce::var ready();
    // `phase` is begin (the gesture begins, with the value), move (the value), end (the gesture ends) or complete
    // (begin, the value and end at once). A value outside the range lands on its end; an unknown id, a phase that is
    // none of these, or a value that is not a finite number for begin, move and complete changes nothing.
    void setParam(const juce::var& id, const juce::var& value, const juce::var& phase);
    // The typed text as the plug-in reads it (lesson::parseValue, the host's reading too): the plain value, or null
    // when it cannot be read or the id is unknown.
    juce::var parseValue(const juce::var& id, const juce::var& text);
    // { scale, scales: [{ value, enabled }, ...] }
    juce::var scales();
    // Applied when it is a number the editor takes (one of the four, enabled); anything else changes nothing. Answers scales().
    juce::var setScale(const juce::var& scale);
    // Saved when it is light, dark or studio.
    void setTheme(const juce::var& theme);
    // Saved when it is a boolean.
    void setHints(const juce::var& on);

    // One step of the timer: the changed parameters, the tick, then the spectrum (Lesson EQ, while playing) or the
    // level points (Lesson Comp, when there are any).
    void tick();

private:
    void timerCallback() override { tick(); }
    void parameterValueChanged(int parameterIndex, float) override;
    void parameterGestureChanged(int, bool) override {}
    void emit(const char* event, const juce::var&);
    void endOpenGestures();
    std::size_t indexOf(const juce::var& id) const;
    double sampleRate() const;
    ScaleState scaleState() const;
    void sendSpectrum(double rate);
    void sendPoints();

    LessonProcessor& processor_;
    UiSettings& settings_;
    Sink sink_;
    std::vector<int> specOfParameter_; // JUCE parameter index -> table index (-1: not ours); read on any thread, never changed
    std::unique_ptr<std::atomic<bool>[]> changed_;
    std::vector<bool> inGesture_;
    ScaleHooks scaleHooks_;
    bool started_ = false;
    std::uint64_t lastBlocks_ = 0;
    int stillTicks_ = kStillTicks;
    SpectrumAnalyser pre_;
    SpectrumAnalyser post_;
    std::vector<float> samples_;
    std::vector<float> db_;
    std::vector<CompPoint> points_;
    std::vector<float> pointFloats_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Bridge)
};

} // namespace lesson::plugin
