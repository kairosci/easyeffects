/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/mid_side_equalizer_processor.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using ee::mobile::BandSettings;
using ee::mobile::FilterType;
using ee::mobile::MidSideEqualizerProcessor;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void disable_all(MidSideEqualizerProcessor& processor) {
  for (std::size_t band = 0; band < MidSideEqualizerProcessor::band_count; ++band) {
    processor.set_mid_band(band, {.type = FilterType::off, .frequency_hz = 1000.0F, .enabled = false});
    processor.set_split_channels(true);
    processor.set_side_band(band, {.type = FilterType::off, .frequency_hz = 1000.0F, .enabled = false});
  }
}

void test_linked_mode_applies_mid_filter_to_side() {
  MidSideEqualizerProcessor processor;
  processor.set_mid_band(0, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 1.4F, .gain_db = 6.0F});
  expect(std::abs(processor.mid_response_db(1000.0F, 48000.0F) - 6.0F) < 0.1F,
         "Mid filter should apply its gain at the center frequency");
  expect(std::abs(processor.side_response_db(1000.0F, 48000.0F) - 6.0F) < 0.1F,
         "linked mode should mirror the Mid filter to Side");
}

void test_mid_only_boost_changes_center_without_side() {
  MidSideEqualizerProcessor processor;
  disable_all(processor);
  processor.set_split_channels(true);
  processor.set_mid_band(0, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 1.4F, .gain_db = 9.0F});
  constexpr std::size_t frames = 48000U;
  std::vector<float> stereo(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float sample = 0.1F * std::sin(6.28318530718F * 1000.0F * static_cast<float>(frame) / 48000.0F);
    stereo[frame * 2U] = sample;
    stereo[frame * 2U + 1U] = sample;
  }
  processor.process(stereo.data(), frames, 48000.0F);
  double output_energy = 0.0;
  double input_energy = 0.0;
  for (std::size_t frame = frames / 4U; frame < frames; ++frame) {
    output_energy += static_cast<double>(stereo[frame * 2U]) * stereo[frame * 2U];
    input_energy += static_cast<double>(0.1F * std::sin(6.28318530718F * 1000.0F *
                                 static_cast<float>(frame) / 48000.0F)) *
                    (0.1F * std::sin(6.28318530718F * 1000.0F * static_cast<float>(frame) / 48000.0F));
  }
  expect(output_energy > input_energy * 7.0, "Mid EQ should boost mono-center content");
  expect(std::abs(stereo[100U] - stereo[101U]) < 0.0001F,
         "a Mid-only EQ must preserve identical left and right output");
}

void test_side_only_boost_changes_anti_phase_content() {
  MidSideEqualizerProcessor processor;
  disable_all(processor);
  processor.set_split_channels(true);
  processor.set_side_band(0, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 1.4F, .gain_db = 9.0F});
  constexpr std::size_t frames = 48000U;
  std::vector<float> stereo(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float sample = 0.1F * std::sin(6.28318530718F * 1000.0F * static_cast<float>(frame) / 48000.0F);
    stereo[frame * 2U] = sample;
    stereo[frame * 2U + 1U] = -sample;
  }
  processor.process(stereo.data(), frames, 48000.0F);
  double left_energy = 0.0;
  for (std::size_t frame = frames / 4U; frame < frames; ++frame) {
    left_energy += static_cast<double>(stereo[frame * 2U]) * stereo[frame * 2U];
    expect(std::abs(stereo[frame * 2U] + stereo[frame * 2U + 1U]) < 0.0001F,
           "Side-only EQ must preserve anti-phase output");
  }
  expect(left_energy > 500.0, "Side EQ should boost anti-phase content");
}

void test_bypass_is_bit_exact_and_balance_is_independent() {
  MidSideEqualizerProcessor processor;
  std::array<float, 8> stereo{0.1F, -0.2F, 0.3F, -0.4F, 0.5F, -0.6F, 0.7F, -0.8F};
  const auto original = stereo;
  processor.set_bypass(true);
  processor.set_output_gain_db(12.0F);
  processor.process(stereo.data(), stereo.size() / 2U, 48000.0F);
  expect(stereo == original, "bypass must preserve PCM and skip output gain");

  processor.set_bypass(false);
  processor.set_input_gain_db(0.0F);
  processor.set_output_gain_db(0.0F);
  processor.set_balance(100.0F);
  processor.process(stereo.data(), stereo.size() / 2U, 48000.0F);
  expect(std::abs(stereo[0]) < 0.0001F && std::abs(stereo[1] + 0.2F) < 0.0001F,
         "full right balance should attenuate only the left output");
}

void test_input_and_output_gains_are_applied() {
  MidSideEqualizerProcessor processor;
  processor.set_input_gain_db(6.0F);
  processor.set_output_gain_db(-3.0F);
  constexpr std::size_t frames = 48000U;
  std::vector<float> stereo(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float sample = 0.1F * std::sin(6.28318530718F * 440.0F * static_cast<float>(frame) / 48000.0F);
    stereo[frame * 2U] = sample;
    stereo[frame * 2U + 1U] = sample;
  }
  processor.process(stereo.data(), frames, 48000.0F);
  double energy = 0.0;
  for (std::size_t frame = frames / 4U; frame < frames; ++frame) {
    energy += static_cast<double>(stereo[frame * 2U]) * stereo[frame * 2U];
  }
  const double expected = static_cast<double>(frames * 3U / 4U) * 0.005 * 2.0;
  expect(energy > expected * 0.97 && energy < expected * 1.03,
         "+6 dB input and -3 dB output gains should produce +3 dB PCM energy");
}

void test_pitch_shift_changes_band_frequency() {
  MidSideEqualizerProcessor processor;
  processor.set_mid_band(0, {.type = FilterType::peaking, .frequency_hz = 1000.0F, .q = 1.4F, .gain_db = 6.0F});
  processor.set_pitch_mid(12.0F);
  expect(processor.mid_response_db(2000.0F, 48000.0F) > 5.9F,
         "one octave pitch shift should double Mid filter frequency");
  expect(std::abs(processor.side_response_db(1000.0F, 48000.0F) - 6.0F) < 0.1F,
         "Mid pitch shift must not alter linked Side frequency");
}
}  // namespace

int main() {
  test_linked_mode_applies_mid_filter_to_side();
  test_mid_only_boost_changes_center_without_side();
  test_side_only_boost_changes_anti_phase_content();
  test_bypass_is_bit_exact_and_balance_is_independent();
  test_input_and_output_gains_are_applied();
  test_pitch_shift_changes_band_frequency();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "PASS: Mid/Side EQ linked and split filters, mono/anti-phase routing, bypass, balance, and pitch shift\n";
  return EXIT_SUCCESS;
}
