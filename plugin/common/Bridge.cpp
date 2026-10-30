#include "Bridge.h"
#include "LessonParameters.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace lesson::plugin {

namespace {
using juce::var;

var object(std::initializer_list<std::pair<const char*, var>> properties) {
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : properties) result->setProperty(name, value);
    return var(result);
}

// A float as the double of its shortest text ("3.8", not 3.7999999523162842), so that the page reads what the host shows.
var num(float value) {
    char text[40];
    const auto result = std::to_chars(text, text + sizeof(text) - 1, value);
    *result.ptr = '\0';
    return var(std::strtod(text, nullptr));
}

bool isNumber(const var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

const var& argument(const juce::Array<var>& args, int i) {
    static const var none;
    return i < args.size() ? args.getReference(i) : none;
}

juce::String base64(const float* data, std::size_t count) {
    return juce::Base64::toBase64(data, count * sizeof(float));
}
}

Bridge::Bridge(LessonProcessor& processor, UiSettings& settings)
    : processor_(processor), settings_(settings),
      changed_(std::make_unique<std::atomic<bool>[]>(processor.specs().size())),
      inGesture_(processor.specs().size(), false),
      samples_(SpectrumAnalyser::kFftSize), db_(SpectrumAnalyser::kBins),
      points_(CompPoints::kCapacity), pointFloats_(3 * CompPoints::kCapacity) {
    specOfParameter_.assign(static_cast<std::size_t>(processor.getParameters().size()), -1);
    for (std::size_t i = 0; i < processor.specs().size(); ++i) {
        changed_[i].store(false, std::memory_order_relaxed);
        auto& parameter = processor.parameter(i);
        const auto at = static_cast<std::size_t>(parameter.getParameterIndex());
        if (at < specOfParameter_.size()) specOfParameter_[at] = static_cast<int>(i);
        parameter.addListener(this);
    }
}

Bridge::~Bridge() {
    stop();
    for (std::size_t i = 0; i < processor_.specs().size(); ++i) processor_.parameter(i).removeListener(this);
}

void Bridge::setSink(Sink sink) { sink_ = std::move(sink); }

juce::WebBrowserComponent::Options Bridge::addTo(const juce::WebBrowserComponent::Options& base) {
    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;
    using Args = juce::Array<var>;
    return base
        .withNativeFunction("ready", [this](const Args&, Completion done) { done(ready()); })
        .withNativeFunction("setParam", [this](const Args& a, Completion done) {
            setParam(argument(a, 0), argument(a, 1), argument(a, 2));
            done(var());
        })
        .withNativeFunction("parseValue", [this](const Args& a, Completion done) { done(parseValue(argument(a, 0), argument(a, 1))); })
        .withNativeFunction("setScale", [this](const Args& a, Completion done) { done(setScale(argument(a, 0))); })
        .withNativeFunction("scales", [this](const Args&, Completion done) { done(scales()); })
        .withNativeFunction("setTheme", [this](const Args& a, Completion done) { setTheme(argument(a, 0)); done(var()); })
        .withNativeFunction("setHints", [this](const Args& a, Completion done) { setHints(argument(a, 0)); done(var()); });
}

void Bridge::start() {
    if (started_) return;
    started_ = true;
    // Points queued while no window was open are not this window's.
    while (auto* points = processor_.compPoints()) {
        if (points->take(points_.data(), static_cast<int>(points_.size())) == 0) break;
    }
    pre_.reset();
    post_.reset();
    lastBlocks_ = processor_.blocksProcessed();
    stillTicks_ = kStillTicks; // not playing until a block comes
    processor_.setScreenOpen(true);
    startTimerHz(kTimerHz);
}

void Bridge::stop() {
    stopTimer();
    endOpenGestures();
    if (!started_) return;
    started_ = false;
    processor_.setScreenOpen(false);
}

void Bridge::endOpenGestures() {
    for (std::size_t i = 0; i < inGesture_.size(); ++i) {
        if (!inGesture_[i]) continue;
        inGesture_[i] = false;
        processor_.parameter(i).endChangeGesture();
    }
}

