// Bridge (plugin/common/Bridge.h), from its header and the page's host.ts: setParam and the host's gestures, and the events
// of tick(). The timer never fires here (no message loop runs), so the time is made by calling tick().
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <common/Bridge.h>
#include <eq/EqProcessor.h>
#include <lesson/Spectrum.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace lesson::plugin;
using lesson::SpectrumAnalyser;

namespace {
struct Event {
    juce::String name;
    juce::var payload;
};

// Counts the host's gesture calls by JUCE parameter index.
struct GestureCounter final : juce::AudioProcessorListener {
    std::map<int, int> begins;
    std::map<int, int> ends;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override { ++begins[index]; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index) override { ++ends[index]; }
};

// A processor, a Bridge on a temporary settings file (never the real <AppData>\PRAYROOM\settings.json), a recorder for the
// events and a counter for the gestures.
template <typename Processor>
struct Rig {
    Processor processor;
    juce::File file { juce::File::createTempFile(".json") };
    UiSettings settings { file };
    GestureCounter gestures;
    std::vector<Event> events;
    Bridge bridge { processor, settings };

    Rig() {
        processor.addListener(&gestures);
        bridge.setSink([this](const juce::Identifier& name, const juce::var& payload) {
            events.push_back({ name.toString(), payload });
        });
    }
    ~Rig() {
        processor.removeListener(&gestures);
        file.deleteFile();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    std::size_t at(const char* id) const {
        const auto specs = processor.specs();
        for (std::size_t i = 0; i < specs.size(); ++i)
            if (specs[i].id == id) return i;
        FAIL("no parameter " << id);
        return 0;
    }
    int juceIndex(std::size_t i) const { return processor.parameter(i).getParameterIndex(); }
    int begins(std::size_t i) { return gestures.begins[juceIndex(i)]; }
    int ends(std::size_t i) { return gestures.ends[juceIndex(i)]; }
    int totalBegins() const {
        int n = 0;
        for (const auto& entry : gestures.begins) n += entry.second;
        return n;
    }
    int totalEnds() const {
        int n = 0;
        for (const auto& entry : gestures.ends) n += entry.second;
        return n;
    }
    std::vector<float> plainValues() const {
        std::vector<float> values;
        for (std::size_t i = 0; i < processor.specs().size(); ++i) values.push_back(processor.plainValue(i));
        return values;
    }
    void set(const char* id, double value, const char* phase) { bridge.setParam(juce::var(id), juce::var(value), juce::var(phase)); }

    std::vector<const Event*> named(const char* name) const {
        std::vector<const Event*> found;
        for (const auto& e : events)
            if (e.name == name) found.push_back(&e);
        return found;
    }
    int count(const char* name) const { return static_cast<int>(named(name).size()); }
    juce::StringArray names() const {
        juce::StringArray all;
        for (const auto& e : events) all.add(e.name);
        return all;
    }
};

// A plain value is the float the table's range lands on, so a round trip through 0..1 is not exact.
doctest::Approx closeTo(double v) { return doctest::Approx(v).epsilon(1e-4).scale(1.0); }
doctest::Approx closeToFloat(double v) { return doctest::Approx(v).epsilon(1e-6).scale(1.0); }

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
juce::var property(const juce::var& object, const char* name) { return object.getProperty(juce::Identifier(name), juce::var()); }
bool has(const juce::var& object, const char* name) { return object.hasProperty(juce::Identifier(name)); }

std::vector<std::uint8_t> decodeBytes(const juce::var& payload) {
    REQUIRE(payload.isString());
    juce::MemoryOutputStream out;
    REQUIRE(juce::Base64::convertFromBase64(out, payload.toString()));
    const auto* data = static_cast<const std::uint8_t*>(out.getData());
    return { data, data + out.getDataSize() };
}

// float32, little-endian.
std::vector<float> floatsOf(const std::vector<std::uint8_t>& bytes) {
    REQUIRE(bytes.size() % 4 == 0);
    std::vector<float> values(bytes.size() / 4);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::uint32_t bits = static_cast<std::uint32_t>(bytes[4 * i]) | (static_cast<std::uint32_t>(bytes[4 * i + 1]) << 8)
            | (static_cast<std::uint32_t>(bytes[4 * i + 2]) << 16) | (static_cast<std::uint32_t>(bytes[4 * i + 3]) << 24);
        std::memcpy(&values[i], &bits, sizeof(float));
    }
    return values;
}

// Stereo noise of amplitude 0.5 through processBlock.
void flow(juce::AudioProcessor& processor, int blocks, int length = 1024) {
    static std::mt19937 random(12345);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(2, length);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < length; ++i) buffer.setSample(ch, i, noise(random));
        processor.processBlock(buffer, midi);
    }
}

