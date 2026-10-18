/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/parametric_equalizer.hpp"
#include "../app/src/main/cpp/android_platform_effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
using ee::mobile::BandSettings;
using ee::mobile::FilterType;
using ee::mobile::ParametricEqualizer;
using ee::mobile::platform_effect_parameter_ranges;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void disable_all(ParametricEqualizer& eq) {
  for (std::size_t i = 0; i < ParametricEqualizer::band_count; ++i) {
    eq.set_band(i, {.type = FilterType::off, .frequency_hz = 1000.0F, .enabled = false});
  }
}

void test_default_response_is_flat() {
  ParametricEqualizer eq;
  for (const float frequency : {20.0F, 100.0F, 1000.0F, 10000.0F, 20000.0F}) {
    expect(std::abs(eq.response_db(frequency, 48000.0F)) < 0.01F, "default EQ response must be flat");
  }
}

void test_peak_gain_and_channel_isolation() {
  ParametricEqualizer eq;
  disable_all(eq);
  eq.set_band(0, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 1.4F,
                  .gain_db = 9.0F, .enabled = true, .channel_mode = 1});
  expect(eq.response_db(1000.0F, 48000.0F, 0) > 8.9F, "left peak must produce its configured gain");
  expect(std::abs(eq.response_db(1000.0F, 48000.0F, 1)) < 0.01F, "left-only peak must not alter right channel");
}

void test_pcm_peak_routes_only_to_selected_channel() {
  ParametricEqualizer eq;
  disable_all(eq);
  eq.set_band(0, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 1.4F,
                  .gain_db = 12.0F, .enabled = true, .channel_mode = 1});
  constexpr std::size_t frames = 48000U;
  std::vector<float> stereo(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float sample = 0.1F * std::sin(6.28318530718F * 1000.0F * static_cast<float>(frame) / 48000.0F);
    stereo[frame * 2U] = sample;
    stereo[frame * 2U + 1U] = sample;
  }
  eq.process(stereo.data(), frames, 2, 48000.0F);
  double left_energy = 0.0;
  double right_energy = 0.0;
  for (std::size_t frame = frames / 4U; frame < frames; ++frame) {
    left_energy += static_cast<double>(stereo[frame * 2U]) * stereo[frame * 2U];
    right_energy += static_cast<double>(stereo[frame * 2U + 1U]) * stereo[frame * 2U + 1U];
  }
  expect(left_energy > right_energy * 10.0, "left-only band must boost left PCM while preserving right PCM");
}

void test_preamp_is_applied_in_pcm_path() {
  ParametricEqualizer eq;
  eq.set_preamp(6.0F);
  constexpr std::size_t frames = 48000U;
  std::vector<float> samples(frames);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    samples[frame] = 0.1F * std::sin(6.28318530718F * 440.0F * static_cast<float>(frame) / 48000.0F);
  }
  double before = 0.0;
  for (const float sample : samples) before += static_cast<double>(sample) * sample;
  eq.process(samples.data(), frames, 1, 48000.0F);
  double after = 0.0;
  for (const float sample : samples) after += static_cast<double>(sample) * sample;
  expect(after > before * 3.8 && after < before * 4.2, "+6 dB preamp must produce the expected PCM energy");
}

void test_response_curve_matches_point_query() {
  ParametricEqualizer eq;
  disable_all(eq);
  eq.set_band(7, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 2.0F,
                  .gain_db = 6.0F, .enabled = true});
  std::array<float, 129> response{};
  eq.response_curve(response.data(), response.size(), 48000.0F, 0);
  expect(std::abs(response[64] - eq.response_db(632.4555F, 48000.0F)) < 0.02F,
         "response curve samples must match the queried transfer function");
  float point_at_center = 0.0F;
  eq.response_curve(&point_at_center, 1, 48000.0F, 0);
  expect(std::isfinite(point_at_center), "single-point curve must remain finite");
}

void test_every_filter_type_is_finite() {
  constexpr std::array types{FilterType::peaking, FilterType::low_shelf, FilterType::high_shelf,
                             FilterType::low_pass, FilterType::high_pass, FilterType::band_pass,
                             FilterType::notch, FilterType::all_pass, FilterType::off};
  for (const auto type : types) {
    ParametricEqualizer eq;
    disable_all(eq);
    eq.set_band(0, {.type = type, .frequency_hz = 1200.0F, .q = 0.8F, .gain_db = 5.0F});
    const float response = eq.response_db(1200.0F, 48000.0F);
    expect(std::isfinite(response), "each supported filter response must be finite");
    std::array<float, 256> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
      samples[i] = 0.25F * std::sin(6.28318530718F * 1200.0F * static_cast<float>(i) / 48000.0F);
    }
    eq.process(samples.data(), samples.size() / 2U, 2, 48000.0F);
    expect(std::all_of(samples.begin(), samples.end(), [](float sample) { return std::isfinite(sample); }),
           "each supported filter must produce finite PCM");
  }
}