void Bridge::setScaleHooks(ScaleHooks hooks) { scaleHooks_ = std::move(hooks); }

ScaleState Bridge::scaleState() const {
    if (scaleHooks_.state) return scaleHooks_.state();
    ScaleState fixed; // no editor: the scale is 1 and there is nothing else to choose
    fixed.choices.push_back({ 1.0, true });
    return fixed;
}

double Bridge::sampleRate() const {
    const auto rate = processor_.getSampleRate();
    return rate > 0 ? rate : 48000.0; // a host that has not prepared it yet: the plug-in's own default
}

var Bridge::ready() {
    auto* values = new juce::DynamicObject();
    auto* names = new juce::DynamicObject();
    for (std::size_t i = 0; i < processor_.specs().size(); ++i) {
        const auto& spec = processor_.specs()[i];
        const juce::Identifier id(asJuce(spec.id));
        values->setProperty(id, num(processor_.plainValue(i)));
        names->setProperty(id, asJuce(spec.name));
    }
    const auto scale = scales();
    return object({ { "plugin", var(juce::String(processor_.screenKey())) },
                    { "version", var(juce::String(JucePlugin_VersionString)) },
                    { "sampleRate", var(sampleRate()) },
                    { "values", var(values) },
                    { "names", var(names) },
                    { "makeup", var(processor_.makeupDb()) },
                    { "scale", scale["scale"] },
                    { "scales", scale["scales"] },
                    { "theme", var(settings_.theme()) },
                    { "hints", var(settings_.hints()) } });
}

std::size_t Bridge::indexOf(const var& id) const {
    const auto count = processor_.specs().size();
    if (!id.isString()) return count;
    const auto text = id.toString();
    for (std::size_t i = 0; i < count; ++i)
        if (asJuce(processor_.specs()[i].id) == text) return i;
    return count;
}

void Bridge::setParam(const var& id, const var& value, const var& phase) {
    const auto i = indexOf(id);
    if (i >= processor_.specs().size() || !phase.isString()) return;
    const auto kind = phase.toString();
    auto& parameter = processor_.parameter(i);
    const auto valid = isNumber(value) && std::isfinite(static_cast<double>(value));
    const auto write = [&] {
        const auto& range = parameter.getNormalisableRange();
        const auto plain = std::clamp(static_cast<double>(value), static_cast<double>(range.start), static_cast<double>(range.end));
        parameter.setValueNotifyingHost(std::clamp(parameter.convertTo0to1(static_cast<float>(plain)), 0.0f, 1.0f));
    };
    const auto begin = [&] {
        if (inGesture_[i]) return;
        inGesture_[i] = true;
        parameter.beginChangeGesture();
    };
    const auto end = [&] {
        if (!inGesture_[i]) return;
        inGesture_[i] = false;
        parameter.endChangeGesture();
    };
    if (kind == "end") {
        end();
        return;
    }
    if (!valid) return;
    if (kind == "begin") {
        begin();
        write();
    } else if (kind == "move") {
        write();
    } else if (kind == "complete") {
        begin();
        write();
        end();
    }
}

var Bridge::parseValue(const var& id, const var& text) {
    const auto i = indexOf(id);
    if (i >= processor_.specs().size() || !text.isString()) return var();
    const auto value = lesson::parseValue(processor_.specs()[i], text.toString().toStdString());
    return value.has_value() && std::isfinite(*value) ? var(*value) : var();
}

var Bridge::scales() {
    const auto state = scaleState();
    juce::Array<var> list;
    for (const auto& choice : state.choices)
        list.add(object({ { "value", var(choice.value) }, { "enabled", var(choice.enabled) } }));
    return object({ { "scale", var(state.scale) }, { "scales", var(list) } });
}