// ---- setParam ----

// begin (with the value) opens the gesture once, move only moves the value, end closes it once.
template <typename Processor>
void gestureLifecycle(const char* id, double first, double second) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);

    rig.set(id, first, "begin");
    CHECK(rig.processor.plainValue(i) == closeTo(first));
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 0);

    rig.set(id, second, "move");
    CHECK(rig.processor.plainValue(i) == closeTo(second));
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 0);

    rig.set(id, first, "move");
    CHECK(rig.processor.plainValue(i) == closeTo(first));
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 0);

    rig.set(id, second, "end");
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 1);
    CHECK(rig.totalBegins() == 1);
    CHECK(rig.totalEnds() == 1);
}

// complete is begin, the value and end at once.
template <typename Processor>
void completeIsOneGesture(const char* id, double first, double second) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);

    rig.set(id, first, "complete");
    CHECK(rig.processor.plainValue(i) == closeTo(first));
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 1);

    rig.set(id, second, "complete");
    CHECK(rig.processor.plainValue(i) == closeTo(second));
    CHECK(rig.begins(i) == 2);
    CHECK(rig.ends(i) == 2);
    CHECK(rig.totalBegins() == 2);
    CHECK(rig.totalEnds() == 2);
}

// A second begin in an open gesture does not begin again (but its value counts); an end with none open does nothing.
template <typename Processor>
void beginInsideAGesture(const char* id, double first, double second) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);

    rig.set(id, first, "end"); // none open
    CHECK(rig.totalBegins() == 0);
    CHECK(rig.totalEnds() == 0);

    rig.set(id, first, "begin");
    rig.set(id, second, "begin");
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 0);
    CHECK(rig.processor.plainValue(i) == closeTo(second));

    rig.set(id, second, "end");
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 1);

    rig.set(id, second, "end"); // closed already
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 1);
    CHECK(rig.totalBegins() == 1);
    CHECK(rig.totalEnds() == 1);
}

// A value outside the range lands on its end, in every phase that carries a value.
template <typename Processor>
void outsideTheRange(const char* id) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);
    const auto range = rig.processor.parameter(i).getNormalisableRange();
    const double lowest = range.start;
    const double highest = range.end;
    const double above = highest + std::abs(highest) * 10.0 + 100.0;
    const double below = lowest - std::abs(lowest) * 10.0 - 100.0;

    rig.set(id, above, "complete");
    CHECK(rig.processor.plainValue(i) == closeTo(highest));
    rig.set(id, below, "complete");
    CHECK(rig.processor.plainValue(i) == closeTo(lowest));

    rig.set(id, above, "begin");
    CHECK(rig.processor.plainValue(i) == closeTo(highest));
    rig.set(id, below, "move");
    CHECK(rig.processor.plainValue(i) == closeTo(lowest));
    rig.set(id, above, "move");
    CHECK(rig.processor.plainValue(i) == closeTo(highest));
    rig.set(id, below, "end");
    CHECK(rig.begins(i) == 3);
    CHECK(rig.ends(i) == 3);
}