void test_bypass_is_bit_exact() {
  ParametricEqualizer eq;
  eq.set_preamp(12.0F);
  eq.set_bypass(true);
  std::array<float, 8> samples{0.1F, -0.1F, 0.25F, -0.25F, 0.4F, -0.4F, 0.8F, -0.8F};
  const auto original = samples;
  eq.process(samples.data(), 4U, 2, 48000.0F);
  expect(samples == original, "bypass must preserve interleaved float PCM");
  expect(std::abs(eq.response_db(1000.0F, 48000.0F, 0)) < 0.01F, "left bypass response must be flat");
  expect(std::abs(eq.response_db(1000.0F, 48000.0F, 1)) < 0.01F, "right bypass response must be flat");
  std::array<float, 3> response{};
  eq.response_curve(response.data(), response.size(), 48000.0F, 1);
  expect(std::all_of(response.begin(), response.end(), [](float value) { return std::abs(value) < 0.01F; }),
         "bypass response curve must remain flat for Android's band mapping");
}

void test_integer_output_saturates_without_overflow() {
  ParametricEqualizer eq;
  eq.set_preamp(24.0F);
  std::array<std::int16_t, 8> samples{30000, -30000, 10000, -10000, 20000, -20000, 0, 0};
  eq.process(samples.data(), 4U, 2, 48000.0F);
  expect(samples[0] == 32767 && samples[1] == -32768, "int16 output must saturate at PCM limits");
}

void test_invalid_inputs_are_ignored() {
  ParametricEqualizer eq;
  std::array<float, 4> samples{0.1F, 0.2F, 0.3F, 0.4F};
  const auto original = samples;
  eq.process(samples.data(), 2U, 9, 48000.0F);
  eq.process(samples.data(), 2U, 2, std::numeric_limits<float>::quiet_NaN());
  eq.set_band(ParametricEqualizer::band_count, {.frequency_hz = 1000.0F, .gain_db = 12.0F});
  expect(samples == original, "unsupported channel count and sample rate must not alter samples");
  expect(std::abs(eq.response_db(1000.0F, 48000.0F)) < 0.01F, "out-of-range band index must be ignored");
}

void test_android_effect_parameter_ranges() {
  for (std::size_t effect = 0; effect < platform_effect_parameter_ranges.size(); ++effect) {
    const auto& range = platform_effect_parameter_ranges[effect];
    expect(ee::mobile::valid_platform_effect_parameter(static_cast<int>(effect), range.minimum),
           "Android platform effect must accept its minimum parameter");
    expect(ee::mobile::valid_platform_effect_parameter(static_cast<int>(effect), range.maximum),
           "Android platform effect must accept its maximum parameter");
    expect(!ee::mobile::valid_platform_effect_parameter(static_cast<int>(effect), range.minimum - 1),
           "Android platform effect must reject a value below its supported range");
    expect(!ee::mobile::valid_platform_effect_parameter(static_cast<int>(effect), range.maximum + 1),
           "Android platform effect must reject a value above its supported range");
  }
  expect(!ee::mobile::valid_platform_effect_parameter(-1, 0), "unknown Android platform effect id must be rejected");
  expect(!ee::mobile::valid_platform_effect_parameter(4, 0), "out-of-range Android platform effect id must be rejected");
}

void test_graph_drag_mapping() {
  expect(std::abs(ee::mobile::graph_frequency_from_x(0.0F) - 20.0F) < 0.001F,
         "left edge of the graph must map to 20 Hz");
  expect(std::abs(ee::mobile::graph_frequency_from_x(1.0F) - 20000.0F) < 0.1F,
         "right edge of the graph must map to 20 kHz");
  const float center_frequency = ee::mobile::graph_frequency_from_x(0.5F);
  expect(std::abs(center_frequency - std::sqrt(20.0F * 20000.0F)) < 0.1F,
         "graph frequency mapping must be logarithmic");
  expect(std::abs(ee::mobile::graph_frequency_from_x(-1.0F) - 20.0F) < 0.001F &&
             std::abs(ee::mobile::graph_frequency_from_x(2.0F) - 20000.0F) < 0.1F,
         "graph frequency mapping must clamp touches to the plot bounds");
  expect(std::abs(ee::mobile::graph_gain_for_response(4.0F, 8.0F, 12.0F) - 8.0F) < 0.001F,
         "graph gain drag must compensate for the existing filter response");
  expect(ee::mobile::graph_gain_for_response(20.0F, 0.0F, 24.0F) == 24.0F &&
             ee::mobile::graph_gain_for_response(-20.0F, 0.0F, -24.0F) == -24.0F,
         "graph gain drag must stay inside equalizer limits");
}
}  // namespace

int main() {
  test_default_response_is_flat();
  test_peak_gain_and_channel_isolation();
  test_pcm_peak_routes_only_to_selected_channel();
  test_preamp_is_applied_in_pcm_path();
  test_response_curve_matches_point_query();
  test_every_filter_type_is_finite();
  test_bypass_is_bit_exact();
  test_integer_output_saturates_without_overflow();
  test_invalid_inputs_are_ignored();
  test_android_effect_parameter_ranges();
  test_graph_drag_mapping();
  if (failures != 0) {
    std::cerr << failures << " C++ equalizer test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ equalizer and Android effect mapping tests passed\n";
  return EXIT_SUCCESS;
}
