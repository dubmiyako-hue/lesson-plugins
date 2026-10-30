add_library(lesson_core STATIC src/Params.cpp src/Format.cpp)
target_include_directories(lesson_core PUBLIC include)
target_compile_features(lesson_core PUBLIC cxx_std_20)
if(MSVC)
    # The tables hold UTF-8 text (最速, ∞:1); /utf-8 is public so that code comparing against them reads its literals alike.
    target_compile_options(lesson_core PUBLIC /utf-8)
    target_compile_options(lesson_core PRIVATE /W4 /permissive-)
endif()