// What changes nothing: not the values, not the gestures.
template <typename Processor>
void changesNothing(const char* id, double start) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);
    rig.set(id, start, "complete");
    REQUIRE(rig.processor.plainValue(i) == closeTo(start));
    const auto values = rig.plainValues();
    const auto begins = rig.gestures.begins;
    const auto ends = rig.gestures.ends;
    const auto unchanged = [&] {
        CHECK(rig.plainValues() == values);
        CHECK(rig.gestures.begins == begins);
        CHECK(rig.gestures.ends == ends);
    };

    const juce::var good(7.0);
    const char* const phases[] = { "begin", "move", "end", "complete" };

    // an id that is not one of the table's, or not a string
    for (const char* phase : phases) {
        INFO(phase);
        rig.bridge.setParam(juce::var("nope"), good, juce::var(phase));
        rig.bridge.setParam(juce::var(3), good, juce::var(phase));
        unchanged();
    }
    // a phase that is none of the four, or not a string
    rig.bridge.setParam(juce::var(id), good, juce::var("drag"));
    rig.bridge.setParam(juce::var(id), good, juce::var(""));
    rig.bridge.setParam(juce::var(id), good, juce::var(3));
    rig.bridge.setParam(juce::var(id), good, juce::var());
    unchanged();
    // a value that is not a finite number
    const juce::var badValues[] = { juce::var(kNan), juce::var(kInf), juce::var(-kInf), juce::var("1") };
    for (const char* phase : { "begin", "move", "complete" }) {
        for (const auto& bad : badValues) {
            INFO(phase << " " << bad.toString());
            rig.bridge.setParam(juce::var(id), bad, juce::var(phase));
            unchanged();
        }
    }
}

// A bad value in the middle of a gesture leaves the gesture and the value as they are; an end still ends it, whatever it carries.
template <typename Processor>
void badValueInsideAGesture(const char* id, double start) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);
    rig.set(id, start, "begin");
    REQUIRE(rig.begins(i) == 1);
    for (const auto& bad : { juce::var(kNan), juce::var(kInf), juce::var("1") }) {
        rig.bridge.setParam(juce::var(id), bad, juce::var("move"));
        CHECK(rig.processor.plainValue(i) == closeTo(start));
        CHECK(rig.begins(i) == 1);
        CHECK(rig.ends(i) == 0);
    }
    rig.bridge.setParam(juce::var(id), juce::var(kNan), juce::var("end"));
    CHECK(rig.processor.plainValue(i) == closeTo(start));
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 1);
}

// stop() ends a gesture that was left open, once.
template <typename Processor>
void stopEndsTheGesture(const char* id, double value) {
    Rig<Processor> rig;
    const auto i = rig.at(id);
    INFO(id);
    rig.bridge.start();
    rig.set(id, value, "begin");
    REQUIRE(rig.begins(i) == 1);
    REQUIRE(rig.ends(i) == 0);
    rig.bridge.stop();
    CHECK(rig.begins(i) == 1);
    CHECK(rig.ends(i) == 1);
    CHECK(rig.totalEnds() == 1);
    rig.bridge.stop(); // nothing left open
    CHECK(rig.ends(i) == 1);
    CHECK(rig.totalEnds() == 1);
}
}

TEST_CASE("[bridge] setParam: begin, move and end make one gesture (Lesson EQ)") {
    gestureLifecycle<EqProcessor>("b1_gain", 6.0, 9.0);
    gestureLifecycle<EqProcessor>("b1_freq", 1000.0, 2000.0);
}

TEST_CASE("[bridge] setParam: begin, move and end make one gesture (Lesson Comp)") {
    gestureLifecycle<CompProcessor>("comp_threshold", -30.0, -20.0);
}

TEST_CASE("[bridge] setParam: complete is a whole gesture (Lesson EQ)") {
    completeIsOneGesture<EqProcessor>("b1_gain", 6.0, -4.0);
    completeIsOneGesture<EqProcessor>("b1_freq", 1000.0, 2000.0);
}

TEST_CASE("[bridge] setParam: complete is a whole gesture (Lesson Comp)") {
    completeIsOneGesture<CompProcessor>("comp_threshold", -30.0, -12.0);
}

TEST_CASE("[bridge] setParam: a second begin does not begin again, an end with none open does nothing (Lesson EQ)") {
    beginInsideAGesture<EqProcessor>("b1_gain", 6.0, 3.0);
    beginInsideAGesture<EqProcessor>("b1_freq", 1000.0, 2000.0);
}

TEST_CASE("[bridge] setParam: a second begin does not begin again, an end with none open does nothing (Lesson Comp)") {
    beginInsideAGesture<CompProcessor>("comp_threshold", -30.0, -20.0);
}

