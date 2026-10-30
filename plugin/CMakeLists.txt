# Two plug-ins from one tree. The shared sources are compiled into each (the JUCE modules are per plug-in target), and the
# tests compile them again (tests/plugin); only the *Entry.cpp files stay out of the tests, each defining createPluginFilter.
set(LESSON_COMMON_SOURCES
    common/LessonParameters.h common/LessonParameters.cpp
    common/LessonProcessor.h common/LessonProcessor.cpp
    common/WebEditor.h common/WebEditor.cpp
    common/Bridge.h common/Bridge.cpp
    common/UiSettings.h common/UiSettings.cpp)

# The editor's page: the built plugin/web (npm run web:build, which toolsuild.cmd runs first), embedded as a table of
# (path, bytes) in a static library that both plug-ins and the tests link. CONFIGURE_DEPENDS re-reads the file list on
# every build; a changed file regenerates the source through DEPENDS, so configure need not be run again.
set(LESSON_WEB_DIST "${CMAKE_CURRENT_SOURCE_DIR}/web/dist")
file(GLOB_RECURSE LESSON_WEB_FILES CONFIGURE_DEPENDS RELATIVE "${LESSON_WEB_DIST}"
    "${LESSON_WEB_DIST}/*.html" "${LESSON_WEB_DIST}/*.css" "${LESSON_WEB_DIST}/*.js"
    "${LESSON_WEB_DIST}/*.woff" "${LESSON_WEB_DIST}/*.woff2")
if(NOT "index.html" IN_LIST LESSON_WEB_FILES)
    message(FATAL_ERROR "plugin/web/dist/index.html is missing: run `npm run web:build` (tools\build.cmd does)")
endif()
list(SORT LESSON_WEB_FILES)
set(LESSON_WEB_DEPENDS "")
foreach(rel IN LISTS LESSON_WEB_FILES)
    list(APPEND LESSON_WEB_DEPENDS "${LESSON_WEB_DIST}/${rel}")
endforeach()
string(REPLACE ";" "|" LESSON_WEB_FILES_ARG "${LESSON_WEB_FILES}")
set(LESSON_WEB_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp")
add_custom_command(OUTPUT "${LESSON_WEB_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" "-DSRC_DIR=${LESSON_WEB_DIST}" "-DOUT=${LESSON_WEB_SOURCE}"
            "-DFILES=${LESSON_WEB_FILES_ARG}" -P "${CMAKE_CURRENT_SOURCE_DIR}/common/embed_web.cmake"
    DEPENDS ${LESSON_WEB_DEPENDS} "${CMAKE_CURRENT_SOURCE_DIR}/common/embed_web.cmake"
    COMMENT "Embedding plugin/web/dist"
    VERBATIM)
add_library(lesson_web_assets STATIC "${LESSON_WEB_SOURCE}" common/WebAssetTable.h)
target_include_directories(lesson_web_assets PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_compile_features(lesson_web_assets PRIVATE cxx_std_20)

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
    target_link_libraries(${target} PRIVATE juce::juce_audio_utils lesson_core lesson_web_assets
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endfunction()

lesson_add_plugin(LessonEQ "Lesson EQ" Lseq EQ eq/EqProcessor.h eq/EqProcessor.cpp eq/EqEntry.cpp)
lesson_add_plugin(LessonComp "Lesson Comp" Lscp Dynamics comp/CompProcessor.h comp/CompProcessor.cpp comp/CompEntry.cpp)
