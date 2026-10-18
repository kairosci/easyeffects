/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/bass_enhancer_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using ee::mobile::BassEnhancerParameters;
using ee::mobile::BassEnhancerProcessor;

constexpr float sample_rate = 48000.0F;
constexpr std::size_t block_frames = 512U;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

auto make_tone(const std::size_t frames, const float frequency, const float amplitude) -> std::vector<float> {
  std::vector<float> signal(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float phase = 6.28318530718F * frequency * static_cast<float>(frame) / sample_rate;
    const float sample = amplitude * std::sin(phase);
    signal[frame * 2U] = sample;
    signal[frame * 2U + 1U] = sample;
  }
  return signal;
}

/** Feeds an interleaved stereo signal to the processor in consecutive slices. */
void process_signal(BassEnhancerProcessor& processor, std::vector<float>& signal) {
  const std::size_t total_frames = signal.size() / 2U;
  for (std::size_t offset = 0; offset < total_frames; offset += block_frames) {
    const std::size_t frames = std::min(block_frames, total_frames - offset);
    processor.process(signal.data() + (offset * 2U), frames);
  }
}

auto tail_peak(const std::vector<float>& signal, const std::size_t tail_frames) -> float {
  const std::size_t total_frames = signal.size() / 2U;
  const std::size_t start = total_frames > tail_frames ? total_frames - tail_frames : 0U;
  float peak = 0.0F;
  for (std::size_t frame = start; frame < total_frames; ++frame) {
    peak = std::max(peak, std::abs(signal[frame * 2U]));
    peak = std::max(peak, std::abs(signal[frame * 2U + 1U]));
  }
  return peak;
}

auto max_difference(const std::vector<float>& lhs, const std::vector<float>& rhs) -> float {
  float difference = 0.0F;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    difference = std::max(difference, std::abs(lhs[i] - rhs[i]));
  }
  return difference;
}

void test_defaults_and_clamps() {
  BassEnhancerProcessor processor;
  const auto defaults = processor.parameters();
  expect(std::abs(defaults.amount_db) < 1.0e-6F, "default amount must be 0 dB");
  expect(std::abs(defaults.harmonics - 8.5F) < 1.0e-6F, "default harmonics must be 8.5");
  expect(std::abs(defaults.scope_hz - 100.0F) < 1.0e-6F, "default scope must be 100 Hz");
  expect(std::abs(defaults.floor_hz - 20.0F) < 1.0e-6F, "default floor must be 20 Hz");
  expect(std::abs(defaults.blend) < 1.0e-6F, "default blend must be 0");
  expect(!defaults.floor_active && !defaults.listen && !defaults.bypass, "toggles default off");
  expect(defaults.input_gain_db == 0.0F && defaults.output_gain_db == 0.0F, "common gains default to 0 dB");

  BassEnhancerParameters values;
  values.amount_db = 500.0F;
  values.harmonics = 99.0F;
  values.scope_hz = 1.0F;
  values.floor_hz = 1000.0F;
  values.blend = -99.0F;
  values.input_gain_db = 500.0F;
  values.output_gain_db = -500.0F;
  processor.set_parameters(values);
  const auto clamped = processor.parameters();
  expect(clamped.amount_db == 36.0F, "amount must clamp to the desktop maximum 36 dB");
  expect(clamped.harmonics == 10.0F, "harmonics must clamp to the desktop maximum 10");
  expect(clamped.scope_hz == 10.0F, "scope must clamp to the desktop minimum 10 Hz");
  expect(clamped.floor_hz == 120.0F, "floor must clamp to the desktop maximum 120 Hz");
  expect(clamped.blend == -10.0F, "blend must clamp to the desktop minimum -10");
  expect(clamped.input_gain_db == 36.0F && clamped.output_gain_db == -36.0F, "gains must clamp to +-36 dB");

  values.amount_db = -500.0F;
  values.harmonics = 0.0F;
  values.scope_hz = 1.0e9F;
  values.floor_hz = 0.0F;
  values.blend = 1.0e9F;
  processor.set_parameters(values);
  const auto other = processor.parameters();
  expect(other.amount_db == -100.0F, "amount must clamp to the desktop minimum -100 dB");
  expect(other.harmonics == 0.1F, "harmonics must clamp to the desktop minimum 0.1");
  expect(other.scope_hz == 250.0F, "scope must clamp to the desktop maximum 250 Hz");
  expect(other.floor_hz == 10.0F, "floor must clamp to the desktop minimum 10 Hz");
  expect(other.blend == 10.0F, "blend must clamp to the desktop maximum 10");
}

void test_invalid_sample_rate() {
  BassEnhancerProcessor processor;
  std::vector<float> block(block_frames * 2U, 0.0F);
  processor.process(block.data(), block_frames);  // before prepare: no-op
  expect(!processor.prepare(0.0F), "zero sample rate must be rejected");
  expect(!processor.prepare(-48000.0F), "negative sample rate must be rejected");
  expect(!processor.prepare(std::nan("")), "NaN sample rate must be rejected");
  expect(processor.prepare(sample_rate), "valid sample rate must prepare the filters");
  expect(std::abs(processor.sample_rate() - sample_rate) < 1.0F, "sample_rate must report the prepared rate");
}

