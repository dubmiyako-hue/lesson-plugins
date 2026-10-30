add_library(lesson_core STATIC src/Params.cpp src/Format.cpp src/Fdlibm.cpp src/WebBiquad.cpp src/Eq.cpp
    src/Compressor.cpp src/AutoGain.cpp src/Engine.cpp src/Taps.cpp src/Spectrum.cpp)
target_include_directories(lesson_core PUBLIC include)
target_compile_features(lesson_core PUBLIC cxx_std_20)
if(MSVC)
    # The tables hold UTF-8 text (最速, ∞:1); /utf-8 is public so that code comparing against them reads its literals alike.
    target_compile_options(lesson_core PUBLIC /utf-8)
    target_compile_options(lesson_core PRIVATE /W4 /permissive-)
    # fdlibm is Chromium's copy kept as it is (src/Fdlibm.cpp); its C idioms warn at /W4.
    set_source_files_properties(src/Fdlibm.cpp PROPERTIES COMPILE_OPTIONS "/W3;/wd4244")
endif()
