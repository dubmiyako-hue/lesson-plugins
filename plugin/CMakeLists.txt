# Two plug-ins from one tree. The shared sources are compiled into each (the JUCE modules are per plug-in target), and the
# tests compile them again (tests/plugin); only the *Entry.cpp files stay out of the tests, each defining createPluginFilter.
set(LESSON_COMMON_SOURCES
    common/LessonParameters.h common/LessonParameters.cpp
    common/LessonProcessor.h common/LessonProcessor.cpp
    common/PlaceholderEditor.h common/PlaceholderEditor.cpp)

function(lesson_add_plugin target product code category)
    juce_add_plugin(${target}
        COMPANY_NAME "Personal"
        PLUGIN_MANUFACTURER_CODE Prsn
        PLUGIN_CODE ${code}
        FORMATS VST3
        PRODUCT_NAME "${product}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx ${category}
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
        NEEDS_CURL FALSE)
    target_sources(${target} PRIVATE ${LESSON_COMMON_SOURCES} ${ARGN})
    target_compile_features(${target} PUBLIC cxx_std_20)
    # JUCE_WEB_BROWSER is left to JUCE (on by default on Windows). The WebView2 loader is linked statically.
    target_compile_definitions(${target} PUBLIC
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target} PRIVATE juce::juce_audio_utils lesson_core
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endfunction()

lesson_add_plugin(LessonEQ "Lesson EQ" Lseq EQ eq/EqProcessor.h eq/EqProcessor.cpp eq/EqEntry.cpp)
lesson_add_plugin(LessonComp "Lesson Comp" Lscp Dynamics comp/CompProcessor.h comp/CompProcessor.cpp comp/CompEntry.cpp)
