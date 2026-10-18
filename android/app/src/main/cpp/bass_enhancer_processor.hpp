/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Based on Calf Bass Enhancer by Markus Schmidt (LGPL-2.1-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace ee::mobile {

struct BassEnhancerParameters {
  float amount_db = 0.0F;
  float harmonics = 8.5F;
  float scope_hz = 100.0F;
  float floor_hz = 20.0F;
  float blend = 0.0F;
  bool floor_active = false;
  bool listen = false;
  float input_gain_db = 0.0F;
  float output_gain_db = 0.0F;
  bool bypass = false;
};

struct BassEnhancerLevels {
  float input_left_db = -100.0F;
  float input_right_db = -100.0F;
  float output_left_db = -100.0F;
  float output_right_db = -100.0F;
  float harmonics_level_db = -100.0F;
};

/**
 * Portable stereo port of the desktop EasyEffects Bass Enhancer plugin. It
 * reproduces Calf's Bass Enhancer DSP: a lowpass split, the TAP saturation stage
 * with its 2x oversampling filters, the optional highpass floor and the desktop
 * parameter ranges, common gains and bypass. No LV2 host, Linux or PipeWire code
 * is included.
 */
class BassEnhancerProcessor final {
 public:
  BassEnhancerProcessor() = default;
  ~BassEnhancerProcessor() = default;
  BassEnhancerProcessor(const BassEnhancerProcessor&) = delete;
  auto operator=(const BassEnhancerProcessor&) -> BassEnhancerProcessor& = delete;
  BassEnhancerProcessor(BassEnhancerProcessor&&) = delete;
  auto operator=(BassEnhancerProcessor&&) -> BassEnhancerProcessor& = delete;

  /** Prepares the filters for a sample rate. Returns false for invalid rates. */
  [[nodiscard]] auto prepare(float sample_rate) noexcept -> bool;
  [[nodiscard]] auto sample_rate() const noexcept -> float { return sample_rate_.load(std::memory_order_relaxed); }

  void set_parameters(const BassEnhancerParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> BassEnhancerParameters;
  /** Restores the desktop defaults and rebuilds the filter coefficients. */
  void reset() noexcept;

  void process(float* interleaved_stereo, std::size_t frames) noexcept;

  [[nodiscard]] auto levels() const noexcept -> BassEnhancerLevels;

 private:
  // Direct Form II biquad, matching Calf's biquad_d2 including the denormal guards.
  struct Biquad {
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double w1 = 0.0;
    double w2 = 0.0;

    void set_lowpass(float fc, float q, float sr) noexcept;
    void set_highpass(float fc, float q, float sr) noexcept;
    void copy_coeffs(const Biquad& src) noexcept;
    [[nodiscard]] auto process(double in) noexcept -> double;
    void sanitize() noexcept;
    void reset() noexcept;
  };

  // Calf/TAP blendable saturation with its 2x oversampling FIR/IIR filter pair.
  struct TapDistortion {
    Biquad up[2];
    Biquad down[2];
    double tmp[16] = {};
    unsigned srate = 0;
    int over = 2;
    float drive_old = -1.0F;
    float blend_old = -1.0F;
    float meter = 0.0F;
    float rdrive = 0.0F;
    float rbdr = 0.0F;
    float kpa = 0.0F;
    float kpb = 0.0F;
    float ap = 0.0F;
    float an = 0.0F;
    float imr = 0.0F;
    float kc = 0.0F;
    float srct = 0.0F;
    float sq = 0.0F;
    float pwrq = 0.0F;
    float kna = 0.0F;
    float knb = 0.0F;
    float prev_med = 0.0F;
    float prev_out = 0.0F;

    void set_sample_rate(unsigned sample_rate) noexcept;
    void set_params(float blend, float drive) noexcept;
    [[nodiscard]] auto process(float in) noexcept -> float;
    [[nodiscard]] auto upsample(double sample) noexcept -> double*;
    [[nodiscard]] auto downsample(double* samples) noexcept -> double;
    void reset() noexcept;
  };

  void params_changed() noexcept;
  void set_input_gain_db(float gain_db) noexcept;
  void set_output_gain_db(float gain_db) noexcept;

  std::atomic<float> sample_rate_{0.0F};
  std::atomic<float> amount_db_{0.0F};
  std::atomic<float> amount_linear_{1.0F};
  std::atomic<float> harmonics_{8.5F};
  std::atomic<float> scope_hz_{100.0F};
  std::atomic<float> floor_hz_{20.0F};
  std::atomic<float> blend_{0.0F};
  std::atomic<bool> floor_active_{false};
  std::atomic<bool> listen_{false};
  std::atomic<float> input_gain_db_{0.0F};
  std::atomic<float> output_gain_db_{0.0F};
  std::atomic<float> input_gain_linear_{1.0F};
  std::atomic<float> output_gain_linear_{1.0F};
  std::atomic<bool> bypass_{false};

  std::array<std::atomic<float>, 4> peak_db_{{-100.0F, -100.0F, -100.0F, -100.0F}};
  std::atomic<float> harmonics_level_db_{-100.0F};

  // Audio-thread only state.
  float sample_rate_value_ = 0.0F;
  Biquad lp_[2][4];
  Biquad hp_[2][2];
  TapDistortion dist_[2];
  float freq_old_ = 0.0F;
  float floor_old_ = 0.0F;
  bool floor_active_old_ = false;
  float meter_falloff_ = 1.0F;
  float harmonics_level_linear_ = 0.0F;
};

}  // namespace ee::mobile
