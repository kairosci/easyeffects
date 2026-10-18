/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "parametric_equalizer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace ee::mobile {

/** IIR Mid/Side processor. LSP FIR/FFT modes and advanced filter topologies are not represented. */
class MidSideEqualizerProcessor final {
 public:
  static constexpr std::size_t band_count = ParametricEqualizer::band_count;

  MidSideEqualizerProcessor() noexcept;
  void set_mid_band(std::size_t index, BandSettings settings) noexcept;
  void set_side_band(std::size_t index, BandSettings settings) noexcept;
  void set_split_channels(bool split) noexcept;
  void set_pitch_mid(float semitones) noexcept;
  void set_pitch_side(float semitones) noexcept;
  void set_bypass(bool bypass) noexcept;
  void set_balance(float percent) noexcept;
  void set_input_gain_db(float gain_db) noexcept;
  void set_output_gain_db(float gain_db) noexcept;
  void clear_audio_state() noexcept;
  void process(float* interleaved_stereo, std::size_t frames, float sample_rate) noexcept;
  [[nodiscard]] auto mid_response_db(float frequency_hz, float sample_rate) const noexcept -> float;
  [[nodiscard]] auto side_response_db(float frequency_hz, float sample_rate) const noexcept -> float;

 private:
  static constexpr std::size_t block_frames = 1024U;
  static auto shifted_band(BandSettings settings, float semitones) noexcept -> BandSettings;
  void apply_mid_band(std::size_t index) noexcept;
  void apply_side_band(std::size_t index) noexcept;

  ParametricEqualizer mid_eq_;
  ParametricEqualizer side_eq_;
  std::array<BandSettings, band_count> mid_bands_{};
  std::array<BandSettings, band_count> side_bands_{};
  mutable std::mutex settings_mutex_;
  std::atomic<bool> split_channels_{false};
  std::atomic<bool> bypass_{false};
  std::atomic<float> pitch_mid_{0.0F};
  std::atomic<float> pitch_side_{0.0F};
  std::atomic<float> balance_{0.0F};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
};

}  // namespace ee::mobile