void test_bypass_preserves_samples() {
  BassEnhancerProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  BassEnhancerParameters parameters;
  parameters.input_gain_db = 6.0F;
  parameters.output_gain_db = -9.0F;
  parameters.amount_db = 12.0F;
  parameters.bypass = true;
  processor.set_parameters(parameters);
  std::vector<float> samples{0.1F, -0.2F, 0.3F, -0.4F, 0.5F, -0.6F, 0.7F, -0.8F};
  const auto original = samples;
  processor.process(samples.data(), 4U);
  expect(samples == original, "bypass must preserve samples exactly, including common gains");
}

void test_amount_controls_wet_mix() {
  BassEnhancerProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  BassEnhancerParameters parameters;
  parameters.amount_db = -100.0F;  // only the dry path remains
  processor.set_parameters(parameters);

  auto signal = make_tone(static_cast<std::size_t>(sample_rate), 80.0F, 0.3F);
  const auto original = signal;
  process_signal(processor, signal);
  expect(max_difference(signal, original) < 1.0e-3F,
         "a fully attenuated wet mix must leave the dry signal almost untouched");
}

void test_scope_lowpass_attenuates_high_tone() {
  BassEnhancerProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  BassEnhancerParameters parameters;
  parameters.harmonics = 8.5F;
  parameters.listen = true;  // monitor the processed band only

  parameters.scope_hz = 250.0F;
  processor.set_parameters(parameters);
  auto passed = make_tone(static_cast<std::size_t>(sample_rate), 100.0F, 0.5F);
  process_signal(processor, passed);
  const float passed_peak = tail_peak(passed, 8192U);

  parameters.scope_hz = 25.0F;
  processor.set_parameters(parameters);
  auto rejected = make_tone(static_cast<std::size_t>(sample_rate), 100.0F, 0.5F);
  process_signal(processor, rejected);
  const float rejected_peak = tail_peak(rejected, 8192U);

  expect(passed_peak > 0.05F, "a tone below the scope lowpass must reach the processed band");
  expect(rejected_peak * 3.0F < passed_peak, "a tone above the scope lowpass must be attenuated");
}

void test_floor_highpass_attenuates_low_tone() {
  BassEnhancerProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  BassEnhancerParameters parameters;
  parameters.scope_hz = 250.0F;
  parameters.listen = true;
  parameters.floor_active = false;

  processor.set_parameters(parameters);
  auto without_floor = make_tone(static_cast<std::size_t>(sample_rate), 40.0F, 0.5F);
  process_signal(processor, without_floor);
  const float without_peak = tail_peak(without_floor, 8192U);

  parameters.floor_active = true;
  parameters.floor_hz = 120.0F;
  processor.set_parameters(parameters);
  auto with_floor = make_tone(static_cast<std::size_t>(sample_rate), 40.0F, 0.5F);
  process_signal(processor, with_floor);
  const float with_peak = tail_peak(with_floor, 8192U);

  expect(with_peak * 2.0F < without_peak, "the floor highpass must attenuate content below its cutoff");
}

void test_saturation_emits_harmonics() {
  BassEnhancerProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  const BassEnhancerParameters parameters;
  processor.set_parameters(parameters);

  auto tone = make_tone(static_cast<std::size_t>(sample_rate), 60.0F, 0.5F);
  process_signal(processor, tone);
  const float loud = processor.levels().harmonics_level_db;
  expect(loud > -60.0F, "a loud bass tone must register saturation harmonics");

  std::vector<float> silence(static_cast<std::size_t>(sample_rate) * 4U, 0.0F);
  process_signal(processor, silence);
  const float quiet = processor.levels().harmonics_level_db;
  expect(quiet < loud - 30.0F, "the harmonics peak-hold meter must decay in silence");
}

void test_block_partition_is_stable() {
  BassEnhancerProcessor single;
  BassEnhancerProcessor blocked;
  expect(single.prepare(sample_rate) && blocked.prepare(sample_rate), "processors must prepare");

  const std::size_t frames = static_cast<std::size_t>(sample_rate);
  auto whole = make_tone(frames, 90.0F, 0.4F);
  auto sliced = whole;
  single.process(whole.data(), frames);
  process_signal(blocked, sliced);
  expect(max_difference(whole, sliced) < 1.0e-6F, "processing in callback-sized blocks must match a single call");
}

void test_reset_restores_defaults() {
  BassEnhancerProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  BassEnhancerParameters parameters;
  parameters.amount_db = 18.0F;
  parameters.harmonics = 2.0F;
  parameters.scope_hz = 200.0F;
  parameters.floor_active = true;
  parameters.listen = true;
  processor.set_parameters(parameters);
  processor.reset();
  const auto defaults = processor.parameters();
  expect(std::abs(defaults.amount_db) < 1.0e-6F && std::abs(defaults.harmonics - 8.5F) < 1.0e-6F &&
             !defaults.floor_active && !defaults.listen,
         "reset must restore the desktop defaults");
}
}  // namespace

int main() {
  test_defaults_and_clamps();
  test_invalid_sample_rate();
  test_bypass_preserves_samples();
  test_amount_controls_wet_mix();
  test_scope_lowpass_attenuates_high_tone();
  test_floor_highpass_attenuates_low_tone();
  test_saturation_emits_harmonics();
  test_block_partition_is_stable();
  test_reset_restores_defaults();
  if (failures != 0) {
    std::cerr << failures << " C++ Bass Enhancer test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ Bass Enhancer tests passed\n";
  return EXIT_SUCCESS;
}
