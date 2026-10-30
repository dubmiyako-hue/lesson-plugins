cmake_minimum_required(VERSION 3.22)
project(lesson VERSION 0.1.0 LANGUAGES C CXX)

# JUCE 9.0.2 with third_party/juce-patches applied (the editor's WebView2 needs them). The default is the worktree that
# subbass-vst uses; see third_party/juce-patches/README.md.
set(LESSON_JUCE_DIR "D:/work/Claude/_sdk/JUCE/JUCE-subbass" CACHE PATH "JUCE source directory (patched)")
file(READ "${LESSON_JUCE_DIR}/modules/juce_gui_extra/misc/juce_WebBrowserComponent.h" lesson_juce_browser_header)
if(NOT lesson_juce_browser_header MATCHES "withTextScaleIgnored")
    message(FATAL_ERROR "${LESSON_JUCE_DIR} lacks third_party/juce-patches/0001-webview2-text-scale.patch "
                        "(see third_party/juce-patches/README.md)")
endif()
file(READ "${LESSON_JUCE_DIR}/modules/juce_gui_extra/native/juce_WebBrowserComponent_windows.cpp" lesson_juce_browser_windows)
if(NOT lesson_juce_browser_windows MATCHES "put_IsZoomControlEnabled")
    message(FATAL_ERROR "${LESSON_JUCE_DIR} lacks third_party/juce-patches/0002-webview2-page-at-component-size.patch "
                        "(see third_party/juce-patches/README.md)")
endif()
add_subdirectory("${LESSON_JUCE_DIR}" JUCE)
enable_testing()
add_subdirectory(core)
add_subdirectory(plugin)
add_subdirectory(tests/unit)
add_subdirectory(tests/plugin)
add_subdirectory(tests/rt)