TEST_CASE("[bridge] setParam: a value outside the range lands on its end (Lesson EQ)") {
    outsideTheRange<EqProcessor>("b1_gain");
    outsideTheRange<EqProcessor>("b1_freq");
}

TEST_CASE("[bridge] setParam: a value outside the range lands on its end (Lesson Comp)") {
    outsideTheRange<CompProcessor>("comp_threshold");
}

TEST_CASE("[bridge] setParam: an unknown id, a bad phase or a value that is not a finite number changes nothing (Lesson EQ)") {
    changesNothing<EqProcessor>("b1_gain", 6.0);
    changesNothing<EqProcessor>("b1_freq", 1000.0);
}

TEST_CASE("[bridge] setParam: an unknown id, a bad phase or a value that is not a finite number changes nothing (Lesson Comp)") {
    changesNothing<CompProcessor>("comp_threshold", -30.0);
}

TEST_CASE("[bridge] setParam: a bad value in a gesture leaves it open, and an end with a NaN still ends it") {
    badValueInsideAGesture<EqProcessor>("b1_gain", 6.0);
    badValueInsideAGesture<CompProcessor>("comp_threshold", -30.0);
}

TEST_CASE("[bridge] stop() ends the gesture that was left open (Lesson EQ)") {
    stopEndsTheGesture<EqProcessor>("b1_gain", 6.0);
    stopEndsTheGesture<EqProcessor>("b1_freq", 1000.0);
}

TEST_CASE("[bridge] stop() ends the gesture that was left open (Lesson Comp)") {
    stopEndsTheGesture<CompProcessor>("comp_threshold", -30.0);
}

TEST_CASE("[bridge] stop() with no gesture open ends nothing") {
    Rig<EqProcessor> rig;
    rig.bridge.start();
    rig.set("b1_gain", 6.0, "complete");
    rig.bridge.stop();
    CHECK(rig.totalBegins() == 1);
    CHECK(rig.totalEnds() == 1);
}

// ---- tick ----

