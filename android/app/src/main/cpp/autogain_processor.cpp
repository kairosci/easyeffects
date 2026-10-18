/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "autogain_processor.hpp"

#include <ebur128.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ee::mobile {
namespace {
constexpr int minimum_history_seconds = 6;
constexpr int maximum_history_seconds = 3600;
constexpr float minimum_gain_db = -36.0F;
constexpr float maximum_gain_db = 36.0F;
constexpr float minimum_db_level = -100.0F;
constexpr double minimum_linear_d_level = 0.00001;
constexpr double attack_time = 0.1;   // seconds, desktop Autogain hard-coded value
constexpr double release_time = 0.4;  // seconds, desktop Autogain hard-coded value
constexpr int measurement_mode =
    EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_SAMPLE_PEAK;

auto ebur(void* state) noexcept -> ebur128_state* { return static_cast<ebur128_state*>(state); }
}  // namespace

AutogainProcessor::~AutogainProcessor() {
  if (state_ != nullptr) {
    auto* state = ebur(state_);
    ebur128_destroy(&state);
    state_ = nullptr;
  }
}

auto AutogainProcessor::clamp_gain(const float gain_db) noexcept -> float {
  if (!std::isfinite(gain_db)) return 0.0F;
  return std::clamp(gain_db, minimum_gain_db, maximum_gain_db);
}

auto AutogainProcessor::clamp_db(const float value_db) noexcept -> float {
  if (!std::isfinite(value_db)) return minimum_db_level;
  return std::clamp(value_db, minimum_db_level, 0.0F);
}

auto AutogainProcessor::linear_gain(const float gain_db) noexcept -> float {
  return std::exp((clamp_gain(gain_db) / 20.0F) * std::numbers::ln10_v<float>);
}

auto AutogainProcessor::level_db(const float peak) noexcept -> float {
  return peak >= static_cast<float>(minimum_linear_d_level) ? 20.0F * std::log10(peak) : minimum_db_level;
}

auto AutogainProcessor::reinit_measurement() noexcept -> bool {
  if (state_ != nullptr) {
    auto* state = ebur(state_);
    ebur128_destroy(&state);
    state_ = nullptr;
  }
  const float rate = sample_rate_value_;
  if (!(rate > 0.0F) || !std::isfinite(rate)) return false;
  auto* const state = ebur128_init(2U, static_cast<unsigned long>(std::lround(rate)), measurement_mode);
  if (state == nullptr) return false;
  ebur128_set_channel(state, 0U, EBUR128_LEFT);
  ebur128_set_channel(state, 1U, EBUR128_RIGHT);
  state_ = state;
  applied_history_seconds_ = -1;
  return true;
}

auto AutogainProcessor::prepare(const float sample_rate) noexcept -> bool {
  if (!std::isfinite(sample_rate) || sample_rate < 1.0F || sample_rate > 768000.0F) {
    return false;
  }
  sample_rate_value_ = sample_rate;
  sample_rate_.store(sample_rate, std::memory_order_relaxed);
  internal_output_gain_ = 1.0;
  previous_output_gain_ = 1.0;
  return reinit_measurement();
}

void AutogainProcessor::apply_maximum_history() noexcept {
  if (state_ == nullptr) return;
  const int seconds =
      std::clamp(maximum_history_seconds_.load(std::memory_order_relaxed), minimum_history_seconds,
                 maximum_history_seconds);
  if (seconds == applied_history_seconds_) return;
  ebur128_set_max_history(ebur(state_), static_cast<unsigned long>(seconds) * 1000UL);
  applied_history_seconds_ = seconds;
}

void AutogainProcessor::set_parameters(const AutogainParameters& parameters) noexcept {
  set_reference(parameters.reference);
  set_target_db(parameters.target_db);
  set_silence_threshold_db(parameters.silence_threshold_db);
  set_maximum_history_seconds(parameters.maximum_history_seconds);
  set_force_silence(parameters.force_silence);
  set_input_gain_db(parameters.input_gain_db);
  set_output_gain_db(parameters.output_gain_db);
  set_bypass(parameters.bypass);
}

auto AutogainProcessor::parameters() const noexcept -> AutogainParameters {
  return {
      .reference = static_cast<AutogainReference>(reference_.load(std::memory_order_relaxed)),
      .target_db = target_db_.load(std::memory_order_relaxed),
      .silence_threshold_db = silence_threshold_db_.load(std::memory_order_relaxed),
      .maximum_history_seconds = maximum_history_seconds_.load(std::memory_order_relaxed),
      .force_silence = force_silence_.load(std::memory_order_relaxed),
      .input_gain_db = input_gain_db_.load(std::memory_order_relaxed),
      .output_gain_db = output_gain_db_.load(std::memory_order_relaxed),
      .bypass = bypass_.load(std::memory_order_relaxed),
  };
}

void AutogainProcessor::set_reference(const AutogainReference reference) noexcept {
  reference_.store(std::clamp(static_cast<int>(reference), 0, 6), std::memory_order_relaxed);
}

