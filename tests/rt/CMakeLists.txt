juce_add_console_app(rt_alloc_test PRODUCT_NAME "Lesson rt alloc test" NEEDS_WEB_BROWSER TRUE NEEDS_WEBVIEW2 TRUE)
set(lesson_plugin_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../plugin")
target_sources(rt_alloc_test PRIVATE rt_alloc_test.cpp
    "${lesson_plugin_dir}/common/LessonParameters.cpp" "${lesson_plugin_dir}/common/LessonProcessor.cpp"
    "${lesson_plugin_dir}/common/WebEditor.cpp" "${lesson_plugin_dir}/common/Bridge.cpp"
    "${lesson_plugin_dir}/common/UiSettings.cpp"
    "${lesson_plugin_dir}/eq/EqProcessor.cpp" "${lesson_plugin_dir}/comp/CompProcessor.cpp")
target_compile_features(rt_alloc_test PRIVATE cxx_std_20)
target_compile_definitions(rt_alloc_test PRIVATE JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JucePlugin_VersionString="${PROJECT_VERSION}")
target_include_directories(rt_alloc_test PRIVATE "${lesson_plugin_dir}")
target_link_libraries(rt_alloc_test PRIVATE juce::juce_audio_utils lesson_core lesson_web_assets juce::juce_recommended_config_flags)
add_test(NAME rt_alloc_test COMMAND rt_alloc_test)