namespace {
template <typename Processor>
void tickCarriesTheState(double sampleRate) {
    Rig<Processor> rig;
    rig.processor.prepareToPlay(sampleRate, 1024);
    rig.bridge.start();
    INFO("sample rate " << sampleRate);
    for (int n = 1; n <= 3; ++n) {
        rig.bridge.tick();
        REQUIRE(rig.count("tick") == n); // one every time
    }
    const auto& payload = rig.named("tick").back()->payload;
    REQUIRE(payload.getDynamicObject() != nullptr);
    CHECK(isNumber(property(payload, "makeup")));
    CHECK(static_cast<double>(property(payload, "makeup")) == closeTo(rig.processor.makeupDb()));
    CHECK(property(payload, "playing").isBool());
    CHECK(isNumber(property(payload, "sampleRate")));
    CHECK(static_cast<double>(property(payload, "sampleRate")) == doctest::Approx(sampleRate));
}

// Starts a rig, ticks once to take what the test did before, and forgets the events.
template <typename Rig>
void flush(Rig& rig) {
    rig.bridge.start();
    rig.bridge.tick();
    rig.events.clear();
}

template <typename Processor>
void paramsEvent(const char* idA, double hostValue, const char* idB, double setParamValue) {
    Rig<Processor> rig;
    rig.processor.prepareToPlay(48000.0, 1024);
    const auto a = rig.at(idA);
    const auto b = rig.at(idB);
    flush(rig);

    // nothing changed: no params
    rig.bridge.tick();
    CHECK(rig.count("params") == 0);
    CHECK(rig.count("tick") == 1);

    // the host moves one parameter: it alone, with its plain value
    rig.events.clear();
    rig.processor.parameter(a).setValueNotifyingHost(static_cast<float>(hostValue));
    rig.bridge.tick();
    REQUIRE(rig.count("params") == 1);
    {
        const auto& payload = rig.named("params")[0]->payload;
        REQUIRE(payload.getDynamicObject() != nullptr);
        CHECK(payload.getDynamicObject()->getProperties().size() == 1);
        REQUIRE(has(payload, idA));
        const auto value = property(payload, idA);
        REQUIRE(isNumber(value));
        CHECK(static_cast<double>(value) == closeToFloat(rig.processor.plainValue(a)));
    }

    // sent once: the next tick has nothing to say
    rig.events.clear();
    rig.bridge.tick();
    CHECK(rig.count("params") == 0);

    // two parameters at once make one event with both; the page's own setParam comes back too
    rig.events.clear();
    rig.processor.parameter(a).setValueNotifyingHost(1.0f);
    rig.set(idB, setParamValue, "complete");
    rig.bridge.tick();
    REQUIRE(rig.count("params") == 1);
    {
        const auto& payload = rig.named("params")[0]->payload;
        REQUIRE(payload.getDynamicObject() != nullptr);
        CHECK(payload.getDynamicObject()->getProperties().size() == 2);
        CHECK(static_cast<double>(property(payload, idA)) == closeToFloat(rig.processor.plainValue(a)));
        CHECK(static_cast<double>(property(payload, idB)) == closeToFloat(rig.processor.plainValue(b)));
        CHECK(static_cast<double>(property(payload, idB)) == closeTo(setParamValue));
    }

    // a setParam alone, in the middle of a gesture, comes back as well
    rig.events.clear();
    rig.set(idB, setParamValue + 1.0, "begin");
    rig.bridge.tick();
    REQUIRE(rig.count("params") == 1);
    CHECK(static_cast<double>(property(rig.named("params")[0]->payload, idB)) == closeTo(setParamValue + 1.0));
    rig.events.clear();
    rig.set(idB, setParamValue + 2.0, "move");
    rig.bridge.tick();
    REQUIRE(rig.count("params") == 1);
    {
        const auto& payload = rig.named("params")[0]->payload;
        CHECK(payload.getDynamicObject()->getProperties().size() == 1);
        CHECK(has(payload, idB));
        CHECK(static_cast<double>(property(payload, idB)) == closeTo(setParamValue + 2.0));
    }
    rig.set(idB, setParamValue + 2.0, "end");
}

template <typename Processor>
void playingFollowsTheBlocks() {
    Rig<Processor> rig;
    rig.processor.prepareToPlay(48000.0, 1024);
    rig.bridge.start();
    const auto playing = [&] {
        rig.events.clear();
        rig.bridge.tick();
        REQUIRE(rig.count("tick") == 1);
        return static_cast<bool>(property(rig.named("tick")[0]->payload, "playing"));
    };

    CHECK_FALSE(playing()); // just after start()
    CHECK_FALSE(playing());

    flow(rig.processor, 1);
    CHECK(playing());
    // no more blocks: five more ticks are still playing, the sixth is not (Bridge::kStillTicks)
    static_assert(Bridge::kStillTicks == 6);
    for (int n = 1; n <= Bridge::kStillTicks - 1; ++n) {
        INFO("tick " << n << " after the block");
        CHECK(playing());
    }
    CHECK_FALSE(playing());
    CHECK_FALSE(playing());

    // a block starts it again, and another block in between keeps it going
    flow(rig.processor, 1);
    CHECK(playing());
    for (int n = 1; n <= 3; ++n) CHECK(playing());
    flow(rig.processor, 1);
    for (int n = 0; n <= Bridge::kStillTicks - 1; ++n) {
        INFO("tick " << n << " after the second block");
        CHECK(playing());
    }
    CHECK_FALSE(playing());
}

// The EQ's spectrum: pre and post, bins up to just above 20 kHz.
void spectrumAt(double sampleRate) {
    Rig<EqProcessor> rig;
    rig.processor.prepareToPlay(sampleRate, 1024);
    rig.bridge.start();
    INFO("sample rate " << sampleRate);

    rig.bridge.tick(); // not playing
    CHECK(rig.count("tick") == 1);
    CHECK(rig.count("spectrum") == 0);

    flow(rig.processor, 16);
    const int bins = SpectrumAnalyser::binsFor(sampleRate);
    REQUIRE(bins > 0);
    for (int n = 0; n < Bridge::kStillTicks; ++n) {
        INFO("playing tick " << n);
        rig.events.clear();
        rig.bridge.tick();
        REQUIRE(rig.count("spectrum") == 1);
        const auto& payload = rig.named("spectrum")[0]->payload;
        REQUIRE(payload.getDynamicObject() != nullptr);
        if (n > 0) continue; // the rest only needs to be there
        const auto preBytes = decodeBytes(property(payload, "pre"));
        const auto postBytes = decodeBytes(property(payload, "post"));
        CHECK(preBytes.size() == 4u * static_cast<std::size_t>(bins));
        CHECK(postBytes.size() == 4u * static_cast<std::size_t>(bins));
        CHECK(isNumber(property(payload, "bin")));
        CHECK(static_cast<double>(property(payload, "bin")) == doctest::Approx(sampleRate / 8192.0));
        CHECK(isNumber(property(payload, "fftSize")));
        CHECK(static_cast<int>(property(payload, "fftSize")) == 8192);
        float preMax = -std::numeric_limits<float>::infinity();
        for (const auto* values : { &preBytes, &postBytes }) {
            for (const float v : floatsOf(*values)) {
                CHECK(std::isfinite(v));
                CHECK(v >= SpectrumAnalyser::kFloorDb);
            }
        }
        for (const float v : floatsOf(preBytes)) preMax = std::max(preMax, v);
        CHECK(preMax > -130.0f);
    }

    // the sixth tick without a block is not playing: no spectrum (and still the tick)
    rig.events.clear();
    rig.bridge.tick();
    CHECK(rig.count("tick") == 1);
    CHECK_FALSE(static_cast<bool>(property(rig.named("tick")[0]->payload, "playing")));
    CHECK(rig.count("spectrum") == 0);
}
}