void AutogainProcessor::set_target_db(const float target_db) noexcept {
  target_db_.store(clamp_db(target_db), std::memory_order_relaxed);
}

void AutogainProcessor::set_silence_threshold_db(const float silence_threshold_db) noexcept {
  silence_threshold_db_.store(clamp_db(silence_threshold_db), std::memory_order_relaxed);
}

void AutogainProcessor::set_maximum_history_seconds(const int seconds) noexcept {
  maximum_history_seconds_.store(std::clamp(seconds, minimum_history_seconds, maximum_history_seconds),
                                 std::memory_order_relaxed);
}

void AutogainProcessor::set_force_silence(const bool force_silence) noexcept {
  force_silence_.store(force_silence, std::memory_order_relaxed);
}

void AutogainProcessor::set_input_gain_db(const float gain_db) noexcept {
  const float clamped = clamp_gain(gain_db);
  input_gain_db_.store(clamped, std::memory_order_relaxed);
  input_gain_linear_.store(linear_gain(clamped), std::memory_order_relaxed);
}

void AutogainProcessor::set_output_gain_db(const float gain_db) noexcept {
  const float clamped = clamp_gain(gain_db);
  output_gain_db_.store(clamped, std::memory_order_relaxed);
  output_gain_linear_.store(linear_gain(clamped), std::memory_order_relaxed);
}

void AutogainProcessor::set_bypass(const bool bypass) noexcept { bypass_.store(bypass, std::memory_order_relaxed); }

void AutogainProcessor::reset_history() noexcept {
  reset_requested_.store(true, std::memory_order_relaxed);
  for (auto& value : loudness_values_) value.store(minimum_db_level, std::memory_order_relaxed);
  loudness_lufs_.store(minimum_db_level, std::memory_order_relaxed);
  output_gain_level_db_.store(0.0F, std::memory_order_relaxed);
  for (auto& peak : peak_db_) peak.store(minimum_db_level, std::memory_order_relaxed);
}

void AutogainProcessor::reset() noexcept {
  set_parameters(AutogainParameters{});
  reset_history();
}

