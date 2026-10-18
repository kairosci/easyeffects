/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/autogain_processor.hpp"

#include <ebur128.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using ee::mobile::AutogainParameters;
using ee::mobile::AutogainProcessor;
using ee::mobile::AutogainReference;

constexpr float sample_rate = 48000.0F;
constexpr std::size_t block_frames = 512U;
constexpr float tone_frequency = 997.0F;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

auto make_sine(const std::size_t frames, const float amplitude) -> std::vector<float> {
  std::vector<float> signal(frames * 2U);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float phase = 6.28318530718F * tone_frequency * static_cast<float>(frame) / sample_rate;
    const float sample = amplitude * std::sin(phase);
    signal[frame * 2U] = sample;
    signal[frame * 2U + 1U] = sample;
  }
  return signal;
}

/** Generates one continuous stereo tone and feeds consecutive slices to the processor. */
auto process_blocks(AutogainProcessor& processor, const float amplitude, const double seconds) -> std::vector<float> {
  const auto total_frames = static_cast<std::size_t>(seconds * static_cast<double>(sample_rate));
  const auto signal = make_sine(total_frames, amplitude);
  std::vector<float> block;
  for (std::size_t offset = 0; offset < total_frames; offset += block_frames) {
    const std::size_t frames = std::min(block_frames, total_frames - offset);
    block.assign(signal.begin() + static_cast<std::ptrdiff_t>(offset * 2U),
                 signal.begin() + static_cast<std::ptrdiff_t>((offset + frames) * 2U));
    processor.process(block.data(), frames);
  }
  return block;
}

auto block_peak(const std::vector<float>& block) -> float {
  float peak = 0.0F;
  for (const float sample : block) peak = std::max(peak, std::abs(sample));
  return peak;
}

/** Feeds an interleaved stereo signal to a fresh libebur128 state, the same library and modes Autogain uses. */
auto reference_energies(const std::vector<float>& signal, double* momentary, double* shortterm, double* integrated) {
  auto* state = ebur128_init(2U, static_cast<unsigned long>(sample_rate),
                             EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_SAMPLE_PEAK);
  if (state == nullptr) return false;
  ebur128_set_channel(state, 0U, EBUR128_LEFT);
  ebur128_set_channel(state, 1U, EBUR128_RIGHT);
  ebur128_set_max_history(state, 15000UL);
  ebur128_add_frames_float(state, signal.data(), signal.size() / 2U);
  const bool ok = EBUR128_SUCCESS == ebur128_loudness_momentary(state, momentary) &&
                  EBUR128_SUCCESS == ebur128_loudness_shortterm(state, shortterm) &&
                  EBUR128_SUCCESS == ebur128_loudness_global(state, integrated);
  ebur128_destroy(&state);
  return ok;
}

void test_defaults_and_clamps() {
  AutogainProcessor processor;
  const auto defaults = processor.parameters();
  expect(defaults.reference == AutogainReference::geometric_msi, "default reference must be Geometric Mean (MSI)");
  expect(std::abs(defaults.target_db - -23.0F) < 1.0e-6F, "default target must be -23 dB");
  expect(std::abs(defaults.silence_threshold_db - -70.0F) < 1.0e-6F, "default silence must be -70 dB");
  expect(defaults.maximum_history_seconds == 15, "default maximum history must be 15 s");
  expect(!defaults.force_silence && !defaults.bypass, "force silence and bypass default off");
  expect(defaults.input_gain_db == 0.0F && defaults.output_gain_db == 0.0F, "common gains default to 0 dB");

  AutogainParameters values;
  values.target_db = 5.0F;
  values.silence_threshold_db = 5.0F;
  values.maximum_history_seconds = 1;
  values.input_gain_db = 500.0F;
  values.output_gain_db = -500.0F;
  values.reference = static_cast<AutogainReference>(99);
  processor.set_parameters(values);
  const auto clamped = processor.parameters();
  expect(clamped.target_db == 0.0F, "target must clamp to the desktop maximum 0 dB");
  expect(clamped.silence_threshold_db == 0.0F, "silence threshold must clamp to the desktop maximum 0 dB");
  expect(clamped.maximum_history_seconds == 6, "maximum history must clamp to the desktop minimum 6 s");
  expect(clamped.input_gain_db == 36.0F && clamped.output_gain_db == -36.0F, "gains must clamp to +-36 dB");
  expect(clamped.reference == AutogainReference::geometric_si, "reference must clamp to a valid index");
}

void test_invalid_sample_rate() {
  AutogainProcessor processor;
  std::vector<float> block(block_frames * 2U, 0.0F);
  processor.process(block.data(), block_frames);  // before prepare: no-op
  expect(!processor.prepare(0.0F), "zero sample rate must be rejected");
  expect(!processor.prepare(-48000.0F), "negative sample rate must be rejected");
  expect(!processor.prepare(std::nan("")), "NaN sample rate must be rejected");
  expect(processor.prepare(sample_rate), "valid sample rate must initialize the measurement");
}

void test_loudness_matches_libebur128() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  constexpr double seconds = 5.0;
  constexpr float amplitude = 0.1F;
  process_blocks(processor, amplitude, seconds);

  double momentary = 0.0;
  double shortterm = 0.0;
  double integrated = 0.0;
  expect(reference_energies(make_sine(static_cast<std::size_t>(seconds * static_cast<double>(sample_rate)), amplitude),
                           &momentary, &shortterm, &integrated),
         "libebur128 reference must compute loudness");

  const auto levels = processor.levels();
  expect(std::abs(levels.momentary_lufs - momentary) < 1.0e-3, "momentary loudness must match the desktop library");
  expect(std::abs(levels.shortterm_lufs - shortterm) < 1.0e-3, "short-term loudness must match the desktop library");
  expect(std::abs(levels.integrated_lufs - integrated) < 1.0e-3, "integrated loudness must match the desktop library");
}

