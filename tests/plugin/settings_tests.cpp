// The window's settings (plugin/common/UiSettings.h) and what the editor does with them, from the headers and the page's
// host.ts: the file's reading and writing, Bridge's ready/parseValue/setTheme/setHints, and the window scale's choices.
// Every settings file is a temporary one (never the real <AppData>\PRAYROOM\settings.json): UiSettings is always given a
// path, and WebEditor is always made with the test's no-WebView2 path and a temporary file.
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <common/Bridge.h>
#include <common/UiSettings.h>
#include <common/WebEditor.h>
#include <eq/EqProcessor.h>
#include <cmath>
#include <string>
#include <vector>

using namespace lesson::plugin;

namespace {
// A temporary folder, removed with everything in it when the test ends.
struct TempDir {
    juce::File dir;
    TempDir()
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("lesson_settings_test", "", false)) {
        REQUIRE(dir.createDirectory().wasOk());
    }
    ~TempDir() { dir.deleteRecursively(); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    juce::File file() const { return dir.getChildFile("settings.json"); }
    // Two folders that are not there yet.
    juce::File nested() const { return dir.getChildFile("a").getChildFile("b").getChildFile("settings.json"); }
};

void put(const juce::File& file, const juce::String& text) { REQUIRE(file.replaceWithText(text)); }
void putBytes(const juce::File& file, const std::string& bytes) { REQUIRE(file.replaceWithData(bytes.data(), bytes.size())); }
juce::MemoryBlock bytesOf(const juce::File& file) {
    juce::MemoryBlock block;
    REQUIRE(file.loadFileAsData(block));
    return block;
}
juce::var parsed(const juce::File& file) { return juce::JSON::parse(file); }
juce::String text(const juce::var& v) { return juce::JSON::toString(v, true); }
juce::var property(const juce::var& object, const char* name) { return object.getProperty(juce::Identifier(name), juce::var()); }
bool has(const juce::var& object, const char* name) { return object.hasProperty(juce::Identifier(name)); }
juce::File tmpOf(const juce::File& file) { return file.getSiblingFile(file.getFileName() + ".tmp"); }

// A JSON object file of exactly `size` bytes that is valid JSON: {"scale":1.5,"pad":"xxx..."}.
std::string objectOfSize(std::size_t size) {
    const std::string head = "{\"scale\":1.5,\"pad\":\"";
    const std::string tail = "\"}";
    REQUIRE(size > head.size() + tail.size());
    return head + std::string(size - head.size() - tail.size(), 'x') + tail;
}
constexpr std::size_t kMegabyte = 1048576;

// ---- AC-C: UiSettings's file ----

TEST_CASE("[settings] reading a file that is not there gives the defaults, and makes nothing") {
    TempDir temp;
    const UiSettings settings(temp.file());
    CHECK(settings.scale() == 1.0);
    CHECK(settings.theme() == "light");
    CHECK(settings.hints() == true);
    CHECK_FALSE(temp.file().exists());
    CHECK_FALSE(temp.nested().getParentDirectory().exists());
}

TEST_CASE("[settings] reading a file that is not a JSON object, lacks the keys or holds other types gives the defaults") {
    const char* const contents[] = {
        "not json at all",
        "{\"scale\":",
        "[1]",
        "[]",
        "3",
        "null",
        "\"studio\"",
        "{}",
        "{\"unrelated\":1}",
        "{\"scale\":\"big\"}",
        "{\"theme\":3}",
        "{\"lesson\":{\"hint\":\"yes\"}}",
        "{\"lesson\":true}",
        "{\"lesson\":{}}",
        "{\"scale\":\"big\",\"theme\":3,\"lesson\":{\"hint\":\"yes\"}}",
    };
    for (const char* content : contents) {
        INFO("file: " << content);
        TempDir temp;
        put(temp.file(), content);
        const UiSettings settings(temp.file());
        CHECK(settings.scale() == 1.0);
        CHECK(settings.theme() == "light");
        CHECK(settings.hints() == true);
    }
}

TEST_CASE("[settings] a scale that is not one of the four reads as 100 %") {
    const char* const contents[] = { "{\"scale\":1.3}", "{\"scale\":0}", "{\"scale\":-1.5}", "{\"scale\":3}" };
    for (const char* content : contents) {
        INFO("file: " << content);
        TempDir temp;
        put(temp.file(), content);
        CHECK(UiSettings(temp.file()).scale() == 1.0);
    }
}

TEST_CASE("[settings] the four scales, the three themes and the Hint read back as they are") {
    struct Case {
        const char* content;
        double scale;
    };
    const Case scales[] = { { "{\"scale\":1.0}", 1.0 }, { "{\"scale\":1.25}", 1.25 }, { "{\"scale\":1.5}", 1.5 },
                            { "{\"scale\":2.0}", 2.0 }, { "{\"scale\":2}", 2.0 } };
    for (const auto& c : scales) {
        INFO("file: " << c.content);
        TempDir temp;
        put(temp.file(), c.content);
        CHECK(UiSettings(temp.file()).scale() == c.scale);
    }
    for (const char* theme : { "light", "dark", "studio" }) {
        TempDir temp;
        put(temp.file(), juce::String("{\"theme\":\"") + theme + "\"}");
        CHECK(UiSettings(temp.file()).theme() == theme);
    }
    TempDir temp;
    put(temp.file(), "{\"lesson\":{\"hint\":false}}");
    CHECK(UiSettings(temp.file()).hints() == false);
    put(temp.file(), "{\"lesson\":{\"hint\":true}}");
    CHECK(UiSettings(temp.file()).hints() == true);
}

TEST_CASE("[settings] each key is read on its own: one key's wrong type does not spoil the others") {
    TempDir temp;
    put(temp.file(), "{\"scale\":\"big\",\"theme\":\"studio\",\"lesson\":{\"hint\":false}}");
    const UiSettings settings(temp.file());
    CHECK(settings.scale() == 1.0);
    CHECK(settings.theme() == "studio");
    CHECK(settings.hints() == false);

    put(temp.file(), "{\"scale\":1.5,\"theme\":3,\"lesson\":{\"hint\":false}}");
    CHECK(settings.scale() == 1.5);
    CHECK(settings.theme() == "light");
    CHECK(settings.hints() == false);

    put(temp.file(), "{\"scale\":2,\"theme\":\"dark\",\"lesson\":true}");
    CHECK(settings.scale() == 2.0);
    CHECK(settings.theme() == "dark");
    CHECK(settings.hints() == true);
}

TEST_CASE("[settings] a file over 1 MB is not used, even when it is valid JSON; exactly 1 MB is") {
    {
        TempDir temp;
        putBytes(temp.file(), objectOfSize(kMegabyte + 1));
        REQUIRE(temp.file().getSize() == static_cast<juce::int64>(kMegabyte + 1));
        REQUIRE(parsed(temp.file()).isObject()); // the content itself is fine
        UiSettings settings(temp.file());
        CHECK(settings.scale() == 1.0); // the file says 1.5
        const auto before = bytesOf(temp.file());
        CHECK_FALSE(settings.setScale(2.0));
        CHECK_FALSE(settings.setTheme("dark"));
        CHECK_FALSE(settings.setHints(false));
        CHECK(bytesOf(temp.file()) == before);
        CHECK_FALSE(tmpOf(temp.file()).exists());
    }
    {
        TempDir temp;
        putBytes(temp.file(), objectOfSize(kMegabyte));
        REQUIRE(temp.file().getSize() == static_cast<juce::int64>(kMegabyte));
        CHECK(UiSettings(temp.file()).scale() == 1.5);
    }
}

TEST_CASE("[settings] the known scales and themes") {
    CHECK(UiSettings::isKnownScale(1.0));
    CHECK(UiSettings::isKnownScale(1.25));
    CHECK(UiSettings::isKnownScale(1.5));
    CHECK(UiSettings::isKnownScale(2.0));
    CHECK_FALSE(UiSettings::isKnownScale(1.3));
    CHECK_FALSE(UiSettings::isKnownScale(0.0));
    CHECK_FALSE(UiSettings::isKnownScale(-1.0));
    CHECK_FALSE(UiSettings::isKnownScale(3.0));
    CHECK(UiSettings::isKnownTheme("light"));
    CHECK(UiSettings::isKnownTheme("dark"));
    CHECK(UiSettings::isKnownTheme("studio"));
    CHECK_FALSE(UiSettings::isKnownTheme("pink"));
    CHECK_FALSE(UiSettings::isKnownTheme(""));
}

TEST_CASE("[settings] writing a key keeps every other key as it was") {
    const juce::String original = "{\"version\":7,\"scale\":1.25,\"theme\":\"dark\",\"future\":{\"a\":[1,2,3],\"b\":null},"
                                  "\"name\":\"x\",\"lesson\":{\"hint\":true,\"other\":\"keep me\",\"deep\":{\"n\":1}}}";
    const auto expectKept = [](const juce::var& v) {
        CHECK(static_cast<int>(property(v, "version")) == 7);
        CHECK(text(property(v, "future")) == text(juce::JSON::parse("{\"a\":[1,2,3],\"b\":null}")));
        CHECK(property(v, "name").toString() == "x");
        CHECK(property(property(v, "lesson"), "other").toString() == "keep me");
        CHECK(text(property(property(v, "lesson"), "deep")) == text(juce::JSON::parse("{\"n\":1}")));
    };
    {
        TempDir temp;
        put(temp.file(), original);
        CHECK(UiSettings(temp.file()).setScale(2.0));
        const auto v = parsed(temp.file());
        REQUIRE(v.isObject());
        CHECK(static_cast<double>(property(v, "scale")) == 2.0);
        CHECK(property(v, "theme").toString() == "dark");
        CHECK(static_cast<bool>(property(property(v, "lesson"), "hint")) == true);
        expectKept(v);
    }
    {
        TempDir temp;
        put(temp.file(), original);
        CHECK(UiSettings(temp.file()).setTheme("studio"));
        const auto v = parsed(temp.file());
        REQUIRE(v.isObject());
        CHECK(property(v, "theme").toString() == "studio");
        CHECK(static_cast<double>(property(v, "scale")) == 1.25);
        CHECK(static_cast<bool>(property(property(v, "lesson"), "hint")) == true);
        expectKept(v);
    }
    {
        TempDir temp;
        put(temp.file(), original);
        CHECK(UiSettings(temp.file()).setHints(false));
        const auto v = parsed(temp.file());
        REQUIRE(v.isObject());
        CHECK(static_cast<bool>(property(property(v, "lesson"), "hint")) == false);
        CHECK(static_cast<double>(property(v, "scale")) == 1.25);
        CHECK(property(v, "theme").toString() == "dark");
        expectKept(v);
    }
}

TEST_CASE("[settings] writing one key adds no other key but the version") {
    TempDir temp;
    put(temp.file(), "{}");
    REQUIRE(UiSettings(temp.file()).setScale(1.5));
    const auto v = parsed(temp.file());
    REQUIRE(v.isObject());
    CHECK(static_cast<double>(property(v, "scale")) == 1.5);
    CHECK_FALSE(has(v, "theme"));
    CHECK_FALSE(has(v, "lesson"));
    CHECK(v.getDynamicObject()->getProperties().size() == 2); // version and scale
}

TEST_CASE("[settings] a file without a version gets version 1, a file with one keeps its value") {
    {
        TempDir temp;
        put(temp.file(), "{\"theme\":\"dark\"}");
        REQUIRE(UiSettings(temp.file()).setScale(1.25));
        const auto v = parsed(temp.file());
        REQUIRE(has(v, "version"));
        CHECK(static_cast<int>(property(v, "version")) == 1);
        CHECK(property(v, "theme").toString() == "dark");
    }
    {
        TempDir temp;
        put(temp.file(), "{\"version\":7}");
        REQUIRE(UiSettings(temp.file()).setHints(false));
        CHECK(static_cast<int>(property(parsed(temp.file()), "version")) == 7);
    }
    {
        TempDir temp;
        put(temp.file(), "{\"version\":0,\"scale\":1.5}");
        REQUIRE(UiSettings(temp.file()).setTheme("dark"));
        CHECK(static_cast<int>(property(parsed(temp.file()), "version")) == 0);
    }
}

TEST_CASE("[settings] writing makes the file when it is not there, and its folders") {
    {
        TempDir temp;
        REQUIRE_FALSE(temp.file().exists());
        CHECK(UiSettings(temp.file()).setTheme("studio"));
        const auto v = parsed(temp.file());
        REQUIRE(v.isObject());
        CHECK(property(v, "theme").toString() == "studio");
        CHECK(static_cast<int>(property(v, "version")) == 1);
    }
    for (int which = 0; which < 3; ++which) {
        INFO("which " << which);
        TempDir temp;
        const auto file = temp.nested();
        REQUIRE_FALSE(file.getParentDirectory().exists());
        UiSettings settings(file);
        const bool ok = which == 0 ? settings.setScale(2.0) : which == 1 ? settings.setTheme("dark") : settings.setHints(false);
        CHECK(ok);
        CHECK(file.existsAsFile());
        const auto v = parsed(file);
        REQUIRE(v.isObject());
        CHECK(static_cast<int>(property(v, "version")) == 1);
        CHECK_FALSE(tmpOf(file).exists());
        if (which == 0) CHECK(settings.scale() == 2.0);
        if (which == 1) CHECK(settings.theme() == "dark");
        if (which == 2) CHECK(settings.hints() == false);
    }
}

TEST_CASE("[settings] setHints replaces a lesson that is not an object with one, and writes hint") {
    const char* const contents[] = { "{\"lesson\":true}", "{\"lesson\":\"x\"}", "{\"lesson\":3}", "{\"lesson\":[1,2]}", "{\"lesson\":null}" };
    for (const char* content : contents) {
        INFO("file: " << content);
        TempDir temp;
        put(temp.file(), content);
        UiSettings settings(temp.file());
        REQUIRE(settings.setHints(false));
        const auto v = parsed(temp.file());
        REQUIRE(v.isObject());
        const auto lesson = property(v, "lesson");
        REQUIRE(lesson.getDynamicObject() != nullptr);
        CHECK(has(lesson, "hint"));
        CHECK(static_cast<bool>(property(lesson, "hint")) == false);
        CHECK(settings.hints() == false);
    }
}

TEST_CASE("[settings] no .tmp is left behind, after a write or after a refused write") {
    TempDir temp;
    UiSettings settings(temp.file());
    CHECK(settings.setScale(1.5));
    CHECK_FALSE(tmpOf(temp.file()).exists());
    CHECK(settings.setTheme("dark"));
    CHECK_FALSE(tmpOf(temp.file()).exists());
    CHECK(settings.setHints(false));
    CHECK_FALSE(tmpOf(temp.file()).exists());
    CHECK(settings.setScale(2.0));
    CHECK_FALSE(tmpOf(temp.file()).exists());
    CHECK_FALSE(settings.setScale(1.3));
    CHECK_FALSE(tmpOf(temp.file()).exists());
    CHECK_FALSE(settings.setTheme("pink"));
    CHECK_FALSE(tmpOf(temp.file()).exists());

    // A folder of its own: nothing else but the settings file is in it.
    CHECK(temp.dir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}

TEST_CASE("[settings] a scale that is not one of the four, or a theme that is not one of the three, writes nothing") {
    for (const double scale : { 1.3, 0.0, -1.0, 2.5, 3.0 }) {
        INFO("scale " << scale);
        TempDir temp;
        CHECK_FALSE(UiSettings(temp.file()).setScale(scale));
        CHECK_FALSE(temp.file().exists());
        CHECK_FALSE(tmpOf(temp.file()).exists());

        const juce::String original = "{\"version\":3,\"scale\":1.5,\"x\":[1, 2]}";
        put(temp.file(), original);
        const auto before = bytesOf(temp.file());
        CHECK_FALSE(UiSettings(temp.file()).setScale(scale));
        CHECK(bytesOf(temp.file()) == before);
        CHECK_FALSE(tmpOf(temp.file()).exists());
    }
    for (const char* theme : { "pink", "" }) {
        INFO("theme '" << theme << "'");
        TempDir temp;
        CHECK_FALSE(UiSettings(temp.file()).setTheme(theme));
        CHECK_FALSE(temp.file().exists());
        CHECK_FALSE(tmpOf(temp.file()).exists());

        const juce::String original = "{\"version\":3,\"theme\":\"dark\",\"x\":[1, 2]}";
        put(temp.file(), original);
        const auto before = bytesOf(temp.file());
        UiSettings settings(temp.file());
        CHECK_FALSE(settings.setTheme(theme));
        CHECK(bytesOf(temp.file()) == before);
        CHECK(settings.theme() == "dark");
    }
}

TEST_CASE("[settings] a file that is not a JSON object is left as it is, and every write answers false") {
    const char* const contents[] = { "{\"scale\":", "not json at all", "[1]", "[]", "3", "\"studio\"", "[{\"scale\":1.5}]" };
    for (const char* content : contents) {
        INFO("file: " << content);
        TempDir temp;
        put(temp.file(), content);
        const auto before = bytesOf(temp.file());
        UiSettings settings(temp.file());
        CHECK_FALSE(settings.setScale(1.5));
        CHECK_FALSE(settings.setTheme("dark"));
        CHECK_FALSE(settings.setHints(false));
        CHECK(bytesOf(temp.file()) == before);
        CHECK_FALSE(tmpOf(temp.file()).exists());
    }
}

TEST_CASE("[settings] what was written reads back from a new UiSettings on the same path") {
    TempDir temp;
    {
        UiSettings writer(temp.nested());
        REQUIRE(writer.setScale(1.25));
        REQUIRE(writer.setTheme("studio"));
        REQUIRE(writer.setHints(false));
    }
    const UiSettings reader(temp.nested());
    CHECK(reader.scale() == 1.25);
    CHECK(reader.theme() == "studio");
    CHECK(reader.hints() == false);

    for (const double scale : UiSettings::kScales) {
        UiSettings writer(temp.file());
        REQUIRE(writer.setScale(scale));
        CHECK(UiSettings(temp.file()).scale() == scale);
    }
    for (const char* theme : { "light", "dark", "studio" }) {
        UiSettings writer(temp.file());
        REQUIRE(writer.setTheme(theme));
        CHECK(UiSettings(temp.file()).theme() == theme);
    }
    for (const bool on : { false, true, false }) {
        UiSettings writer(temp.file());
        REQUIRE(writer.setHints(on));
        CHECK(UiSettings(temp.file()).hints() == on);
    }
    // The three keys do not undo each other.
    UiSettings writer(temp.file());
    REQUIRE(writer.setScale(2.0));
    REQUIRE(writer.setTheme("dark"));
    REQUIRE(writer.setHints(true));
    REQUIRE(writer.setScale(1.5));
    CHECK(writer.scale() == 1.5);
    CHECK(writer.theme() == "dark");
    CHECK(writer.hints() == true);
}

// ---- AC-D: Bridge and the window scale ----

// A processor and a Bridge on a temporary settings file. The file is not made.
template <typename Processor>
struct Rig {
    Processor processor;
    TempDir temp;
    juce::File file { temp.file() };
    UiSettings settings { file };
    Bridge bridge { processor, settings };

    std::vector<float> plainValues() const {
        std::vector<float> values;
        for (std::size_t i = 0; i < processor.specs().size(); ++i) values.push_back(processor.plainValue(i));
        return values;
    }
};

template <typename Processor>
void readyHasEverything(const char* plugin) {
    Rig<Processor> rig;
    rig.processor.prepareToPlay(44100.0, 512);
    // Moves a few parameters off their defaults so that `values` is not just the defaults.
    for (const auto* id : { "b1_freq", "comp_ratio", "b1_gain" }) {
        if (auto* p = rig.processor.parameters().getParameter(id)) p->setValueNotifyingHost(0.73f);
    }
    const auto ready = rig.bridge.ready();
    REQUIRE(ready.isObject());
    for (const char* key : { "plugin", "version", "sampleRate", "values", "names", "makeup", "scale", "scales", "theme", "hints" }) {
        CHECK_MESSAGE(has(ready, key), key);
    }
    CHECK(property(ready, "plugin").toString() == plugin);
    CHECK(property(ready, "version").isString());
    CHECK(property(ready, "version").toString().isNotEmpty());
    CHECK(static_cast<double>(property(ready, "sampleRate")) == doctest::Approx(44100.0));
    CHECK(static_cast<double>(property(ready, "makeup")) == doctest::Approx(rig.processor.makeupDb()));

    const auto specs = rig.processor.specs();
    const auto values = property(ready, "values");
    const auto names = property(ready, "names");
    REQUIRE(values.getDynamicObject() != nullptr);
    REQUIRE(names.getDynamicObject() != nullptr);
    CHECK(static_cast<std::size_t>(values.getDynamicObject()->getProperties().size()) == specs.size());
    CHECK(static_cast<std::size_t>(names.getDynamicObject()->getProperties().size()) == specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const juce::String id(specs[i].id.data(), specs[i].id.size());
        INFO("parameter " << id);
        REQUIRE(values.hasProperty(juce::Identifier(id)));
        REQUIRE(names.hasProperty(juce::Identifier(id)));
        CHECK(static_cast<double>(values.getProperty(juce::Identifier(id), juce::var())) == doctest::Approx(rig.processor.plainValue(i)));
        CHECK(names.getProperty(juce::Identifier(id), juce::var()).toString()
              == juce::String(specs[i].name.data(), specs[i].name.size()));
    }
}

TEST_CASE("[settings] ready has the plug-in, version, rate, values, names, makeup, scale, scales, theme and hints (Lesson EQ)") {
    readyHasEverything<EqProcessor>("eq");
}
TEST_CASE("[settings] ready has the plug-in, version, rate, values, names, makeup, scale, scales, theme and hints (Lesson Comp)") {
    readyHasEverything<CompProcessor>("comp");
}

template <typename Processor>
void readyWithoutScaleHooks() {
    Rig<Processor> rig;
    const auto check = [](const juce::var& state) {
        CHECK(static_cast<double>(property(state, "scale")) == 1.0);
        const auto scales = property(state, "scales");
        REQUIRE(scales.isArray());
        REQUIRE(scales.size() == 1);
        CHECK(static_cast<double>(property(scales[0], "value")) == 1.0);
        CHECK(static_cast<bool>(property(scales[0], "enabled")) == true);
    };
    const auto ready = rig.bridge.ready();
    check(ready);
    check(rig.bridge.scales());
    // Without the editor's hooks nothing can change the scale: the request is answered with the same state.
    check(rig.bridge.setScale(juce::var(1.5)));
    check(rig.bridge.setScale(juce::var(2.0)));
    check(rig.bridge.setScale(juce::var(1.3)));
    check(rig.bridge.setScale(juce::var("big")));
    check(rig.bridge.ready());
    CHECK_FALSE(rig.file.exists());
    CHECK(rig.settings.scale() == 1.0);
}

TEST_CASE("[settings] without scale hooks the scale is 1, the only choice is 100 % enabled, and setScale changes nothing (Lesson EQ)") {
    readyWithoutScaleHooks<EqProcessor>();
}
TEST_CASE("[settings] without scale hooks the scale is 1, the only choice is 100 % enabled, and setScale changes nothing (Lesson Comp)") {
    readyWithoutScaleHooks<CompProcessor>();
}

TEST_CASE("[settings] ready's theme and hints are the file's, or light and true when there is no file") {
    {
        Rig<EqProcessor> rig;
        const auto ready = rig.bridge.ready();
        CHECK(property(ready, "theme").toString() == "light");
        CHECK(static_cast<bool>(property(ready, "hints")) == true);
        CHECK(property(ready, "hints").isBool());
    }
    {
        TempDir temp;
        put(temp.file(), "{\"theme\":\"studio\",\"lesson\":{\"hint\":false},\"scale\":1.5}");
        CompProcessor processor;
        UiSettings settings(temp.file());
        Bridge bridge(processor, settings);
        const auto ready = bridge.ready();
        CHECK(property(ready, "theme").toString() == "studio");
        CHECK(static_cast<bool>(property(ready, "hints")) == false);
        // No editor, so the file's scale does not apply to the Bridge's own state.
        CHECK(static_cast<double>(property(ready, "scale")) == 1.0);
    }
}

TEST_CASE("[settings] ready's values follow the parameters") {
    Rig<EqProcessor> rig;
    const auto before = rig.bridge.ready();
    auto* gain = rig.processor.parameters().getParameter("b1_gain");
    REQUIRE(gain != nullptr);
    gain->setValueNotifyingHost(1.0f);
    const auto after = rig.bridge.ready();
    const auto changed = static_cast<double>(property(property(after, "values"), "b1_gain"));
    CHECK(changed != static_cast<double>(property(property(before, "values"), "b1_gain")));
    CHECK(changed == doctest::Approx(rig.processor.parameters().getParameterRange("b1_gain").end));
}

TEST_CASE("[settings] parseValue reads the typed text as the plug-in does (Lesson EQ)") {
    Rig<EqProcessor> rig;
    const auto valuesBefore = rig.plainValues();
    const auto k = rig.bridge.parseValue(juce::var("b1_freq"), juce::var("2.5k"));
    CHECK_FALSE(k.isVoid());
    CHECK(static_cast<double>(k) == doctest::Approx(2500.0));
    const auto hz = rig.bridge.parseValue(juce::var("b1_freq"), juce::var("440 Hz"));
    CHECK_FALSE(hz.isVoid());
    CHECK(static_cast<double>(hz) == doctest::Approx(440.0));

    CHECK(rig.bridge.parseValue(juce::var("b1_freq"), juce::var("abc")).isVoid());
    CHECK(rig.bridge.parseValue(juce::var("no_such_parameter"), juce::var("2.5k")).isVoid());
    CHECK(rig.bridge.parseValue(juce::var(""), juce::var("2.5k")).isVoid());
    CHECK(rig.bridge.parseValue(juce::var(), juce::var("2.5k")).isVoid());
    CHECK(rig.bridge.parseValue(juce::var(7), juce::var("2.5k")).isVoid());
    CHECK(rig.bridge.parseValue(juce::var("b1_freq"), juce::var(2500)).isVoid());
    CHECK(rig.bridge.parseValue(juce::var("b1_freq"), juce::var(2500.0)).isVoid());
    CHECK(rig.bridge.parseValue(juce::var("b1_freq"), juce::var(true)).isVoid());
    CHECK(rig.bridge.parseValue(juce::var("b1_freq"), juce::var()).isVoid());
    // Reading is not setting.
    CHECK(rig.plainValues() == valuesBefore);
}

TEST_CASE("[settings] parseValue answers null for an id the plug-in does not have (Lesson Comp)") {
    Rig<CompProcessor> rig;
    const auto valuesBefore = rig.plainValues();
    CHECK(rig.bridge.parseValue(juce::var("b1_freq"), juce::var("2.5k")).isVoid()); // Lesson EQ's parameter
    CHECK(rig.bridge.parseValue(juce::var("nope"), juce::var("1")).isVoid());
    CHECK(rig.bridge.parseValue(juce::var(), juce::var()).isVoid());
    // A parameter it has, with text that is not a value.
    const auto id = rig.processor.specs().back().id;
    CHECK(rig.bridge.parseValue(juce::var(juce::String(id.data(), id.size())), juce::var("abc")).isVoid());
    CHECK(rig.plainValues() == valuesBefore);
}

TEST_CASE("[settings] setTheme and setHints are saved, and show in a new UiSettings and in a new Bridge's ready") {
    Rig<EqProcessor> rig;
    rig.bridge.setTheme(juce::var("dark"));
    rig.bridge.setHints(juce::var(false));
    CHECK(rig.file.existsAsFile());
    CHECK(rig.settings.theme() == "dark");
    CHECK(rig.settings.hints() == false);
    {
        const auto ready = rig.bridge.ready();
        CHECK(property(ready, "theme").toString() == "dark");
        CHECK(static_cast<bool>(property(ready, "hints")) == false);
    }
    const UiSettings fresh(rig.file);
    CHECK(fresh.theme() == "dark");
    CHECK(fresh.hints() == false);
    {
        CompProcessor other;
        UiSettings otherSettings(rig.file);
        Bridge otherBridge(other, otherSettings);
        const auto ready = otherBridge.ready();
        CHECK(property(ready, "theme").toString() == "dark");
        CHECK(static_cast<bool>(property(ready, "hints")) == false);
    }
    rig.bridge.setTheme(juce::var("studio"));
    rig.bridge.setHints(juce::var(true));
    CHECK(UiSettings(rig.file).theme() == "studio");
    CHECK(UiSettings(rig.file).hints() == true);
    rig.bridge.setTheme(juce::var("light"));
    CHECK(UiSettings(rig.file).theme() == "light");
    CHECK(UiSettings(rig.file).hints() == true);
    CHECK_FALSE(tmpOf(rig.file).exists());
}

TEST_CASE("[settings] setTheme and setHints with a value that is not allowed change nothing") {
    Rig<EqProcessor> rig;
    // Nothing saved yet: nothing is made.
    rig.bridge.setTheme(juce::var("pink"));
    rig.bridge.setTheme(juce::var(3));
    rig.bridge.setTheme(juce::var(""));
    rig.bridge.setTheme(juce::var());
    rig.bridge.setHints(juce::var("no"));
    rig.bridge.setHints(juce::var());
    CHECK_FALSE(rig.file.exists());
    CHECK_FALSE(tmpOf(rig.file).exists());
    {
        const auto ready = rig.bridge.ready();
        CHECK(property(ready, "theme").toString() == "light");
        CHECK(static_cast<bool>(property(ready, "hints")) == true);
    }

    // Something saved: it stays, byte for byte.
    rig.bridge.setTheme(juce::var("dark"));
    rig.bridge.setHints(juce::var(false));
    const auto before = bytesOf(rig.file);
    rig.bridge.setTheme(juce::var("pink"));
    rig.bridge.setTheme(juce::var(3));
    rig.bridge.setTheme(juce::var(""));
    rig.bridge.setHints(juce::var("no"));
    CHECK(bytesOf(rig.file) == before);
    CHECK(rig.settings.theme() == "dark");
    CHECK(rig.settings.hints() == false);
    const auto ready = rig.bridge.ready();
    CHECK(property(ready, "theme").toString() == "dark");
    CHECK(static_cast<bool>(property(ready, "hints")) == false);
}

template <typename Processor>
void settingsStayOutOfTheSession() {
    Rig<Processor> rig;
    juce::MemoryBlock first;
    juce::MemoryBlock second;
    rig.processor.getStateInformation(first);
    rig.processor.getStateInformation(second);
    REQUIRE(first == second); // the state is stable when nothing changes
    rig.bridge.setTheme(juce::var("studio"));
    rig.bridge.setHints(juce::var(false));
    juce::MemoryBlock after;
    rig.processor.getStateInformation(after);
    CHECK(after == first);
    CHECK(rig.file.existsAsFile());
}

TEST_CASE("[settings] changing the settings does not change the processor's state (Lesson EQ)") { settingsStayOutOfTheSession<EqProcessor>(); }
TEST_CASE("[settings] changing the settings does not change the processor's state (Lesson Comp)") { settingsStayOutOfTheSession<CompProcessor>(); }

TEST_CASE("[settings] scaleChoices: four scales in order, enabled when 960s+60 by 560s+60 fits") {
    const auto choices = WebEditor::scaleChoices({ 0, 0, 5000, 3000 });
    REQUIRE(choices.size() == 4);
    CHECK(choices[0].value == 1.0);
    CHECK(choices[1].value == 1.25);
    CHECK(choices[2].value == 1.5);
    CHECK(choices[3].value == 2.0);
    for (const auto& c : choices) CHECK(c.enabled);
}

TEST_CASE("[settings] scaleChoices: exactly the size is enabled, one px short is not (width and height)") {
    struct Fit {
        double scale;
        int width;
        int height;
        std::size_t index;
    };
    // 960 s + 60 and 560 s + 60.
    const Fit fits[] = { { 1.0, 1020, 620, 0 }, { 1.25, 1260, 760, 1 }, { 1.5, 1500, 900, 2 }, { 2.0, 1980, 1180, 3 } };
    for (const auto& f : fits) {
        INFO("scale " << f.scale);
        REQUIRE(std::lround(960 * f.scale) + 60 == f.width);
        REQUIRE(std::lround(560 * f.scale) + 60 == f.height);
        CHECK(WebEditor::scaleChoices({ 0, 0, f.width, f.height })[f.index].enabled);
        CHECK_FALSE(WebEditor::scaleChoices({ 0, 0, f.width - 1, f.height })[f.index].enabled);
        CHECK_FALSE(WebEditor::scaleChoices({ 0, 0, f.width, f.height - 1 })[f.index].enabled);
        CHECK(WebEditor::scaleChoices({ 0, 0, f.width + 1, f.height + 1 })[f.index].enabled);
        // Wide but not tall, and tall but not wide.
        CHECK_FALSE(WebEditor::scaleChoices({ 0, 0, 10000, f.height - 1 })[f.index].enabled);
        CHECK_FALSE(WebEditor::scaleChoices({ 0, 0, f.width - 1, 10000 })[f.index].enabled);
    }
}

TEST_CASE("[settings] scaleChoices: only the size of the area counts, not where it is") {
    const auto at = WebEditor::scaleChoices({ 0, 0, 1500, 900 });
    const auto moved = WebEditor::scaleChoices({ 1920, 40, 1500, 900 });
    const auto negative = WebEditor::scaleChoices({ -2000, -300, 1500, 900 });
    REQUIRE(at.size() == 4);
    REQUIRE(moved.size() == 4);
    REQUIRE(negative.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(moved[i].enabled == at[i].enabled);
        CHECK(negative[i].enabled == at[i].enabled);
    }
    CHECK(at[0].enabled);
    CHECK(at[1].enabled);
    CHECK(at[2].enabled);
    CHECK_FALSE(at[3].enabled);
}

TEST_CASE("[settings] scaleChoices: a typical 1920x1080 area allows 100, 125 and 150 %, an empty area allows none") {
    const auto hd = WebEditor::scaleChoices({ 0, 0, 1920, 1040 }); // work area under a task bar
    REQUIRE(hd.size() == 4);
    CHECK(hd[0].enabled);
    CHECK(hd[1].enabled);
    CHECK(hd[2].enabled);
    CHECK_FALSE(hd[3].enabled); // 2 x needs 1180 in height

    const auto none = WebEditor::scaleChoices({ 0, 0, 0, 0 });
    REQUIRE(none.size() == 4);
    for (const auto& c : none) CHECK_FALSE(c.enabled);
}

TEST_CASE("[settings] openingScale: the saved scale when it is enabled") {
    const auto choices = WebEditor::scaleChoices({ 0, 0, 5000, 3000 });
    for (const double saved : UiSettings::kScales) CHECK(WebEditor::openingScale(saved, choices) == saved);
}

TEST_CASE("[settings] openingScale: otherwise the largest enabled scale at or below it, else 1") {
    const auto choices = WebEditor::scaleChoices({ 0, 0, 1300, 800 }); // 100 % and 125 % fit
    REQUIRE(choices.size() == 4);
    REQUIRE(choices[1].enabled);
    REQUIRE_FALSE(choices[2].enabled);
    CHECK(WebEditor::openingScale(1.0, choices) == 1.0);
    CHECK(WebEditor::openingScale(1.25, choices) == 1.25);
    CHECK(WebEditor::openingScale(1.5, choices) == 1.25);
    CHECK(WebEditor::openingScale(2.0, choices) == 1.25);
    CHECK(WebEditor::openingScale(1.3, choices) == 1.25); // not one of the four: the largest enabled below it

    // The choices as given, not only those of a real area: 100 % and 150 % are off.
    const std::vector<ScaleChoice> odd { { 1.0, false }, { 1.25, true }, { 1.5, false }, { 2.0, true } };
    CHECK(WebEditor::openingScale(1.5, odd) == 1.25);
    CHECK(WebEditor::openingScale(2.0, odd) == 2.0);
    CHECK(WebEditor::openingScale(1.25, odd) == 1.25);
    CHECK(WebEditor::openingScale(1.0, odd) == 1.0); // nothing enabled at or below 1: 1

    const std::vector<ScaleChoice> lowOnly { { 1.0, true }, { 1.25, false }, { 1.5, false }, { 2.0, false } };
    CHECK(WebEditor::openingScale(2.0, lowOnly) == 1.0);
    CHECK(WebEditor::openingScale(1.25, lowOnly) == 1.0);
}

TEST_CASE("[settings] openingScale: 1 when nothing is enabled, or the saved scale is below every choice") {
    const auto none = WebEditor::scaleChoices({ 0, 0, 100, 100 });
    for (const double saved : { 1.0, 1.25, 1.5, 2.0, 0.5, 0.0, 1.3 }) CHECK(WebEditor::openingScale(saved, none) == 1.0);
    const auto all = WebEditor::scaleChoices({ 0, 0, 5000, 3000 });
    CHECK(WebEditor::openingScale(0.5, all) == 1.0);
    CHECK(WebEditor::openingScale(0.0, all) == 1.0);
}

// A real editor (no WebView2): the scale the settings ask for.
template <typename Processor>
void requestScaleOnEditor() {
    TempDir temp;
    const auto file = temp.file();
    Processor processor;
    WebEditor editor(processor, true, file);
    CHECK_FALSE(editor.showsPage());

    const auto size = [](double s) { return juce::Point<int>(static_cast<int>(std::lround(960 * s)), static_cast<int>(std::lround(560 * s))); };
    const auto expectSize = [&](double s) {
        CHECK(editor.getWidth() == size(s).x);
        CHECK(editor.getHeight() == size(s).y);
    };

    editor.setUserAreaForTest({ 0, 0, 2100, 1300 }); // all four fit
    {
        const auto state = editor.requestScale(1.5);
        CHECK(state.scale == 1.5);
        expectSize(1.5);
        CHECK(UiSettings(file).scale() == 1.5);
        REQUIRE(state.choices.size() == 4);
        for (const auto& c : state.choices) CHECK(c.enabled);
        CHECK(editor.scaleState().scale == 1.5);
    }
    {
        const auto state = editor.requestScale(2.0);
        CHECK(state.scale == 2.0);
        expectSize(2.0);
        CHECK(UiSettings(file).scale() == 2.0);
    }
    {
        const auto state = editor.requestScale(1.0);
        CHECK(state.scale == 1.0);
        expectSize(1.0);
        CHECK(UiSettings(file).scale() == 1.0);
    }
    {
        const auto state = editor.requestScale(1.25);
        CHECK(state.scale == 1.25);
        expectSize(1.25);
        CHECK(UiSettings(file).scale() == 1.25);
    }

    // A screen that holds only 150 % exactly: 1500 x 900.
    editor.setUserAreaForTest({ 0, 0, 1500, 900 });
    {
        const auto state = editor.requestScale(1.5);
        CHECK(state.scale == 1.5);
        expectSize(1.5);
        CHECK(UiSettings(file).scale() == 1.5);
        REQUIRE(state.choices.size() == 4);
        CHECK(state.choices[2].enabled);
        CHECK_FALSE(state.choices[3].enabled);
    }
    // 200 % does not fit: nothing changes, neither the window nor the file.
    {
        const auto bytes = bytesOf(file);
        const auto state = editor.requestScale(2.0);
        CHECK(state.scale == 1.5);
        expectSize(1.5);
        CHECK(bytesOf(file) == bytes);
        CHECK(UiSettings(file).scale() == 1.5);
        CHECK_FALSE(state.choices[3].enabled);
    }
    // Not one of the four: nothing changes.
    for (const double bad : { 1.3, 0.0, -1.0, 3.0 }) {
        INFO("scale " << bad);
        const auto bytes = bytesOf(file);
        const auto state = editor.requestScale(bad);
        CHECK(state.scale == 1.5);
        expectSize(1.5);
        CHECK(bytesOf(file) == bytes);
    }
    // One px short in width or height of 150 %: the 150 % request is refused.
    editor.requestScale(1.0);
    editor.setUserAreaForTest({ 0, 0, 1499, 900 });
    {
        const auto state = editor.requestScale(1.5);
        CHECK(state.scale == 1.0);
        expectSize(1.0);
        CHECK(UiSettings(file).scale() == 1.0);
    }
    editor.setUserAreaForTest({ 0, 0, 1500, 899 });
    {
        const auto state = editor.requestScale(1.5);
        CHECK(state.scale == 1.0);
        expectSize(1.0);
        CHECK(UiSettings(file).scale() == 1.0);
        CHECK(state.choices[1].enabled);
        CHECK_FALSE(state.choices[2].enabled);
    }
    CHECK_FALSE(tmpOf(file).exists());
}

TEST_CASE("[settings] requestScale resizes the window and saves the scale, but only for an enabled one of the four (Lesson EQ)") {
    requestScaleOnEditor<EqProcessor>();
}
TEST_CASE("[settings] requestScale resizes the window and saves the scale, but only for an enabled one of the four (Lesson Comp)") {
    requestScaleOnEditor<CompProcessor>();
}

TEST_CASE("[settings] requestScale keeps the other keys of the settings file") {
    TempDir temp;
    const auto file = temp.file();
    put(file, "{\"version\":4,\"theme\":\"studio\",\"mine\":[1],\"lesson\":{\"hint\":false}}");
    EqProcessor processor;
    WebEditor editor(processor, true, file);
    editor.setUserAreaForTest({ 0, 0, 2100, 1300 });
    CHECK(editor.requestScale(1.25).scale == 1.25);
    const auto v = parsed(file);
    REQUIRE(v.isObject());
    CHECK(static_cast<double>(property(v, "scale")) == 1.25);
    CHECK(static_cast<int>(property(v, "version")) == 4);
    CHECK(property(v, "theme").toString() == "studio");
    CHECK(text(property(v, "mine")) == text(juce::JSON::parse("[1]")));
    CHECK(static_cast<bool>(property(property(v, "lesson"), "hint")) == false);
}

} // namespace
