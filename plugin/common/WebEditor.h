#pragma once
// The editor of both plug-ins: a shell around a WebView2 page (plugin/web/dist, embedded in the binary and served by a
// resource provider). It has no controls of its own. When WebView2 cannot be used it shows a one-label note and the
// plug-in still plays.
#include "LessonProcessor.h"
#include "UiSettings.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <optional>
#include <vector>

namespace lesson::plugin {

class Bridge;

class WebEditor final : public juce::AudioProcessorEditor {
public:
    // The page's size in CSS px (PRAYROOM's L window) and the editor's size at scale 1. At scale s the window is
    // 960s x 560s and the page is still 960 x 560: the web view is placed at (0, 0, kWidth, kHeight) and drawn with a
    // transform of s (the patched JUCE lays the page out at the component's size and renders it at the window's scale).
    static constexpr int kWidth = 960, kHeight = 560;
    // Room left round the window on its screen, in px, which a scale must keep (in width and in height).
    static constexpr int kScreenMargin = 60;
    // forceFallbackForTest: take the no-WebView2 path even when the runtime is there. Only the tests pass true.
    // settingsFile: where the window's settings are read and saved (a test gives a temporary file, never the real one).
    explicit WebEditor(LessonProcessor&, bool forceFallbackForTest = false, juce::File settingsFile = UiSettings::defaultFile());
    ~WebEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // The embedded file for a request path as the web view sends it ("/index.html"; the query and the fragment are
    // ignored). nullopt for a path that is not an embedded file, including anything with "..", a backslash, a colon,
    // a percent sign, an empty segment or a second leading "/". Needs no editor.
    static std::optional<juce::WebBrowserComponent::Resource> findResource(const juce::String& path);
    // The text of the note shown when the page cannot be shown.
    static juce::String fallbackText(const juce::String& pluginName);
    // The colour behind the page before it has painted: the theme's --surface-000.
    static juce::Colour backgroundFor(const juce::String& theme);

    // The four scales for a screen whose usable area is `userArea`: one is enabled when 960s x 560s plus kScreenMargin
    // in width and in height fits in it.
    static std::vector<ScaleChoice> scaleChoices(juce::Rectangle<int> userArea);
    // The scale to open at: the saved one when it is enabled in `choices`, else the largest enabled one below it, else
    // 1. The settings file is left as it is, so the saved scale comes back on a screen where it fits.
    static double openingScale(double saved, const std::vector<ScaleChoice>& choices);
    // The scale in use, and the choices for the screen the window is on (the primary screen when it is on none; all
    // enabled when there is no screen to ask).
    ScaleState scaleState() const;
    // The settings chose `scale`: applied and saved only when it is one of the four and enabled on this screen (else
    // nothing changes). Answers the state after.
    ScaleState requestScale(double scale);
    // A test's screen: the area that scaleState() uses in place of the real display's.
    void setUserAreaForTest(juce::Rectangle<int> userArea) { userAreaForTest_ = userArea; }
    bool showsPage() const noexcept { return web_ != nullptr; }

private:
    class Web;
    // The scale in use and the window's size, with no check of the screen.
    void applyScale(double scale);
    UiSettings settings_;
    juce::Colour background_;
    std::unique_ptr<Bridge> bridge_; // before web_: the web view calls into it, so it is destroyed after the view
    std::unique_ptr<Web> web_;
    std::unique_ptr<juce::Label> note_;
    double scale_ = 1.0;
    std::optional<juce::Rectangle<int>> userAreaForTest_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebEditor)
};

} // namespace lesson::plugin
