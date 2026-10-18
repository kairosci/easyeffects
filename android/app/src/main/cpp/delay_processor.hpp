/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Adapted from LSP Compensation Delay semantics (LGPL-3.0-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ee::mobile {

enum class DelayMode : std::uint8_t { samples, distance, time };

struct DelayChannelParameters {
  DelayMode mode = DelayMode::time;
  float time_ms = 0.0F;
  float samples = 0.0F;
  float meters = 0.0F;
  float centimeters = 0.0F;
  float temperature_c = 20.0F;
  float dry_db = -80.01F;
  float wet_db = 0.0F;
  bool invert_phase = false;
};

struct DelayParameters {
  std::array<DelayChannelParameters, 2> channels{};
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  bool bypass = false;
};

struct DelayLevels {
  float input_left_db = -100.0F;
  float input_right_db = -100.0F;
  float output_left_db = -100.0F;
  float output_right_db = -100.0F;
};

/** Stereo port of EasyEffects' LSP Compensation Delay controls and signal path. */
class DelayProcessor final {
 public:
  DelayProcessor();
  DelayProcessor(const DelayProcessor&) = delete;
  auto operator=(const DelayProcessor&) -> DelayProcessor& = delete;
  DelayProcessor(DelayProcessor&&) = delete;
  auto operator=(DelayProcessor&&) -> DelayProcessor& = delete;

  // Call while the audio stream is stopped, before process() or after a rate change.
  [[nodiscard]] auto prepare(float sample_rate) -> bool;
  void set_parameters(const DelayParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> DelayParameters;
  [[nodiscard]] auto levels() const noexcept -> DelayLevels;
  [[nodiscard]] auto effective_delay_samples(std::size_t channel) const noexcept -> std::uint32_t;
  void reset() noexcept;
  void process(float* interleaved_stereo, std::size_t frames) noexcept;

 private:
  static constexpr std::size_t channel_count = 2U;
  static auto clamp_channel(DelayChannelParameters parameters) noexcept -> DelayChannelParameters;
  static auto delay_samples(const DelayChannelParameters& parameters, float sample_rate) noexcept -> std::uint32_t;
  static auto gain_from_db(float gain_db, bool silence_at_minimum) noexcept -> float;

  std::array<std::atomic<DelayMode>, channel_count> mode_{};
  std::array<std::atomic<float>, channel_count> time_ms_{};
  std::array<std::atomic<float>, channel_count> samples_{};
  std::array<std::atomic<float>, channel_count> meters_{};
  std::array<std::atomic<float>, channel_count> centimeters_{};
  std::array<std::atomic<float>, channel_count> temperature_c_{};
  std::array<std::atomic<float>, channel_count> dry_db_{};
  std::array<std::atomic<float>, channel_count> wet_db_{};
  std::array<std::atomic<bool>, channel_count> invert_phase_{};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
  std::atomic<bool> bypass_{false};
  std::atomic<bool> reset_requested_{false};
  std::array<std::atomic<float>, 4> levels_db_{{-100.0F, -100.0F, -100.0F, -100.0F}};

  std::array<std::vector<float>, channel_count> buffers_{};
  std::array<std::size_t, channel_count> write_indices_{};
  float sample_rate_ = 0.0F;
};

}  // namespace ee::mobile
