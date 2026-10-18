/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/crusher_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
using ee::mobile::CrusherMode;
using ee::mobile::CrusherParameters;
using ee::mobile::CrusherProcessor;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_bypass_is_bit_exact() {
  CrusherProcessor processor;
  auto p = processor.parameters();
  p.bypass = true;
  p.input_gain_db = 12.0F;
  p.output_gain_db = -6.0F;
  processor.set_parameters(p);
  std::array<float, 8> samples{0.1F, -0.2F, 0.3F, -0.4F, 0.5F, -0.6F, 0.7F, -0.8F};
  const auto original = samples;
  processor.process(samples.data(), samples.size() / 2U, 48000.0F);
  expect(samples == original, "bypass must pass stereo PCM bit-exactly without applying gain controls");
}

void test_sample_rate_reduction_holds_samples() {
  CrusherProcessor processor;
  auto p = processor.parameters();
  p.sample_reduction = 2.0F;
  p.bit_reduction = 16.0F;
  p.morph = 1.0F;
  p.anti_aliasing = 0.0F;
  processor.set_parameters(p);
  std::array<float, 8> samples{0.1F, 0.1F, 0.2F, 0.2F, 0.3F, 0.3F, 0.4F, 0.4F};
  processor.process(samples.data(), 4U, 48000.0F);
  expect(samples[0] == 0.0F && samples[1] == 0.0F, "sample-rate reduction starts with held zero state");
  expect(std::abs(samples[2] - 0.2F) < 0.0001F && std::abs(samples[3] - 0.2F) < 0.0001F,
         "sample-rate reduction must hold the input for the configured interval");
  expect(std::abs(samples[4] - 0.2F) < 0.0001F && std::abs(samples[5] - 0.2F) < 0.0001F,
         "sample-rate reduction must keep the held sample until the next interval");
  expect(std::abs(samples[6] - 0.4F) < 0.0001F && std::abs(samples[7] - 0.4F) < 0.0001F,
         "sample-rate reduction must update held samples on interval boundaries");
}

void test_bit_reduction_is_audible_and_modes_are_finite() {
  std::array<float, 10> input{-0.9F, -0.7F, -0.5F, -0.3F, -0.1F, 0.1F, 0.3F, 0.5F, 0.7F, 0.9F};
  auto linear = input;
  auto logarithmic = input;
  CrusherProcessor linear_processor;
  CrusherProcessor logarithmic_processor;
  auto p = linear_processor.parameters();
  p.sample_reduction = 1.0F;
  p.morph = 1.0F;
  p.bit_reduction = 2.0F;
  p.anti_aliasing = 0.0F;
  p.mode = CrusherMode::linear;
  linear_processor.set_parameters(p);
  linear_processor.process(linear.data(), linear.size() / 2U, 48000.0F);
  p.mode = CrusherMode::logarithmic;
  logarithmic_processor.set_parameters(p);
  logarithmic_processor.process(logarithmic.data(), logarithmic.size() / 2U, 48000.0F);
  expect(linear != input, "bit reduction must alter the input signal");
  expect(logarithmic != input, "logarithmic bit reduction must alter the input signal");
  expect(std::all_of(linear.begin(), linear.end(), [](float x) { return std::isfinite(x); }) &&
             std::all_of(logarithmic.begin(), logarithmic.end(), [](float x) { return std::isfinite(x); }),
         "both quantization modes must produce finite output including zero crossings");
  expect(linear != logarithmic, "linear and logarithmic quantization must have distinct transfer curves");
}

void test_morph_and_gain_are_applied() {
  CrusherProcessor processor;
  auto p = processor.parameters();
  p.bit_reduction = 1.0F;
  p.anti_aliasing = 0.0F;
  p.sample_reduction = 1.0F;
  p.morph = 0.0F;
  p.input_gain_db = 6.0F;
  p.output_gain_db = -3.0F;
  processor.set_parameters(p);
  std::array<float, 4> samples{0.1F, 0.2F, 0.3F, 0.4F};
  constexpr float expected_gain = 1.99526231F * 0.70794576F;
  processor.process(samples.data(), 2U, 48000.0F);
  expect(std::abs(samples[0] - 0.1F * expected_gain) < 0.0001F &&
             std::abs(samples[3] - 0.4F * expected_gain) < 0.0001F,
         "zero morph must preserve the dry signal through configured input and output gain");
}

