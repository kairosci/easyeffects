/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ee::mobile {

enum class AutogainReference : std::uint8_t {
  momentary = 0,
  shortterm = 1,
  integrated = 2,
  geometric_msi = 3,
  geometric_ms = 4,
  geometric_mi = 5,
  geometric_si = 6,
};

struct AutogainParameters {
  AutogainReference reference = AutogainReference::geometric_msi;
  float target_db = -23.0F;
  float silence_threshold_db = -70.0F;
  int maximum_history_seconds = 15;
  bool force_silence = false;
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  bool bypass = false;
};

struct AutogainLevels {
  float momentary_lufs = -100.0F;
  float shortterm_lufs = -100.0F;
  float integrated_lufs = -100.0F;
  float relative_lufs = -100.0F;
  float range_lu = 0.0F;
  float loudness_lufs = -100.0F;
  float output_gain_db = 0.0F;
  float input_left_db = -100.0F;
  float input_right_db = -100.0F;
  float output_left_db = -100.0F;
  float output_right_db = -100.0F;
};

/**
 * Portable stereo port of the desktop EasyEffects Autogain plugin. It uses the
 * same libebur128 1.2.6 measurement modes and applies the desktop reference
 * selection, target correction, peak ceiling, attack/release smoothing, silence
 * handling, common gains and bypass.
 */
class AutogainProcessor final {
 public:
  AutogainProcessor() = default;
  ~AutogainProcessor();
  AutogainProcessor(const AutogainProcessor&) = delete;
  auto operator=(const AutogainProcessor&) -> AutogainProcessor& = delete;
  AutogainProcessor(AutogainProcessor&&) = delete;
  auto operator=(AutogainProcessor&&) -> AutogainProcessor& = delete;

  /** Allocates the loudness measurement state. Returns false for invalid rates. */
  [[nodiscard]] auto prepare(float sample_rate) noexcept -> bool;
  [[nodiscard]] auto sample_rate() const noexcept -> float { return sample_rate_; }

  void set_parameters(const AutogainParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> AutogainParameters;

  void set_reference(AutogainReference reference) noexcept;
  void set_target_db(float target_db) noexcept;
  void set_silence_threshold_db(float silence_threshold_db) noexcept;
  void set_maximum_history_seconds(int seconds) noexcept;
  void set_force_silence(bool force_silence) noexcept;
  void set_input_gain_db(float gain_db) noexcept;
  void set_output_gain_db(float gain_db) noexcept;
  void set_bypass(bool bypass) noexcept;

  /** Clears the loudness measurement and returns the internal gain to unity. */
  void reset_history() noexcept;
  /** Restores the desktop defaults and then clears the measurement history. */
  void reset() noexcept;

  void process(float* interleaved_stereo, std::size_t frames) noexcept;

  [[nodiscard]] auto levels() const noexcept -> AutogainLevels;

 private:
  auto reinit_measurement() noexcept -> bool;
  void apply_maximum_history() noexcept;
  static auto clamp_gain(float gain_db) noexcept -> float;
  static auto clamp_db(float value_db) noexcept -> float;
  static auto linear_gain(float gain_db) noexcept -> float;
  static auto level_db(float peak) noexcept -> float;

  void* state_ = nullptr;  // ebur128_state*, kept opaque to avoid a C include.

  std::atomic<float> sample_rate_{0.0F};

  std::atomic<int> reference_{static_cast<int>(AutogainReference::geometric_msi)};
  std::atomic<float> target_db_{-23.0F};
  std::atomic<float> silence_threshold_db_{-70.0F};
  std::atomic<int> maximum_history_seconds_{15};
  std::atomic<bool> force_silence_{false};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
  std::atomic<float> input_gain_linear_{1.0F};
  std::atomic<float> output_gain_linear_{1.0F};
  std::atomic<bool> bypass_{false};

  std::atomic<bool> reset_requested_{false};

  std::array<std::atomic<float>, 7> loudness_values_{};  // momentary..range
  std::atomic<float> loudness_lufs_{-100.0F};
  std::atomic<float> output_gain_level_db_{0.0F};
  std::array<std::atomic<float>, 4> peak_db_{{-100.0F, -100.0F, -100.0F, -100.0F}};

  // Audio-thread only state.
  double internal_output_gain_ = 1.0;
  double previous_output_gain_ = 1.0;
  int applied_history_seconds_ = -1;
  float sample_rate_value_ = 0.0F;
};

}  // namespace ee::mobile