TEST_CASE("[bridge] tick: the tick event has Auto Gain, playing and the sample rate (Lesson EQ)") {
    tickCarriesTheState<EqProcessor>(48000.0);
    tickCarriesTheState<EqProcessor>(44100.0);
}

TEST_CASE("[bridge] tick: the tick event has Auto Gain, playing and the sample rate (Lesson Comp)") {
    tickCarriesTheState<CompProcessor>(48000.0);
    tickCarriesTheState<CompProcessor>(44100.0);
}

TEST_CASE("[bridge] tick: params holds only what changed since the last tick (Lesson EQ)") {
    paramsEvent<EqProcessor>("b1_gain", 0.75, "b1_freq", 2000.0);
}

TEST_CASE("[bridge] tick: params holds only what changed since the last tick (Lesson Comp)") {
    paramsEvent<CompProcessor>("comp_ratio", 0.5, "comp_threshold", -30.0);
}

TEST_CASE("[bridge] tick: params carries a choice and a switch as their plain values") {
    Rig<EqProcessor> rig;
    const auto slope = rig.at("hp_slope");
    const auto on = rig.at("b1_on");
    flush(rig);
    rig.processor.parameter(slope).setValueNotifyingHost(1.0f); // the last of the four
    rig.processor.parameter(on).setValueNotifyingHost(0.0f);    // from On
    rig.bridge.tick();
    REQUIRE(rig.count("params") == 1);
    const auto& payload = rig.named("params")[0]->payload;
    CHECK(payload.getDynamicObject()->getProperties().size() == 2);
    CHECK(has(payload, "hp_slope"));
    CHECK(has(payload, "b1_on"));
    CHECK(static_cast<double>(property(payload, "hp_slope")) == doctest::Approx(3.0));
    CHECK(static_cast<double>(property(payload, "b1_on")) == doctest::Approx(0.0));
}

TEST_CASE("[bridge] tick: playing is true after a block and false the sixth tick after the last one (Lesson EQ)") {
    playingFollowsTheBlocks<EqProcessor>();
}

TEST_CASE("[bridge] tick: playing is true after a block and false the sixth tick after the last one (Lesson Comp)") {
    playingFollowsTheBlocks<CompProcessor>();
}

TEST_CASE("[bridge] tick: Lesson EQ sends the spectrum only while playing, at 48 kHz") {
    spectrumAt(48000.0);
}

TEST_CASE("[bridge] tick: Lesson EQ sends the spectrum only while playing, at 44.1 kHz") {
    spectrumAt(44100.0);
}

