#pragma once
// Reading tests/golden: tab separated rows and raw float32 little endian files, channel after channel.
#include <doctest.h>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace golden {

inline std::string path(const std::string& name) {
    return std::string(LESSON_GOLDEN_DIR) + "/" + name;
}

inline std::vector<std::vector<std::string>> rows(const std::string& name) {
    std::ifstream file(path(name), std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "missing " << path(name));
    std::vector<std::vector<std::string>> result;
    std::string text;
    while (std::getline(file, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        if (text.empty()) continue;
        std::vector<std::string> fields;
        std::stringstream line(text);
        std::string field;
        while (std::getline(line, field, '\t')) fields.push_back(field);
        result.push_back(std::move(fields));
    }
    REQUIRE(!result.empty());
    return result;
}

inline double number(const std::string& text) {
    double v = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), v);
    REQUIRE_MESSAGE(result.ptr == text.data() + text.size(), "not a number: " << text);
    return v;
}

// channels x frames
inline std::vector<std::vector<float>> planar(const std::string& name, int channels, int frames) {
    std::ifstream file(path(name), std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "missing " << path(name));
    std::vector<std::vector<float>> result(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(frames)));
    for (auto& channel : result)
        file.read(reinterpret_cast<char*>(channel.data()), static_cast<std::streamsize>(channel.size() * sizeof(float)));
    REQUIRE_MESSAGE(file.gcount() == static_cast<std::streamsize>(frames * sizeof(float)), "short file " << name);
    REQUIRE(file.peek() == std::char_traits<char>::eof());
    return result;
}

} // namespace golden
