add_executable(core_tests main.cpp params_tests.cpp)
target_include_directories(core_tests PRIVATE "../../third_party/doctest")
target_link_libraries(core_tests PRIVATE lesson_core)
target_compile_definitions(core_tests PRIVATE LESSON_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../golden")
# One ctest entry per test case, listed from the built executable, so that `ctest -j` runs them side by side.
set(core_tests_list "${CMAKE_CURRENT_BINARY_DIR}/core_tests_list.cmake")
add_custom_command(TARGET core_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:core_tests>" "-DOUT=${core_tests_list}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ListTests.cmake"
    VERBATIM)
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${core_tests_list}")