void AutogainProcessor::process(float* const interleaved_stereo, const std::size_t frames) noexcept {
  if (state_ == nullptr || interleaved_stereo == nullptr || frames == 0U) return;
  if (!(sample_rate_value_ > 0.0F) || !std::isfinite(sample_rate_value_)) return;

  if (reset_requested_.exchange(false, std::memory_order_relaxed)) {
    internal_output_gain_ = 1.0;
    previous_output_gain_ = 1.0;
    reinit_measurement();
    if (state_ == nullptr) return;
  }

  if (bypass_.load(std::memory_order_relaxed)) return;

  const float input_gain = input_gain_linear_.load(std::memory_order_relaxed);
  if (input_gain != 1.0F) {
    for (std::size_t i = 0; i < frames * 2U; ++i) interleaved_stereo[i] *= input_gain;
  }

  apply_maximum_history();

  auto* const state = ebur(state_);
  ebur128_add_frames_float(state, interleaved_stereo, frames);

  double momentary = 0.0;
  double shortterm = 0.0;
  double integrated = 0.0;
  double relative = 0.0;
  double range = 0.0;
  bool failed = (EBUR128_SUCCESS != ebur128_loudness_momentary(state, &momentary)) ||
                (EBUR128_SUCCESS != ebur128_loudness_shortterm(state, &shortterm)) ||
                (EBUR128_SUCCESS != ebur128_loudness_global(state, &integrated)) ||
                (EBUR128_SUCCESS != ebur128_relative_threshold(state, &relative)) ||
                (EBUR128_SUCCESS != ebur128_loudness_range(state, &range));

  if (std::isinf(momentary) || std::isnan(momentary)) momentary = 0.0;
  if (shortterm > 10.0 || std::isinf(shortterm) || std::isnan(shortterm)) shortterm = momentary;
  if (integrated > 10.0 || std::isinf(integrated) || std::isnan(integrated)) integrated = momentary;

  loudness_values_[0].store(static_cast<float>(momentary), std::memory_order_relaxed);
  loudness_values_[1].store(static_cast<float>(shortterm), std::memory_order_relaxed);
  loudness_values_[2].store(static_cast<float>(integrated), std::memory_order_relaxed);
  loudness_values_[3].store(static_cast<float>(relative), std::memory_order_relaxed);
  loudness_values_[4].store(static_cast<float>(range), std::memory_order_relaxed);

  double loudness = momentary;
  const float silence_threshold = silence_threshold_db_.load(std::memory_order_relaxed);
  if (momentary > static_cast<double>(silence_threshold) && !failed) {
    double peak_left = 0.0;
    double peak_right = 0.0;
    if (EBUR128_SUCCESS != ebur128_prev_sample_peak(state, 0U, &peak_left) ||
        EBUR128_SUCCESS != ebur128_prev_sample_peak(state, 1U, &peak_right)) {
      failed = true;
    }

    if (!failed) {
      switch (static_cast<AutogainReference>(reference_.load(std::memory_order_relaxed))) {
        case AutogainReference::momentary:
          loudness = momentary;
          break;
        case AutogainReference::shortterm:
          loudness = shortterm;
          break;
        case AutogainReference::integrated:
          loudness = integrated;
          break;
        case AutogainReference::geometric_msi:
          loudness = std::cbrt(momentary * shortterm * integrated);
          break;
        case AutogainReference::geometric_ms:
          loudness = std::sqrt(std::fabs(momentary * shortterm));
          if (momentary < 0.0 && shortterm < 0.0) loudness *= -1.0;
          break;
        case AutogainReference::geometric_mi:
          loudness = std::sqrt(std::fabs(momentary * integrated));
          if (momentary < 0.0 && integrated < 0.0) loudness *= -1.0;
          break;
        case AutogainReference::geometric_si:
          loudness = std::sqrt(std::fabs(shortterm * integrated));
          if (shortterm < 0.0 && integrated < 0.0) loudness *= -1.0;
          break;
      }

      const double diff = static_cast<double>(target_db_.load(std::memory_order_relaxed)) - loudness;
      const double target_request = std::exp((diff / 20.0) * std::numbers::ln10);
      const double peak = std::max(peak_left, peak_right);
      const double db_peak = peak >= minimum_linear_d_level ? 20.0 * std::log10(peak)
                                                            : static_cast<double>(minimum_db_level);
      if (db_peak > static_cast<double>(minimum_db_level)) {
        const double max_gain = 1.0 / peak;  // desktop safe ceiling
        const double target_gain = std::min(target_request, max_gain);
        const double block_time = static_cast<double>(frames) / static_cast<double>(sample_rate_value_);
        const double attack_coeff = std::exp(-block_time / attack_time);
        const double release_coeff = std::exp(-block_time / release_time);
        const double alpha = (target_gain < previous_output_gain_) ? attack_coeff : release_coeff;
        internal_output_gain_ = (alpha * previous_output_gain_) + ((1.0 - alpha) * target_gain);
        previous_output_gain_ = internal_output_gain_;
      }
    }
  } else if (force_silence_.load(std::memory_order_relaxed)) {
    internal_output_gain_ = minimum_linear_d_level;
    previous_output_gain_ = internal_output_gain_;
  }
  loudness_lufs_.store(static_cast<float>(loudness), std::memory_order_relaxed);

  float input_peak_left = 0.0F;
  float input_peak_right = 0.0F;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    input_peak_left = std::max(input_peak_left, std::abs(interleaved_stereo[frame * 2U]));
    input_peak_right = std::max(input_peak_right, std::abs(interleaved_stereo[frame * 2U + 1U]));
  }

  const float final_gain =
      static_cast<float>(internal_output_gain_ * output_gain_linear_.load(std::memory_order_relaxed));
  if (final_gain != 1.0F) {
    for (std::size_t i = 0; i < frames * 2U; ++i) interleaved_stereo[i] *= final_gain;
  }
  output_gain_level_db_.store(level_db(static_cast<float>(internal_output_gain_)), std::memory_order_relaxed);

  float output_peak_left = 0.0F;
  float output_peak_right = 0.0F;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    output_peak_left = std::max(output_peak_left, std::abs(interleaved_stereo[frame * 2U]));
    output_peak_right = std::max(output_peak_right, std::abs(interleaved_stereo[frame * 2U + 1U]));
  }
  peak_db_[0].store(level_db(input_peak_left), std::memory_order_relaxed);
  peak_db_[1].store(level_db(input_peak_right), std::memory_order_relaxed);
  peak_db_[2].store(level_db(output_peak_left), std::memory_order_relaxed);
  peak_db_[3].store(level_db(output_peak_right), std::memory_order_relaxed);
}

auto AutogainProcessor::levels() const noexcept -> AutogainLevels {
  return {
      .momentary_lufs = loudness_values_[0].load(std::memory_order_relaxed),
      .shortterm_lufs = loudness_values_[1].load(std::memory_order_relaxed),
      .integrated_lufs = loudness_values_[2].load(std::memory_order_relaxed),
      .relative_lufs = loudness_values_[3].load(std::memory_order_relaxed),
      .range_lu = loudness_values_[4].load(std::memory_order_relaxed),
      .loudness_lufs = loudness_lufs_.load(std::memory_order_relaxed),
      .output_gain_db = output_gain_level_db_.load(std::memory_order_relaxed),
      .input_left_db = peak_db_[0].load(std::memory_order_relaxed),
      .input_right_db = peak_db_[1].load(std::memory_order_relaxed),
      .output_left_db = peak_db_[2].load(std::memory_order_relaxed),
      .output_right_db = peak_db_[3].load(std::memory_order_relaxed),
  };
}

}  // namespace ee::mobile
