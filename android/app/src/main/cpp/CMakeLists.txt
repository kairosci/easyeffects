# Copyright © 2026 Wellington Wallace
# Copyright © 2026 Alessio Attilio
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.22.1)
project(easyeffects_android LANGUAGES C CXX)

include(CTest)

add_library(easyeffects_bs2b STATIC
    third_party/libbs2b/bs2b.c
)
target_include_directories(easyeffects_bs2b PUBLIC third_party/libbs2b)
set_target_properties(easyeffects_bs2b PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_library(easyeffects_ebur128 STATIC
    third_party/libebur128/ebur128.c
)
target_include_directories(easyeffects_ebur128 PUBLIC third_party/libebur128)
set_target_properties(easyeffects_ebur128 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

if(ANDROID)
    add_library(easyeffects_audio SHARED
        android_global_equalizer.cpp
        android_platform_effects_backend.cpp
        android_autogain_preview.cpp
        autogain_processor.cpp
        bass_loudness_processor.cpp
        crossfeed_processor.cpp
        crusher_processor.cpp
        delay_processor.cpp
        crosstalk_canceller_processor.cpp
        mid_side_equalizer_processor.cpp
        android_pcm_preview.cpp
        jni_bridge.cpp
        parametric_equalizer.cpp
    )
    target_sources(easyeffects_audio PRIVATE
        $<$<CONFIG:Debug>:android_backend_integration_tests.cpp>
    )

    target_compile_features(easyeffects_audio PRIVATE cxx_std_20)
    target_compile_options(easyeffects_audio PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    # Android 15+ devices can use 16 KB memory pages. Keep every ELF PT_LOAD
    # segment aligned to that page size (NDK r27 does not enable this by default).
    target_link_options(easyeffects_audio PRIVATE -Wl,-z,max-page-size=16384)
    target_link_libraries(easyeffects_audio PRIVATE easyeffects_bs2b easyeffects_ebur128 android log aaudio)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(easyeffects_parametric_equalizer_tests
        ../../../../tests/parametric_equalizer_test.cpp
        parametric_equalizer.cpp
    )
    target_compile_features(easyeffects_parametric_equalizer_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_parametric_equalizer_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME easyeffects_parametric_equalizer_tests COMMAND easyeffects_parametric_equalizer_tests)

    add_executable(easyeffects_crossfeed_tests
        ../../../../tests/crossfeed_processor_test.cpp
        crossfeed_processor.cpp
    )
    target_compile_features(easyeffects_crossfeed_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_crossfeed_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_link_libraries(easyeffects_crossfeed_tests PRIVATE easyeffects_bs2b)
    add_test(NAME easyeffects_crossfeed_tests COMMAND easyeffects_crossfeed_tests)

    add_executable(easyeffects_crusher_tests
        ../../../../tests/crusher_processor_test.cpp
        crusher_processor.cpp
    )
    target_compile_features(easyeffects_crusher_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_crusher_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME easyeffects_crusher_tests COMMAND easyeffects_crusher_tests)

    add_executable(easyeffects_delay_tests
        ../../../../tests/delay_processor_test.cpp
        delay_processor.cpp
    )
    target_compile_features(easyeffects_delay_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_delay_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME easyeffects_delay_tests COMMAND easyeffects_delay_tests)

    add_executable(easyeffects_crosstalk_canceller_tests
        ../../../../tests/crosstalk_canceller_processor_test.cpp
        crosstalk_canceller_processor.cpp
    )
    target_compile_features(easyeffects_crosstalk_canceller_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_crosstalk_canceller_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME easyeffects_crosstalk_canceller_tests COMMAND easyeffects_crosstalk_canceller_tests)

    add_executable(easyeffects_mid_side_equalizer_tests
        ../../../../tests/mid_side_equalizer_processor_test.cpp
        mid_side_equalizer_processor.cpp
        parametric_equalizer.cpp
    )
    target_compile_features(easyeffects_mid_side_equalizer_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_mid_side_equalizer_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME easyeffects_mid_side_equalizer_tests COMMAND easyeffects_mid_side_equalizer_tests)

    add_executable(easyeffects_bass_loudness_tests
        ../../../../tests/bass_loudness_processor_test.cpp
        bass_loudness_processor.cpp
    )
    target_compile_features(easyeffects_bass_loudness_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_bass_loudness_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_include_directories(easyeffects_bass_loudness_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME easyeffects_bass_loudness_tests COMMAND easyeffects_bass_loudness_tests)

    add_executable(easyeffects_autogain_tests
        ../../../../tests/autogain_processor_test.cpp
        autogain_processor.cpp
    )
    target_compile_features(easyeffects_autogain_tests PRIVATE cxx_std_20)
    target_compile_options(easyeffects_autogain_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_link_libraries(easyeffects_autogain_tests PRIVATE easyeffects_ebur128)
    add_test(NAME easyeffects_autogain_tests COMMAND easyeffects_autogain_tests)
endif()
