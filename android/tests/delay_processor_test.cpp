/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app/src/main/cpp/delay_processor.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
using ee::mobile::DelayMode;
using ee::mobile::DelayParameters;
using ee::mobile::DelayProcessor;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_independent_channel_sample_delays() {
  DelayProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare at the audio stream rate");
  auto p = processor.parameters();
  p.channels[0].mode = DelayMode::samples;
  p.channels[0].samples = 2.0F;
  p.channels[1].mode = DelayMode::samples;
  p.channels[1].samples = 3.0F;
  processor.set_parameters(p);
  std::array<float, 12> signal{1.0F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F,
                               0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
  processor.process(signal.data(), signal.size() / 2U);
  expect(signal[0] == 0.0F && signal[1] == 0.0F && signal[2] == 0.0F,
         "delayed-only mix must be silent before each channel delay expires");
  expect(signal[4] == 1.0F && signal[5] == 0.0F,
         "left channel must receive the impulse after its independent two-sample delay");
  expect(signal[6] == 0.0F && signal[7] == 0.5F,
         "right channel must receive the impulse after its independent three-sample delay");
  expect(processor.effective_delay_samples(0U) == 2U && processor.effective_delay_samples(1U) == 3U,
         "effective-delay readback must reflect each channel setting");
}

void test_desktop_defaults_and_input_output_meters() {
  DelayProcessor processor;
  const auto defaults = processor.parameters();
  expect(defaults.channels[0].mode == DelayMode::time && defaults.channels[1].mode == DelayMode::time,
         "both channels must default to the desktop Time mode");
  expect(defaults.channels[0].dry_db == -80.01F && defaults.channels[0].wet_db == 0.0F &&
             defaults.channels[1].dry_db == -80.01F && defaults.channels[1].wet_db == 0.0F,
         "both channel dry/wet gains must match desktop defaults");
  expect(processor.prepare(48000.0F), "processor must prepare for desktop meter controls");
  std::array<float, 4> signal{0.5F, -0.25F, -0.75F, 1.0F};
  processor.process(signal.data(), 2U);
  const auto levels = processor.levels();
  expect(std::abs(levels.input_left_db - 20.0F * std::log10(0.75F)) < 0.001F &&
             std::abs(levels.input_right_db) < 0.001F,
         "input meters must report per-channel block peak levels in dBFS");
  expect(std::abs(levels.output_left_db - levels.input_left_db) < 0.001F &&
             std::abs(levels.output_right_db - levels.input_right_db) < 0.001F,
         "default zero-delay output meters must match the input levels");
}

void test_time_and_distance_modes_match_lsp_units() {
  DelayProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare for time and distance conversion");
  auto p = processor.parameters();
  p.channels[0].mode = DelayMode::time;
  p.channels[0].time_ms = 1.25F;
  p.channels[1].mode = DelayMode::distance;
  p.channels[1].meters = 1.0F;
  p.channels[1].centimeters = 50.0F;
  p.channels[1].temperature_c = 20.0F;
  processor.set_parameters(p);
  expect(processor.effective_delay_samples(0U) == 60U, "time mode must convert milliseconds to sample frames");
  const float sound_speed = std::sqrt(1.4F * 8.3144598F * (20.0F + 273.15F) * 1000.0F / 28.98F);
  const auto expected_distance_samples = static_cast<std::uint32_t>(48000.0F * 1.5F / sound_speed);
  expect(processor.effective_delay_samples(1U) == expected_distance_samples,
         "distance mode must use LSP sound-speed conversion with temperature and centimeters");
  p.channels[1].temperature_c = -60.0F;
  processor.set_parameters(p);
  expect(processor.effective_delay_samples(1U) > expected_distance_samples,
         "colder air must increase the distance-derived delay as in the desktop plugin");
}

void test_dry_wet_phase_and_global_gain_controls() {
  DelayProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare for gain and phase test");
  auto p = processor.parameters();
  p.channels[0].mode = DelayMode::samples;
  p.channels[0].samples = 0.0F;
  p.channels[0].dry_db = 0.0F;
  p.channels[0].wet_db = -80.01F;
  p.channels[0].invert_phase = true;
  p.channels[1] = p.channels[0];
  p.channels[1].invert_phase = false;
  p.input_gain_db = 6.0F;
  p.output_gain_db = -3.0F;
  processor.set_parameters(p);
  std::array<float, 4> signal{0.25F, -0.25F, 0.5F, -0.5F};
  processor.process(signal.data(), 2U);
  constexpr float net_gain = 1.41253754F;
  expect(std::abs(signal[0] + 0.25F * net_gain) < 0.0001F &&
             std::abs(signal[1] + 0.25F * net_gain) < 0.0001F,
         "dry gain, -inf wet, phase inversion and common gains must compose correctly");
  expect(std::abs(signal[2] + 0.5F * net_gain) < 0.0001F &&
             std::abs(signal[3] + 0.5F * net_gain) < 0.0001F,
         "phase inversion must be independent per channel");
}

void test_bypass_passes_audio_while_preserving_delay_state() {
  DelayProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare for bypass test");
  auto p = processor.parameters();
  p.channels[0].mode = DelayMode::samples;
  p.channels[0].samples = 1.0F;
  p.channels[1] = p.channels[0];
  p.bypass = true;
  processor.set_parameters(p);
  std::array<float, 2> bypassed{0.75F, -0.5F};
  processor.process(bypassed.data(), 1U);
  expect(bypassed[0] == 0.75F && bypassed[1] == -0.5F, "bypass must pass the original stereo samples bit-exactly");
  p.bypass = false;
  processor.set_parameters(p);
  std::array<float, 2> next{0.0F, 0.0F};
  processor.process(next.data(), 1U);
  expect(next[0] == 0.75F && next[1] == -0.5F,
         "delay history must continue updating while the desktop bypass is enabled");
}

void test_parameter_limits_and_chunk_continuity() {
  DelayProcessor processor;
  DelayProcessor chunked;
  expect(processor.prepare(48000.0F) && chunked.prepare(48000.0F), "both processors must prepare");
  DelayParameters p;
  p.channels[0].mode = DelayMode::samples;
  p.channels[0].samples = 20000.0F;
  p.channels[0].meters = 500.0F;
  p.channels[0].centimeters = 300.0F;
  p.channels[0].temperature_c = 100.0F;
  p.channels[0].dry_db = -100.0F;
  p.channels[0].wet_db = 30.0F;
  p.channels[1] = p.channels[0];
  p.channels[1].samples = 127.0F;
  p.channels[1].mode = DelayMode::samples;
  p.input_gain_db = 50.0F;
  p.output_gain_db = -50.0F;
  processor.set_parameters(p);
  chunked.set_parameters(p);
  const auto clamped = processor.parameters();
  expect(clamped.channels[0].samples == 10000.0F && clamped.channels[0].meters == 200.0F &&
             clamped.channels[0].centimeters == 100.0F && clamped.channels[0].temperature_c == 60.0F,
         "channel controls must clamp to desktop KConfig ranges");
  expect(clamped.channels[0].dry_db == -80.01F && clamped.channels[0].wet_db == 20.0F &&
             clamped.input_gain_db == 36.0F && clamped.output_gain_db == -36.0F,
         "plugin and common gain controls must clamp to desktop ranges");

  p.channels[0].samples = 127.0F;
  p.channels[1].samples = 127.0F;
  p.input_gain_db = 0.0F;
  p.output_gain_db = 0.0F;
  p.channels[0].dry_db = -80.01F;
  p.channels[1].dry_db = -80.01F;
  p.channels[0].wet_db = 0.0F;
  p.channels[1].wet_db = 0.0F;
  processor.set_parameters(p);
  chunked.set_parameters(p);
  std::array<float, 64> full{};
  for (std::size_t i = 0; i < full.size(); ++i) full[i] = static_cast<float>(i) / 64.0F;
  auto parts = full;
  processor.process(full.data(), full.size() / 2U);
  chunked.process(parts.data(), 8U);
  chunked.process(parts.data() + 16U, 8U);
  chunked.process(parts.data() + 32U, 16U);
  expect(full == parts, "delay-line history must be continuous across audio callback blocks");
}

void test_invalid_prepare_and_null_audio_are_safe() {
  DelayProcessor processor;
  expect(!processor.prepare(0.0F) && !processor.prepare(1000000.0F), "invalid stream rates must be rejected");
  std::array<float, 2> unchanged{0.2F, -0.3F};
  processor.process(nullptr, 8U);
  processor.process(unchanged.data(), 1U);
  expect(unchanged[0] == 0.2F && unchanged[1] == -0.3F, "unprepared and null-buffer calls must be safe no-ops");
}

void test_reset_restores_desktop_parameters_and_delay_state() {
  DelayProcessor processor;
  expect(processor.prepare(48000.0F), "processor must prepare for reset verification");
  auto p = processor.parameters();
  p.channels[0].mode = DelayMode::samples;
  p.channels[0].samples = 4.0F;
  p.channels[1] = p.channels[0];
  processor.set_parameters(p);
  std::array<float, 2> impulse{1.0F, 1.0F};
  processor.process(impulse.data(), 1U);
  processor.reset();
  const auto defaults = processor.parameters();
  expect(defaults.channels[0].mode == DelayMode::time && defaults.channels[0].samples == 0.0F &&
             defaults.channels[0].dry_db == -80.01F && defaults.channels[0].wet_db == 0.0F && !defaults.bypass,
         "reset must restore the complete desktop default state");
  std::array<float, 2> after_reset{0.25F, -0.25F};
  processor.process(after_reset.data(), 1U);
  expect(after_reset[0] == 0.25F && after_reset[1] == -0.25F,
         "reset must clear the previous delay line before applying default zero-delay settings");
}
}  // namespace

int main() {
  test_desktop_defaults_and_input_output_meters();
  test_independent_channel_sample_delays();
  test_time_and_distance_modes_match_lsp_units();
  test_dry_wet_phase_and_global_gain_controls();
  test_bypass_passes_audio_while_preserving_delay_state();
  test_parameter_limits_and_chunk_continuity();
  test_invalid_prepare_and_null_audio_are_safe();
  test_reset_restores_desktop_parameters_and_delay_state();
  if (failures != 0) {
    std::cerr << failures << " C++ Delay test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All C++ Delay tests passed\n";
  return EXIT_SUCCESS;
}