void test_settings_clamp_to_desktop_ranges() {
  CrusherProcessor processor;
  CrusherParameters p;
  p.bit_reduction = 100.0F;
  p.morph = -1.0F;
  p.dc_db = 100.0F;
  p.anti_aliasing = 2.0F;
  p.sample_reduction = 0.0F;
  p.lfo_range = 500.0F;
  p.lfo_rate_hz = -1.0F;
  p.input_gain_db = 100.0F;
  p.output_gain_db = -100.0F;
  processor.set_parameters(p);
  const auto actual = processor.parameters();
  expect(actual.bit_reduction == 16.0F && actual.morph == 0.0F && actual.dc_db == 12.041F,
         "bit reduction, morph, and DC must clamp to EasyEffects settings bounds");
  expect(actual.anti_aliasing == 1.0F && actual.sample_reduction == 1.0F && actual.lfo_range == 250.0F,
         "anti-aliasing and sample controls must clamp to EasyEffects settings bounds");
  expect(actual.lfo_rate_hz == 0.01F && actual.input_gain_db == 36.0F && actual.output_gain_db == -36.0F,
         "LFO rate and gain controls must clamp to their safe bounds");
}

void test_reset_and_input_output_meters() {
  CrusherProcessor processor;
  auto p = processor.parameters();
  p.bit_reduction = 10.0F;
  p.lfo_active = true;
  p.input_gain_db = 6.0F;
  processor.set_parameters(p);
  std::array<float, 8> samples{0.25F, -0.5F, 0.5F, -0.25F, 0.75F, -0.125F, 0.125F, -0.75F};
  processor.process(samples.data(), 4U, 48000.0F);
  const auto levels = processor.levels();
  expect(levels.input_left_db > 0.0F && levels.input_right_db > 0.0F &&
             std::isfinite(levels.output_left_db) && std::isfinite(levels.output_right_db),
         "Crusher meters must expose per-channel input and processed output peaks");
  processor.reset();
  const auto defaults = processor.parameters();
  expect(defaults.bit_reduction == 4.0F && defaults.morph == 0.5F && !defaults.lfo_active &&
             defaults.input_gain_db == 0.0F && !defaults.bypass,
         "reset must restore every desktop Crusher parameter and clear process state safely");
}

void test_chunked_processing_matches_single_buffer() {
  CrusherProcessor whole;
  CrusherProcessor chunked;
  auto whole_data = std::array<float, 24>{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F,
                                          0.7F, 0.8F, 0.9F, 1.0F, 0.2F, 0.3F,
                                          0.4F, 0.5F, 0.6F, 0.7F, 0.8F, 0.9F,
                                          1.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F};
  auto chunks = whole_data;
  auto p = whole.parameters();
  p.sample_reduction = 3.7F;
  p.lfo_active = true;
  p.lfo_range = 3.0F;
  p.lfo_rate_hz = 7.0F;
  whole.set_parameters(p);
  chunked.set_parameters(p);
  whole.process(whole_data.data(), whole_data.size() / 2U, 48000.0F);
  chunked.process(chunks.data(), 4U, 48000.0F);
  chunked.process(chunks.data() + 8U, 8U, 48000.0F);
  expect(whole_data == chunks, "LFO and sample-hold state must remain continuous across audio blocks");
}
}  // namespace

int main() {
  test_bypass_is_bit_exact();
  test_sample_rate_reduction_holds_samples();
  test_bit_reduction_is_audible_and_modes_are_finite();
  test_morph_and_gain_are_applied();
  test_settings_clamp_to_desktop_ranges();
  test_reset_and_input_output_meters();
  test_chunked_processing_matches_single_buffer();
  if (failures != 0) {
    std::cerr << failures << " C++ Crusher test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ Crusher tests passed\n";
  return EXIT_SUCCESS;
}
