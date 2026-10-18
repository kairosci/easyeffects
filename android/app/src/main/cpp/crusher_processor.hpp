/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Adapted from Calf Studio Gear Crusher DSP by Markus Schmidt and Christian Holschuh.
 * Copyright © 2001-2010 Calf contributors. LGPL-2.1-only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ee::mobile {

enum class CrusherMode : std::uint8_t { linear, logarithmic };

struct CrusherParameters {
  CrusherMode mode = CrusherMode::linear;
  float bit_reduction = 4.0F;
  float morph = 0.5F;
  float dc_db = 0.0F;
  float anti_aliasing = 0.5F;
  float sample_reduction = 1.0F;
  bool lfo_active = false;
  float lfo_range = 20.0F;
  float lfo_rate_hz = 0.3F;
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  bool bypass = false;
};

struct CrusherLevels {
  float input_left_db = -100.0F;
  float input_right_db = -100.0F;
  float output_left_db = -100.0F;
  float output_right_db = -100.0F;
};

/** Stereo Calf Crusher processor with block-safe settings and no process-time allocation. */
class CrusherProcessor final {
 public:
  CrusherProcessor() = default;
  CrusherProcessor(const CrusherProcessor&) = delete;
  auto operator=(const CrusherProcessor&) -> CrusherProcessor& = delete;
  CrusherProcessor(CrusherProcessor&&) = delete;
  auto operator=(CrusherProcessor&&) -> CrusherProcessor& = delete;

  void set_parameters(const CrusherParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> CrusherParameters;
  [[nodiscard]] auto levels() const noexcept -> CrusherLevels;
  void reset() noexcept;
  void process(float* interleaved_stereo, std::size_t frames, float sample_rate) noexcept;

 private:
  struct SampleReducer {
    float target = 0.0F;
    float real = 0.0F;
    float amount = 1.0F;
    unsigned int samples = 0U;
    unsigned int round = 1U;
    double last = 0.0;
    void set_amount(float value) noexcept;
    [[nodiscard]] auto process(double value) noexcept -> double;
  };

  [[nodiscard]] static auto clamp_parameters(CrusherParameters parameters) noexcept -> CrusherParameters;
  [[nodiscard]] static auto quantize(float input, const CrusherParameters& parameters) noexcept -> float;
  [[nodiscard]] static auto gain_from_db(float db) noexcept -> float;

  std::atomic<CrusherMode> mode_{CrusherMode::linear};
  std::atomic<float> bit_reduction_{4.0F};
  std::atomic<float> morph_{0.5F};
  std::atomic<float> dc_db_{0.0F};
  std::atomic<float> anti_aliasing_{0.5F};
  std::atomic<float> sample_reduction_{1.0F};
  std::atomic<bool> lfo_active_{false};
  std::atomic<float> lfo_range_{20.0F};
  std::atomic<float> lfo_rate_hz_{0.3F};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
  std::atomic<bool> bypass_{false};
  std::atomic<bool> reset_requested_{false};
  std::array<std::atomic<float>, 4> levels_db_{{-100.0F, -100.0F, -100.0F, -100.0F}};

  SampleReducer left_reducer_;
  SampleReducer right_reducer_;
  double lfo_phase_ = 0.0;
};

}  // namespace ee::mobile
