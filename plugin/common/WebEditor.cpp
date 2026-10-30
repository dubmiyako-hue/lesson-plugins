#include "WebEditor.h"
#include "Bridge.h"
#include "WebAssetTable.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace lesson::plugin {

namespace {
const char* mimeFor(const juce::String& path) {
    const auto extension = path.fromLastOccurrenceOf(".", false, false).toLowerCase();
    if (extension == "html") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "text/javascript";
    if (extension == "woff") return "font/woff";
    if (extension == "woff2") return "font/woff2";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "json") return "application/json";
    if (extension == "png") return "image/png";
    return nullptr;
}

juce::WebBrowserComponent::Options webOptions(juce::Colour background) {
    using Options = juce::WebBrowserComponent::Options;
    const auto userData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                              .getChildFile("PRAYROOM").getChildFile("Lesson").getChildFile("WebView2");
    return Options()
        .withBackend(Options::Backend::webview2)
        .withWinWebView2Options(Options::WinWebView2()
                                    .withUserDataFolder(userData)
                                    .withStatusBarDisabled()
                                    .withBuiltInErrorPageDisabled()
                                    .withBackgroundColour(background)
                                    // The window is a fixed size, so the Windows text size must not enlarge the page
                                    // (third_party/juce-patches/0001).
                                    .withTextScaleIgnored())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider(&WebEditor::findResource);
}
}

// Navigation stays inside the embedded page: anything that is not under the resource provider's root is refused.
class WebEditor::Web final : public juce::WebBrowserComponent {
public:
    explicit Web(const Options& options) : juce::WebBrowserComponent(options) {}
    bool pageAboutToLoad(const juce::String& newURL) override {
        return newURL.startsWith(getResourceProviderRoot());
    }
    void pageFinishedLoading(const juce::String&) override { loaded_ = true; }
    // The bridge's events go out only once the page has loaded (before that the page has no listener to receive them).
    void emitEvent(const juce::Identifier& id, const juce::var& payload) {
        if (loaded_) emitEventIfBrowserIsVisible(id, payload);
    }

private:
    bool loaded_ = false;
};

std::optional<juce::WebBrowserComponent::Resource> WebEditor::findResource(const juce::String& request) {
    auto path = request.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
    if (path.startsWithChar('/')) path = path.substring(1); // the one "/" the provider's root leaves in front.
    if (path.isEmpty()) path = "index.html";
    if (path.containsAnyOf("\\:%") || path.contains("//") || path.endsWithChar('/')) return std::nullopt;
    for (const auto& segment : juce::StringArray::fromTokens(path, "/", ""))
        if (segment == "." || segment == "..") return std::nullopt;
    const auto* mime = mimeFor(path);
    if (mime == nullptr) return std::nullopt;
    const auto name = path.toRawUTF8();
    for (int i = 0; i < lesson_web::kAssetCount; ++i) {
        const auto& asset = lesson_web::kAssets[i];
        if (std::strcmp(asset.path, name) != 0) continue;
        juce::WebBrowserComponent::Resource resource;
        resource.data.resize(static_cast<std::size_t>(asset.size));
        if (asset.size > 0) std::memcpy(resource.data.data(), asset.data, static_cast<std::size_t>(asset.size));
        resource.mimeType = mime;
        return resource;
    }
    return std::nullopt;
}

juce::String WebEditor::fallbackText(const juce::String& pluginName) {
    return pluginName + " cannot show its screen because the Microsoft WebView2 runtime is not available. "
                        "The sound is not affected.";
}

juce::Colour WebEditor::backgroundFor(const juce::String& theme) {
    // --surface-000 of plugin/web/prayroom/tokens.css as 8-bit sRGB: light oklch(0.985 0.003 255),
    // dark oklch(0.215 0.006 255), studio oklch(0.209 0 0).
    if (theme == "dark") return juce::Colour(0x18, 0x1A, 0x1C);
    if (theme == "studio") return juce::Colour(0x18, 0x18, 0x18);
    return juce::Colour(0xF9, 0xFA, 0xFC);
}

