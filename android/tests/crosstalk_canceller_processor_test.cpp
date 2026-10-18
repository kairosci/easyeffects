/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/crosstalk_canceller_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using ee::mobile::CrosstalkCancellerProcessor;
using ee::mobile::CrosstalkParameters;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_desktop_defaults_and_parameter_limits() {
  CrosstalkCancellerProcessor processor;
  const auto defaults = processor.parameters();
  expect(defaults.delay_us == 313.0F && defaults.decay_db == -2.0F && !defaults.phantom_center_only &&
             defaults.input_gain_db == 0.0F && defaults.output_gain_db == 0.0F && !defaults.bypass,
         "all parameters must match desktop Crosstalk Canceller defaults");
  CrosstalkParameters p;
  p.delay_us = 100.0F;
  p.decay_db = -20.0F;
  p.input_gain_db = 100.0F;
  p.output_gain_db = -100.0F;
  processor.set_parameters(p);
  const auto bounded = processor.parameters();
  expect(bounded.delay_us == 200.0F && bounded.decay_db == -6.0F && bounded.input_gain_db == 36.0F &&
             bounded.output_gain_db == -36.0F,
         "delay, decay, and common gains must clamp to desktop settings bounds");
}

void test_stereo_cancellation_is_delayed_and_filter_shaped() {
  CrosstalkCancellerProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare at the system stream rate");
  auto p = processor.parameters();
  p.delay_us = 313.0F;
  p.decay_db = -2.0F;
  processor.set_parameters(p);
  constexpr std::size_t frames = 12000U;
  std::vector<float> signal(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    signal[frame * 2U] = 0.3F * std::sin(6.28318530718F * 1000.0F * static_cast<float>(frame) / 48000.0F);
  }
  processor.process(signal.data(), frames);
  double right_energy = 0.0;
  for (std::size_t frame = 1000U; frame < frames; ++frame) {
    const double sample = signal[frame * 2U + 1U];
    right_energy += sample * sample;
  }
  expect(right_energy > 0.01, "left-channel energy must create delayed, filtered right-channel cancellation");
  expect(std::isfinite(processor.levels().output_right_db), "output meter must expose processed right-channel level");
}

void test_phantom_center_only_preserves_side_signal() {
  CrosstalkCancellerProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare for phantom-center mode");
  auto p = processor.parameters();
  p.phantom_center_only = true;
  processor.set_parameters(p);
  std::array<float, 128> signal{};
  for (std::size_t frame = 0; frame < signal.size() / 2U; ++frame) {
    signal[frame * 2U] = 0.2F;
    signal[frame * 2U + 1U] = -0.2F;
  }
  const auto original = signal;
  processor.process(signal.data(), signal.size() / 2U);
  expect(signal == original, "phantom-center-only processing must pass side-only stereo content unchanged");

  processor.reset();
  processor.process(signal.data(), signal.size() / 2U);
  expect(std::all_of(signal.begin(), signal.end(), [](float value) { return std::isfinite(value); }),
         "reset phantom-center processing must remain finite");
}

void test_bypass_and_common_gain_stages() {
  CrosstalkCancellerProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare for bypass and gain controls");
  auto p = processor.parameters();
  p.bypass = true;
  p.input_gain_db = 12.0F;
  p.output_gain_db = -6.0F;
  processor.set_parameters(p);
  std::array<float, 8> samples{0.1F, -0.1F, 0.25F, -0.25F, 0.5F, -0.5F, 0.75F, -0.75F};
  const auto original = samples;
  processor.process(samples.data(), samples.size() / 2U);
  expect(samples == original, "bypass must preserve interleaved audio exactly");

  p.bypass = false;
  p.input_gain_db = 6.0F;
  p.output_gain_db = -3.0F;
  p.decay_db = 0.0F;
  processor.set_parameters(p);
  std::array<float, 2> first_frame{0.25F, -0.25F};
  processor.process(first_frame.data(), 1U);
  constexpr float combined_gain = 1.41253754F;
  expect(std::abs(first_frame[0] - 0.25F * combined_gain) < 0.0001F &&
             std::abs(first_frame[1] + 0.25F * combined_gain) < 0.0001F,
         "input and output gains must bracket the DSP as on desktop");
  const auto levels = processor.levels();
  expect(levels.input_left_db > -10.0F && std::isfinite(levels.output_left_db),
         "input/output meters must report per-channel dBFS peaks");
}

void test_block_continuity_reset_and_invalid_inputs() {
  CrosstalkCancellerProcessor whole;
  CrosstalkCancellerProcessor chunked;
  expect(whole.prepare(44100.0F) && chunked.prepare(44100.0F), "processors must prepare at 44.1 kHz");
  auto p = whole.parameters();
  p.delay_us = 440.0F;
  p.phantom_center_only = true;
  whole.set_parameters(p);
  chunked.set_parameters(p);
  std::array<float, 256> full{};
  for (std::size_t i = 0; i < full.size(); ++i) {
    full[i] = 0.2F * std::sin(0.13F * static_cast<float>(i));
  }
  auto pieces = full;
  whole.process(full.data(), full.size() / 2U);
  chunked.process(pieces.data(), 32U);
  chunked.process(pieces.data() + 64U, 48U);
  chunked.process(pieces.data() + 160U, 48U);
  expect(full == pieces, "crosstalk filters and delay lines must remain continuous across callback blocks");
  whole.reset();
  expect(whole.parameters().delay_us == 313.0F && whole.parameters().decay_db == -2.0F &&
             !whole.parameters().phantom_center_only,
         "reset must restore all desktop default settings and schedule state cleanup on the audio thread");
  expect(!whole.prepare(0.0F) && !whole.prepare(1000000.0F), "unsupported sample rates must be rejected safely");
  whole.process(nullptr, 8U);
}
}  // namespace

int main() {
  test_desktop_defaults_and_parameter_limits();
  test_stereo_cancellation_is_delayed_and_filter_shaped();
  test_phantom_center_only_preserves_side_signal();
  test_bypass_and_common_gain_stages();
  test_block_continuity_reset_and_invalid_inputs();
  if (failures != 0) {
    std::cerr << failures << " C++ Crosstalk Canceller test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ Crosstalk Canceller tests passed\n";
  return EXIT_SUCCESS;
}
