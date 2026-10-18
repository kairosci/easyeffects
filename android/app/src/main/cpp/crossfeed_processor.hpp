/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "bs2b.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ee::mobile {

enum class CrossfeedPreset : std::uint8_t { default_, cmoy, jmeier };

struct CrossfeedParameters {
  int cutoff_hz = 700;
  float feed_db = 4.5F;
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  bool bypass = false;
};

struct CrossfeedLevels {
  float input_left_db = -100.0F;
  float input_right_db = -100.0F;
  float output_left_db = -100.0F;
  float output_right_db = -100.0F;
};

/** C++ owner of the same libbs2b DSP used by the desktop Crossfeed plugin. */
class CrossfeedProcessor final {
 public:
  CrossfeedProcessor();
  ~CrossfeedProcessor();
  CrossfeedProcessor(const CrossfeedProcessor&) = delete;
  auto operator=(const CrossfeedProcessor&) -> CrossfeedProcessor& = delete;
  CrossfeedProcessor(CrossfeedProcessor&&) = delete;
  auto operator=(CrossfeedProcessor&&) -> CrossfeedProcessor& = delete;

  void set_cutoff_hz(int cutoff_hz) noexcept;
  void set_feed_db(float feed_db) noexcept;
  void set_input_gain_db(float gain_db) noexcept;
  void set_output_gain_db(float gain_db) noexcept;
  void set_bypass(bool bypass) noexcept;
  void apply_preset(CrossfeedPreset preset) noexcept;
  void reset() noexcept;
  void process(float* interleaved_stereo, std::size_t frames, float sample_rate) noexcept;

  [[nodiscard]] auto parameters() const noexcept -> CrossfeedParameters;
  [[nodiscard]] auto levels() const noexcept -> CrossfeedLevels;

 private:
  static auto pack_parameters(int cutoff_hz, int feed_tenths_db) noexcept -> std::uint32_t;
  static auto linear_gain(float gain_db) noexcept -> float;

  t_bs2bdp state_ = nullptr;
  std::atomic<std::uint32_t> packed_parameters_{pack_parameters(700, 45)};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
  std::atomic<bool> bypass_{false};
  std::atomic<bool> reset_requested_{false};
  std::atomic<float> input_gain_linear_{1.0F};
  std::atomic<float> output_gain_linear_{1.0F};
  std::array<std::atomic<float>, 4> levels_db_{{-100.0F, -100.0F, -100.0F, -100.0F}};
  std::uint32_t applied_parameters_ = 0U;
  std::uint32_t applied_sample_rate_ = 0U;
};

}  // namespace ee::mobile
