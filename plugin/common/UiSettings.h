#pragma once
// The PC's settings of the window (not the session's, PRAYROOM behavior.md 状態の保存), in the file the PRAYROOM plug-ins
// share: <AppData>\PRAYROOM\settings.json, a JSON object (design.md 6):
//   {"version":1,"scale":1.0,"theme":"light","lesson":{"hint":true}}
// The scale and the theme are shared with the other PRAYROOM plug-ins; "lesson" is Lesson EQ's and Lesson Comp's own.
// Every key it does not write is kept as it was. Message thread only. The constructor only keeps the path.
#include <juce_core/juce_core.h>
#include <array>
#include <functional>
#include <vector>

namespace lesson::plugin {

// One entry of the scale list: a window scale and whether the window's screen has the room for it.
struct ScaleChoice {
    double value = 1.0;
    bool enabled = true;
};

// The scale in use and the choices, in the order of the list.
struct ScaleState {
    double scale = 1.0;
    std::vector<ScaleChoice> choices;
};

class UiSettings {
public:
    // PRAYROOM's four window scales: 100, 125, 150 and 200 %.
    static constexpr std::array<double, 4> kScales { 1.0, 1.25, 1.5, 2.0 };
    static bool isKnownScale(double value) noexcept;
    // "light", "dark", "studio" (the page's data-theme).
    static bool isKnownTheme(const juce::String& theme);

    explicit UiSettings(juce::File file = defaultFile()) : file_(std::move(file)) {}
    // <userApplicationData>\PRAYROOM\settings.json (%APPDATA% on Windows); nothing is created.
    static juce::File defaultFile();
    const juce::File& file() const noexcept { return file_; }

    // Each reads the file: the key's value, or the default (1, "light", true) when the file is not there, is not a JSON
    // object, lacks the key, or holds something else.
    double scale() const;
    juce::String theme() const;
    bool hints() const;

    // Each writes its key and nothing else, through "<file>.tmp", adding "version": 1 when the file has none. A file that
    // is not there is made (with its folder). False, with nothing written, for a value outside the known ones, or when the
    // file is there but is not a JSON object (the other plug-ins' settings are not to be lost). No .tmp is left behind.
    bool setScale(double value);
    bool setTheme(const juce::String& theme);
    bool setHints(bool on);

private:
    bool write(const std::function<void(juce::DynamicObject&)>& change);
    juce::File file_;
};

} // namespace lesson::plugin
