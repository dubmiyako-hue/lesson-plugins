#include "UiSettings.h"
#include <cmath>

namespace lesson::plugin {

namespace {
constexpr juce::int64 kMaxFileBytes = 1 << 20;
const juce::Identifier kVersion("version");
const juce::Identifier kScale("scale");
const juce::Identifier kTheme("theme");
const juce::Identifier kLesson("lesson");
const juce::Identifier kHint("hint");

// The file as a JSON object. `present` is whether the file exists; false from the function means it exists and cannot be used.
bool readObject(const juce::File& file, juce::var& object, bool& present) {
    present = file.existsAsFile();
    if (!present) {
        object = juce::var(new juce::DynamicObject());
        return true;
    }
    if (file.getSize() > kMaxFileBytes) return false;
    juce::var parsed;
    if (!juce::JSON::parse(file.loadFileAsString(), parsed).wasOk() || parsed.getDynamicObject() == nullptr) return false;
    object = parsed;
    return true;
}

// The file's object, or an empty one when it cannot be read.
juce::var readOrEmpty(const juce::File& file) {
    juce::var object;
    bool present = false;
    if (!readObject(file, object, present)) return juce::var(new juce::DynamicObject());
    return object;
}

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
}

bool UiSettings::isKnownScale(double value) noexcept {
    for (const auto known : kScales)
        if (std::abs(value - known) < 1e-9) return true;
    return false;
}

bool UiSettings::isKnownTheme(const juce::String& theme) {
    return theme == "light" || theme == "dark" || theme == "studio";
}

juce::File UiSettings::defaultFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PRAYROOM").getChildFile("settings.json");
}

double UiSettings::scale() const {
    const auto value = readOrEmpty(file_)[kScale];
    if (!isNumber(value)) return 1.0;
    const auto number = static_cast<double>(value);
    for (const auto known : kScales) // the table's own value, not the file's near miss
        if (std::abs(number - known) < 1e-9) return known;
    return 1.0;
}

juce::String UiSettings::theme() const {
    const auto value = readOrEmpty(file_)[kTheme];
    return value.isString() && isKnownTheme(value.toString()) ? value.toString() : juce::String("light");
}

bool UiSettings::hints() const {
    const auto value = readOrEmpty(file_)[kLesson][kHint];
    return value.isBool() ? static_cast<bool>(value) : true;
}

bool UiSettings::setScale(double value) {
    if (!isKnownScale(value)) return false;
    return write([value](juce::DynamicObject& object) { object.setProperty(kScale, value); });
}

bool UiSettings::setTheme(const juce::String& theme) {
    if (!isKnownTheme(theme)) return false;
    return write([theme](juce::DynamicObject& object) { object.setProperty(kTheme, theme); });
}

bool UiSettings::setHints(bool on) {
    return write([on](juce::DynamicObject& object) {
        auto lesson = object.getProperty(kLesson);
        if (lesson.getDynamicObject() == nullptr) { // absent, or not an object: ours to make
            lesson = juce::var(new juce::DynamicObject());
            object.setProperty(kLesson, lesson);
        }
        lesson.getDynamicObject()->setProperty(kHint, on);
    });
}

bool UiSettings::write(const std::function<void(juce::DynamicObject&)>& change) {
    juce::var object;
    bool present = false;
    if (!readObject(file_, object, present)) return false;
    auto& properties = *object.getDynamicObject();
    if (!properties.hasProperty(kVersion)) properties.setProperty(kVersion, 1);
    change(properties);
    if (!file_.getParentDirectory().createDirectory().wasOk()) return false;

    const auto text = juce::JSON::toString(object, false);
    const auto temporary = file_.getSiblingFile(file_.getFileName() + ".tmp");
    bool written = false;
    {
        juce::FileOutputStream out(temporary);
        if (out.openedOk()) {
            out.setPosition(0);
            written = out.truncate().wasOk() && out.writeText(text, false, false, "\n");
            out.flush();
            written = written && out.getStatus().wasOk();
        }
    }
    // replaceFileIn removes the temporary file when it succeeds; when it does not, the target is as it was.
    if (written && temporary.replaceFileIn(file_)) return true;
    if (temporary.existsAsFile()) temporary.deleteFile();
    return false;
}

} // namespace lesson::plugin
