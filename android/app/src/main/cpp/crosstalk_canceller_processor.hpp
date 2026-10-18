/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Crosstalk Canceller algorithm by Antti S. Lankila <alankila@bel.fi>.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace ee::mobile {

struct CrosstalkParameters {
  float delay_us = 313.0F;
  float decay_db = -2.0F;
  bool phantom_center_only = false;
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  bool bypass = false;
};

struct CrosstalkLevels {
  float input_left_db = -100.0F;
  float input_right_db = -100.0F;
  float output_left_db = -100.0F;
  float output_right_db = -100.0F;
};

/** C++ implementation of EasyEffects' six-filter stereo crosstalk canceller. */
class CrosstalkCancellerProcessor final {
 public:
  CrosstalkCancellerProcessor() = default;
  CrosstalkCancellerProcessor(const CrosstalkCancellerProcessor&) = delete;
  auto operator=(const CrosstalkCancellerProcessor&) -> CrosstalkCancellerProcessor& = delete;
  CrosstalkCancellerProcessor(CrosstalkCancellerProcessor&&) = delete;
  auto operator=(CrosstalkCancellerProcessor&&) -> CrosstalkCancellerProcessor& = delete;

  // Call while the stream is stopped, before process() or after a sample-rate change.
  [[nodiscard]] auto prepare(float sample_rate) -> bool;
  void set_parameters(const CrosstalkParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> CrosstalkParameters;
  [[nodiscard]] auto levels() const noexcept -> CrosstalkLevels;
  void reset() noexcept;
  void process(float* interleaved_stereo, std::size_t frames) noexcept;

 private:
  class Biquad final {
   public:
    void set_high_pass(double frequency, double sample_rate, double quality) noexcept;
    void set_low_pass(double frequency, double sample_rate, double quality) noexcept;
    void set_peaking(double frequency, double sample_rate, double gain_db, double quality) noexcept;
    void clear() noexcept;
    [[nodiscard]] auto process(float input) noexcept -> float;

   private:
    void set_coefficients(double a0, double a1, double a2, double b0, double b1, double b2) noexcept;
    float a1_ = 0.0F;
    float a2_ = 0.0F;
    float b0_ = 1.0F;
    float b1_ = 0.0F;
    float b2_ = 0.0F;
    float x1_ = 0.0F;
    float x2_ = 0.0F;
    float y1_ = 0.0F;
    float y2_ = 0.0F;
  };

  struct FilteredDelay {
    std::array<Biquad, 6> filters{};
    std::vector<float> history;
    std::size_t write_index = 0U;
    void configure_filters(float sample_rate) noexcept;
    void clear() noexcept;
    [[nodiscard]] auto delayed(std::size_t delay) const noexcept -> float;
    void push(float value) noexcept;
  };

  static auto clamp_parameters(CrosstalkParameters parameters) noexcept -> CrosstalkParameters;
  static auto gain_from_db(float gain_db) noexcept -> float;
  std::atomic<float> delay_us_{313.0F};
  std::atomic<float> decay_db_{-2.0F};
  std::atomic<bool> phantom_center_only_{false};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
  std::atomic<bool> bypass_{false};
  std::atomic<bool> reset_requested_{false};
  std::array<std::atomic<float>, 4> levels_db_{{-100.0F, -100.0F, -100.0F, -100.0F}};
  std::array<FilteredDelay, 2> lines_{};
  float sample_rate_ = 0.0F;
  float applied_delay_us_ = 0.0F;
  std::size_t delay_samples_ = 0U;
};

}  // namespace ee::mobile