var Bridge::setScale(const var& scale) {
    if (scaleHooks_.set && isNumber(scale)) {
        const auto number = static_cast<double>(scale);
        if (std::isfinite(number)) scaleHooks_.set(number);
    }
    return scales();
}

void Bridge::setTheme(const var& theme) {
    if (theme.isString()) settings_.setTheme(theme.toString()); // not light, dark or studio: setTheme refuses it
}

void Bridge::setHints(const var& on) {
    if (on.isBool()) settings_.setHints(static_cast<bool>(on));
}

void Bridge::parameterValueChanged(int parameterIndex, float) {
    // Any thread, the audio thread included: an atomic mark and nothing else.
    if (parameterIndex < 0 || static_cast<std::size_t>(parameterIndex) >= specOfParameter_.size()) return;
    const auto i = specOfParameter_[static_cast<std::size_t>(parameterIndex)];
    if (i >= 0) changed_[static_cast<std::size_t>(i)].store(true, std::memory_order_relaxed);
}

void Bridge::emit(const char* event, const var& payload) {
    if (sink_) sink_(juce::Identifier(event), payload);
}

void Bridge::tick() {
    // The parameters that changed since the last tick, in one event, with the values they have now.
    juce::DynamicObject::Ptr changed;
    for (std::size_t i = 0; i < processor_.specs().size(); ++i) {
        if (!changed_[i].exchange(false, std::memory_order_relaxed)) continue;
        if (changed == nullptr) changed = new juce::DynamicObject();
        changed->setProperty(juce::Identifier(asJuce(processor_.specs()[i].id)), num(processor_.plainValue(i)));
    }
    if (changed != nullptr) emit("params", var(changed.get()));

    const auto blocks = processor_.blocksProcessed();
    stillTicks_ = blocks != lastBlocks_ ? 0 : std::min(stillTicks_ + 1, kStillTicks);
    lastBlocks_ = blocks;
    const bool playing = stillTicks_ < kStillTicks;
    const auto rate = sampleRate();
    emit("tick", object({ { "makeup", var(processor_.makeupDb()) }, { "playing", var(playing) }, { "sampleRate", var(rate) } }));

    if (playing) sendSpectrum(rate);
    sendPoints();
}

void Bridge::sendSpectrum(double rate) {
    auto* pre = processor_.spectrumPre();
    auto* post = processor_.spectrumPost();
    if (pre == nullptr || post == nullptr) return;
    const auto bins = SpectrumAnalyser::binsFor(rate);
    pre->latest(samples_.data(), SpectrumAnalyser::kFftSize);
    pre_.analyse(samples_.data(), kSpectrumSmoothing);
    pre_.decibels(db_.data(), bins);
    const auto preText = base64(db_.data(), static_cast<std::size_t>(bins));
    post->latest(samples_.data(), SpectrumAnalyser::kFftSize);
    post_.analyse(samples_.data(), kSpectrumSmoothing);
    post_.decibels(db_.data(), bins);
    const auto postText = base64(db_.data(), static_cast<std::size_t>(bins));
    emit("spectrum", object({ { "pre", var(preText) }, { "post", var(postText) },
                              { "bin", var(rate / SpectrumAnalyser::kFftSize) }, { "fftSize", var(SpectrumAnalyser::kFftSize) } }));
}

void Bridge::sendPoints() {
    auto* points = processor_.compPoints();
    if (points == nullptr) return;
    const auto n = points->take(points_.data(), static_cast<int>(points_.size()));
    if (n == 0) return;
    for (int k = 0; k < n; ++k) {
        pointFloats_[3 * static_cast<std::size_t>(k)] = points_[static_cast<std::size_t>(k)].in;
        pointFloats_[3 * static_cast<std::size_t>(k) + 1] = points_[static_cast<std::size_t>(k)].out;
        pointFloats_[3 * static_cast<std::size_t>(k) + 2] = points_[static_cast<std::size_t>(k)].gr;
    }
    emit("points", var(base64(pointFloats_.data(), 3 * static_cast<std::size_t>(n))));
}

} // namespace lesson::plugin
