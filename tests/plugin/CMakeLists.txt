juce_add_console_app(plugin_tests PRODUCT_NAME "Lesson plugin tests" NEEDS_WEB_BROWSER TRUE NEEDS_WEBVIEW2 TRUE)
set(lesson_plugin_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../plugin")
target_sources(plugin_tests PRIVATE main.cpp params_tests.cpp state_tests.cpp bus_tests.cpp mapping_tests.cpp bridge_tests.cpp
    "${lesson_plugin_dir}/common/LessonParameters.cpp" "${lesson_plugin_dir}/common/LessonProcessor.cpp"
    "${lesson_plugin_dir}/common/WebEditor.cpp" "${lesson_plugin_dir}/common/Bridge.cpp"
    "${lesson_plugin_dir}/common/UiSettings.cpp"
    "${lesson_plugin_dir}/eq/EqProcessor.cpp" "${lesson_plugin_dir}/comp/CompProcessor.cpp")
target_compile_features(plugin_tests PRIVATE cxx_std_20)
# The plug-ins' own definitions (plugin/CMakeLists.txt); JucePlugin_VersionString is the project version.
target_compile_definitions(plugin_tests PRIVATE JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JucePlugin_VersionString="${PROJECT_VERSION}"
    LESSON_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden")
target_include_directories(plugin_tests PRIVATE ../../third_party/doctest "${lesson_plugin_dir}")
target_link_libraries(plugin_tests PRIVATE juce::juce_audio_utils lesson_core lesson_web_assets juce::juce_recommended_config_flags)
add_test(NAME plugin_tests COMMAND plugin_tests)
