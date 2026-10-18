/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Based on MDA Loudness by Paul Kellett and David Robillard (GPL-3.0-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace ee::mobile {

struct BassLoudnessParameters {
  float loudness_db = -3.1F;
  float output_db = -6.0F;
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  float link_db = -9.1F;
  bool bypass = false;
};

/** Portable stereo port of the MDA equal-loudness contour processor. */
class BassLoudnessProcessor final {
 public:
  BassLoudnessProcessor() = default;
  BassLoudnessProcessor(const BassLoudnessProcessor&) = delete;
  auto operator=(const BassLoudnessProcessor&) -> BassLoudnessProcessor& = delete;
  BassLoudnessProcessor(BassLoudnessProcessor&&) = delete;
  auto operator=(BassLoudnessProcessor&&) -> BassLoudnessProcessor& = delete;

  [[nodiscard]] auto prepare(float sample_rate) noexcept -> bool;
  void set_parameters(const BassLoudnessParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> BassLoudnessParameters;
  void reset() noexcept;
  void process(float* interleaved_stereo, std::size_t frames) noexcept;

 private:
  std::array<std::atomic<float>, 4> parameter_values_{{-3.1F, -6.0F, 0.0F, 0.0F}};
  std::atomic<float> link_db_{-9.1F};
  std::atomic<bool> bypass_{false};
  std::array<float, 4> state_{};
  float sample_rate_ = 0.0F;
};

}  // namespace ee::mobile