void test_gain_reaches_target() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  AutogainParameters parameters;
  parameters.reference = AutogainReference::integrated;
  parameters.target_db = -10.0F;
  processor.set_parameters(parameters);

  constexpr double seconds = 8.0;
  constexpr float amplitude = 0.1F;
  process_blocks(processor, amplitude, seconds);

  double momentary = 0.0;
  double shortterm = 0.0;
  double integrated = 0.0;
  reference_energies(make_sine(static_cast<std::size_t>(seconds * static_cast<double>(sample_rate)), amplitude),
                     &momentary, &shortterm, &integrated);

  const double expected_gain_db = static_cast<double>(parameters.target_db) - integrated;
  const auto levels = processor.levels();
  expect(std::abs(levels.output_gain_db - expected_gain_db) < 0.3,
         "internal gain must follow the desktop target minus measured loudness");
  expect(levels.output_left_db <= 0.01F && levels.output_left_db > -20.0F,
         "peak ceiling must keep the boosted output below full scale");
}

void test_peak_ceiling() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  AutogainParameters parameters;
  parameters.target_db = 0.0F;
  processor.set_parameters(parameters);

  constexpr float amplitude = 0.5F;
  const auto last = process_blocks(processor, amplitude, 8.0);

  const float peak = block_peak(last);
  const auto levels = processor.levels();
  expect(peak <= 1.0005F, "the 1 / peak ceiling must keep the output from clipping");
  expect(levels.output_gain_db <= 6.05F, "gain must not exceed the ceiling imposed by the input peak");
}

void test_silence_force_silence() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  AutogainParameters parameters;
  parameters.target_db = -23.0F;
  parameters.silence_threshold_db = -70.0F;
  parameters.force_silence = true;
  processor.set_parameters(parameters);

  constexpr float amplitude = 0.00002F;
  const auto last = process_blocks(processor, amplitude, 5.0);
  const float peak = block_peak(last);
  expect(peak < amplitude * 1.0e-3F, "force silence must attenuate sub-threshold content to the desktop floor");

  parameters.force_silence = false;
  processor.set_parameters(parameters);
  processor.reset_history();
  const auto passthrough = process_blocks(processor, amplitude, 1.0);
  const float passthrough_peak = block_peak(passthrough);
  expect(passthrough_peak > amplitude * 0.5F, "without force silence sub-threshold content is not muted");
}

void test_bypass_preserves_samples() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  processor.set_input_gain_db(6.0F);
  processor.set_output_gain_db(-9.0F);
  processor.set_bypass(true);
  std::vector<float> samples{0.1F, -0.2F, 0.3F, -0.4F, 0.5F, -0.6F, 0.7F, -0.8F};
  const auto original = samples;
  processor.process(samples.data(), 4U);
  expect(samples == original, "bypass must preserve samples exactly, including common gains");
}

void test_reset_history_and_reset() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  AutogainParameters parameters;
  parameters.target_db = -10.0F;
  processor.set_parameters(parameters);
  process_blocks(processor, 0.1F, 8.0);
  expect(std::abs(processor.levels().output_gain_db) > 1.0F, "gain must have moved away from unity");

  processor.reset_history();
  std::vector<float> silence(block_frames * 2U, 0.0F);
  processor.process(silence.data(), block_frames);
  expect(std::abs(processor.levels().output_gain_db) < 1.0e-6F,
         "reset history must restart the measurement from unity before it re-converges");

  processor.set_target_db(-5.0F);
  processor.set_maximum_history_seconds(600);
  processor.set_force_silence(true);
  processor.set_input_gain_db(12.0F);
  processor.reset();
  const auto defaults = processor.parameters();
  expect(defaults.target_db == -23.0F && defaults.maximum_history_seconds == 15 && !defaults.force_silence &&
             defaults.input_gain_db == 0.0F,
         "reset must restore the desktop defaults");
}

void test_chunk_boundaries_are_stable() {
  AutogainProcessor processor;
  expect(processor.prepare(sample_rate), "processor must prepare");
  AutogainParameters parameters;
  parameters.target_db = -12.0F;
  processor.set_parameters(parameters);
  process_blocks(processor, 0.1F, 8.0);
  const float settled = processor.levels().output_gain_db;
  process_blocks(processor, 0.1F, 1.0);
  const float after = processor.levels().output_gain_db;
  expect(std::isfinite(settled) && std::isfinite(after), "gain must remain finite across callback chunks");
  expect(std::abs(settled - after) < 0.1F, "gain must stay stable once it has converged");
}
}  // namespace

int main() {
  test_defaults_and_clamps();
  test_invalid_sample_rate();
  test_loudness_matches_libebur128();
  test_gain_reaches_target();
  test_peak_ceiling();
  test_silence_force_silence();
  test_bypass_preserves_samples();
  test_reset_history_and_reset();
  test_chunk_boundaries_are_stable();
  if (failures != 0) {
    std::cerr << failures << " C++ Autogain test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ Autogain tests passed\n";
  return EXIT_SUCCESS;
}