WebEditor::WebEditor(LessonProcessor& processor, bool forceFallbackForTest, juce::File settingsFile)
    : juce::AudioProcessorEditor(processor), settings_(std::move(settingsFile)), background_(backgroundFor(settings_.theme())) {
    if (!forceFallbackForTest && juce::WebBrowserComponent::areOptionsSupported(webOptions(background_))) {
        // The bridge (its listeners, its timer, the taps) exists only when there is a page to talk to.
        bridge_ = std::make_unique<Bridge>(processor, settings_);
        const auto options = bridge_->addTo(webOptions(background_));
        options.getWinWebView2BackendOptions().getUserDataFolder().createDirectory();
        web_ = std::make_unique<Web>(options);
        bridge_->setSink([web = web_.get()](const juce::Identifier& id, const juce::var& payload) { web->emitEvent(id, payload); });
        addAndMakeVisible(*web_);
        web_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");
        bridge_->setScaleHooks({ [this] { return scaleState(); }, [this](double scale) { return requestScale(scale); } });
        bridge_->start();
    }
    if (web_ == nullptr) {
        note_ = std::make_unique<juce::Label>(juce::String(), fallbackText(processor.getName()));
        note_->setJustificationType(juce::Justification::centred);
        note_->setColour(juce::Label::textColourId, background_.contrasting(0.8f));
        note_->setFont(juce::FontOptions(16.0f));
        addAndMakeVisible(*note_);
    }
    setResizable(false, false);
    applyScale(openingScale(settings_.scale(), scaleState().choices));
}

WebEditor::~WebEditor() {
    if (bridge_ != nullptr) {
        bridge_->stop();
        bridge_->setSink({});
        bridge_->setScaleHooks({});
    }
}

std::vector<ScaleChoice> WebEditor::scaleChoices(juce::Rectangle<int> userArea) {
    std::vector<ScaleChoice> choices;
    for (const auto scale : UiSettings::kScales)
        choices.push_back({ scale, kWidth * scale + kScreenMargin <= userArea.getWidth()
                                       && kHeight * scale + kScreenMargin <= userArea.getHeight() });
    return choices;
}

double WebEditor::openingScale(double saved, const std::vector<ScaleChoice>& choices) {
    double scale = UiSettings::kScales.front();
    for (const auto& choice : choices)
        if (choice.enabled && choice.value <= saved + 1e-9) scale = std::max(scale, choice.value);
    return scale;
}

ScaleState WebEditor::scaleState() const {
    ScaleState state;
    state.scale = scale_;
    if (userAreaForTest_.has_value()) {
        state.choices = scaleChoices(*userAreaForTest_);
        return state;
    }
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect(getScreenBounds());
    if (display == nullptr) display = displays.getPrimaryDisplay();
    if (display != nullptr) {
        state.choices = scaleChoices(display->userBounds.toNearestInt());
    } else { // no screen to ask: nothing can be said not to fit
        for (const auto scale : UiSettings::kScales) state.choices.push_back({ scale, true });
    }
    return state;
}

ScaleState WebEditor::requestScale(double scale) {
    if (UiSettings::isKnownScale(scale)) {
        for (const auto& choice : scaleState().choices) {
            if (std::abs(choice.value - scale) >= 1e-9) continue;
            if (choice.enabled) {
                applyScale(choice.value);
                settings_.setScale(choice.value); // a settings file that cannot be written keeps the scale for this window only
            }
            break;
        }
    }
    return scaleState();
}

void WebEditor::applyScale(double scale) {
    scale_ = scale;
    if (web_ != nullptr) web_->setTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
    setSize(juce::roundToInt(kWidth * scale), juce::roundToInt(kHeight * scale));
}

void WebEditor::paint(juce::Graphics& g) {
    g.fillAll(background_);
}

void WebEditor::resized() {
    if (web_ != nullptr) web_->setBounds(0, 0, kWidth, kHeight);
    if (note_ != nullptr) note_->setBounds(getLocalBounds().reduced(48));
}

} // namespace lesson::plugin
