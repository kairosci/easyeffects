/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/crossfeed_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using ee::mobile::CrossfeedPreset;
using ee::mobile::CrossfeedProcessor;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

auto make_signal(const std::size_t frames, const float frequency = 180.0F) -> std::vector<float> {
  std::vector<float> signal(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float phase = 6.28318530718F * frequency * static_cast<float>(frame) / 48000.0F;
    signal[frame * 2U] = 0.3F * std::sin(phase);
    signal[frame * 2U + 1U] = 0.0F;
  }
  return signal;
}

void test_presets_match_desktop_values() {
  CrossfeedProcessor processor;
  auto p = processor.parameters();
  expect(p.cutoff_hz == 700 && std::abs(p.feed_db - 4.5F) < 0.001F, "default preset must match desktop defaults");
  processor.apply_preset(CrossfeedPreset::cmoy);
  p = processor.parameters();
  expect(p.cutoff_hz == 700 && std::abs(p.feed_db - 6.0F) < 0.001F, "Cmoy preset must match desktop values");
  processor.apply_preset(CrossfeedPreset::jmeier);
  p = processor.parameters();
  expect(p.cutoff_hz == 650 && std::abs(p.feed_db - 9.5F) < 0.001F, "Jmeier preset must match desktop values");
}

void test_pcm_matches_libbs2b_reference() {
  CrossfeedProcessor processor;
  processor.set_cutoff_hz(900);
  processor.set_feed_db(7.3F);
  processor.set_input_gain_db(3.0F);
  processor.set_output_gain_db(-2.0F);
  auto actual = make_signal(4096U);
  auto expected = actual;

  auto reference = bs2b_open();
  expect(reference != nullptr, "libbs2b reference context must initialize");
  if (reference == nullptr) return;
  bs2b_set_level(reference, static_cast<uint32_t>(900 | (73 << 16)));
  bs2b_set_srate(reference, 48000U);
  constexpr float input_gain = 1.41253754F;
  constexpr float output_gain = 0.79432823F;
  for (auto& sample : expected) sample *= input_gain;
  bs2b_cross_feed_f(reference, expected.data(), static_cast<int>(expected.size() / 2U));
  for (auto& sample : expected) sample *= output_gain;

  processor.process(actual.data(), actual.size() / 2U, 48000.0F);
  float maximum_error = 0.0F;
  for (std::size_t i = 0; i < actual.size(); ++i) maximum_error = std::max(maximum_error, std::abs(actual[i] - expected[i]));
  expect(maximum_error < 1.0e-6F, "Android C++ processing must match libbs2b's desktop DSP pipeline");
  bs2b_close(reference);
}

void test_mono_left_content_crossfeeds_to_right() {
  CrossfeedProcessor processor;
  auto signal = make_signal(12000U, 120.0F);
  processor.process(signal.data(), signal.size() / 2U, 48000.0F);
  double right_energy = 0.0;
  for (std::size_t frame = 2000U; frame < signal.size() / 2U; ++frame) {
    const double sample = signal[frame * 2U + 1U];
    right_energy += sample * sample;
  }
  expect(right_energy > 1.0e-4, "left channel low-frequency content must produce audible crossfeed in right channel");
}

void test_chunked_processing_matches_single_buffer() {
  CrossfeedProcessor whole;
  CrossfeedProcessor chunked;
  auto all = make_signal(8192U);
  auto pieces = all;
  whole.process(all.data(), all.size() / 2U, 48000.0F);
  chunked.process(pieces.data(), 1024U, 48000.0F);
  chunked.process(pieces.data() + 2048U, 3072U, 48000.0F);
  chunked.process(pieces.data() + 8192U, 4096U, 48000.0F);
  expect(all == pieces, "streaming in audio callback chunks must preserve filter state and output");
}

void test_bypass_and_parameter_clamps() {
  CrossfeedProcessor processor;
  processor.set_input_gain_db(12.0F);
  processor.set_output_gain_db(-12.0F);
  processor.set_bypass(true);
  std::array<float, 8> samples{0.1F, -0.1F, 0.25F, -0.25F, 0.4F, -0.4F, 0.8F, -0.8F};
  const auto original = samples;
  processor.process(samples.data(), samples.size() / 2U, 48000.0F);
  expect(samples == original, "bypass must preserve samples exactly, including plugin gain controls");

  processor.set_bypass(false);
  processor.set_cutoff_hz(-100);
  processor.set_feed_db(100.0F);
  processor.set_input_gain_db(100.0F);
  const auto p = processor.parameters();
  expect(p.cutoff_hz == BS2B_MINFCUT, "cutoff must clamp to libbs2b minimum");
  expect(std::abs(p.feed_db - 15.0F) < 0.001F, "feed must clamp to the desktop UI maximum");
  expect(std::abs(p.input_gain_db - 36.0F) < 0.001F, "input gain must clamp to its safe range");
}

void test_reset_and_input_output_meters() {
  CrossfeedProcessor processor;
  processor.set_input_gain_db(6.0F);
  processor.set_output_gain_db(-3.0F);
  std::array<float, 8> samples{0.25F, -0.5F, 0.5F, -0.25F, 0.75F, -0.125F, 0.125F, -0.75F};
  processor.process(samples.data(), 4U, 48000.0F);
  const auto levels = processor.levels();
  expect(levels.input_left_db > 0.0F && levels.input_right_db > 0.0F,
         "input meters must include the desktop input gain stage");
  expect(std::isfinite(levels.output_left_db) && std::isfinite(levels.output_right_db),
         "output meters must report finite block peak values");
  processor.set_cutoff_hz(1500);
  processor.set_feed_db(10.0F);
  processor.set_bypass(true);
  processor.reset();
  const auto p = processor.parameters();
  expect(p.cutoff_hz == 700 && std::abs(p.feed_db - 4.5F) < 0.001F && !p.bypass &&
             p.input_gain_db == 0.0F && p.output_gain_db == 0.0F,
         "reset must restore desktop default plugin parameters and request DSP state clear");
}

void test_invalid_audio_inputs_are_ignored() {
  CrossfeedProcessor processor;
  std::array<float, 4> samples{0.1F, 0.2F, 0.3F, 0.4F};
  const auto original = samples;
  processor.process(samples.data(), 2U, 1000.0F);
  processor.process(samples.data(), 2U, 48000.0F / 0.0F);
  expect(samples == original, "unsupported or invalid sample rate must not modify the buffer");
}
}  // namespace

int main() {
  test_presets_match_desktop_values();
  test_pcm_matches_libbs2b_reference();
  test_mono_left_content_crossfeeds_to_right();
  test_chunked_processing_matches_single_buffer();
  test_bypass_and_parameter_clamps();
  test_reset_and_input_output_meters();
  test_invalid_audio_inputs_are_ignored();
  if (failures != 0) {
    std::cerr << failures << " C++ Crossfeed test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ Crossfeed tests passed\n";
  return EXIT_SUCCESS;
}