TEST_CASE("[bridge] tick: Lesson Comp sends no spectrum, Lesson EQ no points") {
    Rig<CompProcessor> comp;
    comp.processor.prepareToPlay(48000.0, 1024);
    comp.bridge.start();
    flow(comp.processor, 16);
    comp.bridge.tick();
    CHECK(comp.count("tick") == 1);
    CHECK(static_cast<bool>(property(comp.named("tick")[0]->payload, "playing")));
    CHECK(comp.count("spectrum") == 0);

    Rig<EqProcessor> eq;
    eq.processor.prepareToPlay(48000.0, 1024);
    eq.bridge.start();
    flow(eq.processor, 16);
    eq.bridge.tick();
    CHECK(eq.count("tick") == 1);
    CHECK(static_cast<bool>(property(eq.named("tick")[0]->payload, "playing")));
    CHECK(eq.count("points") == 0);
}

TEST_CASE("[bridge] tick: the events come in the order params, tick, then spectrum or points") {
    Rig<EqProcessor> eq;
    eq.processor.prepareToPlay(48000.0, 1024);
    flush(eq);
    eq.processor.parameter(eq.at("b1_gain")).setValueNotifyingHost(0.75f);
    flow(eq.processor, 16);
    eq.bridge.tick();
    CHECK(eq.names() == juce::StringArray { "params", "tick", "spectrum" });

    Rig<CompProcessor> comp;
    comp.processor.prepareToPlay(48000.0, 1024);
    flush(comp);
    comp.processor.parameter(comp.at("comp_threshold")).setValueNotifyingHost(0.5f);
    flow(comp.processor, 1);
    comp.bridge.tick();
    CHECK(comp.names() == juce::StringArray { "params", "tick", "points" });
}

TEST_CASE("[bridge] tick: Lesson Comp's points are 48 bytes for one 1024-sample block, and then none") {
    Rig<CompProcessor> rig;
    rig.processor.prepareToPlay(48000.0, 1024);
    flush(rig);

    flow(rig.processor, 1, 1024);
    rig.bridge.tick();
    REQUIRE(rig.count("points") == 1);
    const auto bytes = decodeBytes(rig.named("points")[0]->payload);
    CHECK(bytes.size() == 48u); // 1024 / 256 points of three floats
    const auto values = floatsOf(bytes);
    float largestInput = 0.0f;
    for (std::size_t i = 0; i < values.size(); ++i) {
        INFO("float " << i);
        CHECK(std::isfinite(values[i]));
        CHECK(values[i] >= 0.0f);
        if (i % 3 == 0) largestInput = std::max(largestInput, values[i]); // the input peak
    }
    CHECK(largestInput > 0.0f);
    CHECK(largestInput <= 0.5f); // the noise's amplitude

    // no block since: the tick is there, the points are not
    rig.events.clear();
    rig.bridge.tick();
    CHECK(rig.count("tick") == 1);
    CHECK(rig.count("points") == 0);
}

TEST_CASE("[bridge] tick: Lesson Comp's points hold the GR when the threshold is low") {
    Rig<CompProcessor> rig;
    rig.processor.prepareToPlay(48000.0, 1024);
    rig.set("comp_threshold", -40.0, "complete");
    rig.set("comp_ratio", 10.0, "complete");
    rig.set("comp_attack", 0.0, "complete");
    flush(rig);

    flow(rig.processor, 8, 1024);
    rig.bridge.tick();
    REQUIRE(rig.count("points") == 1);
    const auto values = floatsOf(decodeBytes(rig.named("points")[0]->payload));
    REQUIRE(values.size() == 8u * 4u * 3u);
    float largestGr = 0.0f;
    for (std::size_t i = 0; i < values.size(); ++i) {
        CHECK(std::isfinite(values[i]));
        CHECK(values[i] >= 0.0f);
        if (i % 3 == 2) largestGr = std::max(largestGr, values[i]);
    }
    CHECK(largestGr > 0.0f); // noise at 0.5 is about -6 dB, well over -40 dB
}

TEST_CASE("[bridge] tick: Lesson Comp's points do not include the blocks that ran before start()") {
    Rig<CompProcessor> rig;
    rig.processor.prepareToPlay(48000.0, 1024);
    flow(rig.processor, 1, 1024);
    rig.bridge.start();
    rig.bridge.tick();
    CHECK(rig.count("tick") == 1);
    CHECK(rig.count("points") == 0);
}
