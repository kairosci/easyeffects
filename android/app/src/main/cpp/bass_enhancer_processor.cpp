/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Based on Calf Bass Enhancer by Markus Schmidt (LGPL-2.1-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bass_enhancer_processor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ee::mobile {
namespace {
constexpr float minimum_db_level = -100.0F;
constexpr float minimum_linear_level = 0.00001F;
constexpr float minimum_gain_db = -36.0F;
constexpr float maximum_gain_db = 36.0F;
constexpr float minimum_amount_db = -100.0F;
constexpr float maximum_amount_db = 36.0F;
constexpr float minimum_harmonics = 0.1F;
constexpr float maximum_harmonics = 10.0F;
constexpr float minimum_scope_hz = 10.0F;
constexpr float maximum_scope_hz = 250.0F;
constexpr float minimum_floor_hz = 10.0F;
constexpr float maximum_floor_hz = 120.0F;
constexpr float minimum_blend = -10.0F;
constexpr float maximum_blend = 10.0F;
constexpr double small_value = 1.0 / 16777216.0;  // Calf dsp::small_value<double>()
constexpr float tiny_value = 0.00000001F;         // Calf tap_distortion M()/D() threshold
constexpr int oversample_filters = 2;              // desktop resampleN uses two filters per direction

auto db_to_linear(const float db) noexcept -> float {
  return std::exp((db / 20.0F) * std::numbers::ln10_v<float>);
}

auto linear_to_db(const float amp) noexcept -> float {
  return amp >= minimum_linear_level ? 20.0F * std::log10(amp) : minimum_db_level;
}

auto clamp_gain(const float gain_db) noexcept -> float {
  if (!std::isfinite(gain_db)) return 0.0F;
  return std::clamp(gain_db, minimum_gain_db, maximum_gain_db);
}

// Calf tap_distortion helpers.
auto tap_m(const float x) noexcept -> float { return std::abs(x) > tiny_value ? x : 0.0F; }

auto tap_d(const float x) noexcept -> float {
  const float magnitude = std::abs(x);
  return magnitude > tiny_value ? std::sqrt(magnitude) : 0.0F;
}
}  // namespace

void BassEnhancerProcessor::Biquad::set_lowpass(const float fc, const float q, const float sr) noexcept {
  const double omega = 2.0 * std::numbers::pi * static_cast<double>(fc) / static_cast<double>(sr);
  const double sn = std::sin(omega);
  const double cs = std::cos(omega);
  const double alpha = sn / (2.0 * static_cast<double>(q));
  const double inv = 1.0 / (1.0 + alpha);
  a2 = a0 = inv * (1.0 - cs) * 0.5;
  a1 = a0 + a0;
  b1 = -2.0 * cs * inv;
  b2 = (1.0 - alpha) * inv;
}

void BassEnhancerProcessor::Biquad::set_highpass(const float fc, const float q, const float sr) noexcept {
  const double omega = 2.0 * std::numbers::pi * static_cast<double>(fc) / static_cast<double>(sr);
  const double sn = std::sin(omega);
  const double cs = std::cos(omega);
  const double alpha = sn / (2.0 * static_cast<double>(q));
  const double inv = 1.0 / (1.0 + alpha);
  a0 = inv * (1.0 + cs) * 0.5;
  a1 = -2.0 * a0;
  a2 = a0;
  b1 = -2.0 * cs * inv;
  b2 = (1.0 - alpha) * inv;
}

void BassEnhancerProcessor::Biquad::copy_coeffs(const Biquad& src) noexcept {
  a0 = src.a0;
  a1 = src.a1;
  a2 = src.a2;
  b1 = src.b1;
  b2 = src.b2;
}

auto BassEnhancerProcessor::Biquad::process(const double in) noexcept -> double {
  double n = in;
  if (!std::isnormal(n)) n = 0.0;
  if (std::abs(n) < small_value) n = 0.0;
  if (std::abs(w1) < small_value) w1 = 0.0;
  if (std::abs(w2) < small_value) w2 = 0.0;
  const double tmp = n - (w1 * b1) - (w2 * b2);
  const double out = (tmp * a0) + (w1 * a1) + (w2 * a2);
  w2 = w1;
  w1 = tmp;
  return out;
}

void BassEnhancerProcessor::Biquad::sanitize() noexcept {
  if (std::abs(w1) < small_value) w1 = 0.0;
  if (std::abs(w2) < small_value) w2 = 0.0;
}

void BassEnhancerProcessor::Biquad::reset() noexcept {
  w1 = 0.0;
  w2 = 0.0;
}

void BassEnhancerProcessor::TapDistortion::set_sample_rate(const unsigned rate) noexcept {
  srate = std::max(2U, rate);
  over = static_cast<float>(srate) * 2.0F > 96000.0F ? 1 : 2;
  const float factor = static_cast<float>(over);
  const float cutoff = std::max(25000.0F, static_cast<float>(srate) / 2.0F);
  up[0].set_lowpass(cutoff, 0.8F, static_cast<float>(srate) * factor);
  up[1].copy_coeffs(up[0]);
  down[1].copy_coeffs(up[0]);
}

void BassEnhancerProcessor::TapDistortion::set_params(const float blend, const float drive) noexcept {
  if (drive_old == drive && blend_old == blend) {
    return;
  }
  rdrive = 12.0F / drive;
  rbdr = rdrive / (10.5F - blend) * 780.0F / 33.0F;
  kpa = tap_d((2.0F * (rdrive * rdrive)) - 1.0F) + 1.0F;
  kpb = (2.0F - kpa) / 2.0F;
  ap = ((rdrive * rdrive) - kpa + 1.0F) / 2.0F;
  kc = kpa / tap_d((2.0F * tap_d((2.0F * (rdrive * rdrive)) - 1.0F)) - (2.0F * rdrive * rdrive));
  srct = (0.1F * static_cast<float>(srate)) / ((0.1F * static_cast<float>(srate)) + 1.0F);
  sq = (kc * kc) + 1.0F;
  knb = -1.0F * rbdr / tap_d(sq);
  kna = 2.0F * kc * rbdr / tap_d(sq);
  an = (rbdr * rbdr) / sq;
  imr = (2.0F * knb) + tap_d((2.0F * kna) + (4.0F * an) - 1.0F);
  pwrq = 2.0F / (imr + 1.0F);
  drive_old = drive;
  blend_old = blend;
}

auto BassEnhancerProcessor::TapDistortion::upsample(const double sample) noexcept -> double* {
  tmp[0] = sample;
  if (over > 1) {
    for (int f = 0; f < oversample_filters; ++f) {
      tmp[0] = up[f].process(sample);
    }
    for (int i = 1; i < over; ++i) {
      tmp[i] = 0.0;
      for (int f = 0; f < oversample_filters; ++f) {
        tmp[i] = up[f].process(sample);
      }
    }
  }
  return tmp;
}

auto BassEnhancerProcessor::TapDistortion::downsample(double* const samples) noexcept -> double {
  if (over > 1) {
    for (int i = 0; i < over; ++i) {
      for (int f = 0; f < oversample_filters; ++f) {
        samples[i] = down[f].process(samples[i]);
      }
    }
  }
  return samples[0];
}

auto BassEnhancerProcessor::TapDistortion::process(const float in) noexcept -> float {
  double* samples = upsample(static_cast<double>(in));
  meter = 0.0F;
  for (int o = 0; o < over; ++o) {
    float proc = static_cast<float>(samples[o]);
    float med = 0.0F;
    if (proc >= 0.0F) {
      med = (tap_d(ap + (proc * (kpa - proc))) + kpb) * pwrq;
    } else {
      med = (tap_d(an - (proc * (kna + proc))) + knb) * pwrq * -1.0F;
    }
    proc = srct * (med - prev_med + prev_out);
    prev_med = tap_m(med);
    prev_out = tap_m(proc);
    samples[o] = proc;
    meter = std::max(meter, proc);
  }
  return static_cast<float>(downsample(samples));
}

void BassEnhancerProcessor::TapDistortion::reset() noexcept {
  up[0].reset();
  up[1].reset();
  down[0].reset();
  down[1].reset();
  for (auto& value : tmp) value = 0.0;
  srate = 0;
  over = 2;
  drive_old = -1.0F;
  blend_old = -1.0F;
  meter = 0.0F;
  prev_med = 0.0F;
  prev_out = 0.0F;
}

void BassEnhancerProcessor::params_changed() noexcept {
  const float scope = scope_hz_.load(std::memory_order_relaxed);
  if (scope != freq_old_) {
    lp_[0][0].set_lowpass(scope, 0.707F, sample_rate_value_);
    for (int i = 1; i < 4; ++i) {
      lp_[0][i].copy_coeffs(lp_[0][0]);
      lp_[1][i].copy_coeffs(lp_[0][0]);
    }
    lp_[1][0].copy_coeffs(lp_[0][0]);
    freq_old_ = scope;
  }

  const float floor_hz = floor_hz_.load(std::memory_order_relaxed);
  const bool floor_active = floor_active_.load(std::memory_order_relaxed);
  if (floor_hz != floor_old_ || floor_active != floor_active_old_) {
    hp_[0][0].set_highpass(floor_hz, 0.707F, sample_rate_value_);
    hp_[0][1].copy_coeffs(hp_[0][0]);
    hp_[1][0].copy_coeffs(hp_[0][0]);
    hp_[1][1].copy_coeffs(hp_[0][0]);
    floor_old_ = floor_hz;
    floor_active_old_ = floor_active;
  }

  const float blend = blend_.load(std::memory_order_relaxed);
  const float drive = harmonics_.load(std::memory_order_relaxed);
  dist_[0].set_params(blend, drive);
  dist_[1].set_params(blend, drive);
}

void BassEnhancerProcessor::set_input_gain_db(const float gain_db) noexcept {
  const float clamped = clamp_gain(gain_db);
  input_gain_db_.store(clamped, std::memory_order_relaxed);
  input_gain_linear_.store(db_to_linear(clamped), std::memory_order_relaxed);
}

void BassEnhancerProcessor::set_output_gain_db(const float gain_db) noexcept {
  const float clamped = clamp_gain(gain_db);
  output_gain_db_.store(clamped, std::memory_order_relaxed);
  output_gain_linear_.store(db_to_linear(clamped), std::memory_order_relaxed);
}

auto BassEnhancerProcessor::prepare(const float sample_rate) noexcept -> bool {
  if (!std::isfinite(sample_rate) || sample_rate < 1.0F || sample_rate > 768000.0F) {
    return false;
  }
  sample_rate_value_ = sample_rate;
  sample_rate_.store(sample_rate, std::memory_order_relaxed);

  for (auto& filter : lp_[0]) filter.reset();
  for (auto& filter : lp_[1]) filter.reset();
  for (auto& filter : hp_[0]) filter.reset();
  for (auto& filter : hp_[1]) filter.reset();
  freq_old_ = 0.0F;
  floor_old_ = 0.0F;
  floor_active_old_ = false;

  const auto rate = static_cast<unsigned>(std::lround(sample_rate));
  dist_[0].reset();
  dist_[1].reset();
  dist_[0].set_sample_rate(rate);
  dist_[1].set_sample_rate(rate);

  harmonics_level_db_.store(minimum_db_level, std::memory_order_relaxed);
  for (auto& peak : peak_db_) peak.store(minimum_db_level, std::memory_order_relaxed);

  // Calf's vumeters set_falloff(1.f, srate): 20 dB of decay per second.
  meter_falloff_ = static_cast<float>(std::pow(0.1, 1.0 / static_cast<double>(sample_rate_value_)));
  harmonics_level_linear_ = 0.0F;

  params_changed();
  return true;
}

void BassEnhancerProcessor::set_parameters(const BassEnhancerParameters& parameters) noexcept {
  const float amount_db =
      std::isfinite(parameters.amount_db) ? std::clamp(parameters.amount_db, minimum_amount_db, maximum_amount_db) : 0.0F;
  amount_db_.store(amount_db, std::memory_order_relaxed);
  amount_linear_.store(db_to_linear(amount_db), std::memory_order_relaxed);
  harmonics_.store(std::isfinite(parameters.harmonics)
                       ? std::clamp(parameters.harmonics, minimum_harmonics, maximum_harmonics)
                       : 8.5F,
                   std::memory_order_relaxed);
  scope_hz_.store(std::isfinite(parameters.scope_hz)
                      ? std::clamp(parameters.scope_hz, minimum_scope_hz, maximum_scope_hz)
                      : 100.0F,
                  std::memory_order_relaxed);
  floor_hz_.store(std::isfinite(parameters.floor_hz)
                      ? std::clamp(parameters.floor_hz, minimum_floor_hz, maximum_floor_hz)
                      : 20.0F,
                  std::memory_order_relaxed);
  blend_.store(std::isfinite(parameters.blend) ? std::clamp(parameters.blend, minimum_blend, maximum_blend) : 0.0F,
               std::memory_order_relaxed);
  floor_active_.store(parameters.floor_active, std::memory_order_relaxed);
  listen_.store(parameters.listen, std::memory_order_relaxed);
  set_input_gain_db(parameters.input_gain_db);
  set_output_gain_db(parameters.output_gain_db);
  bypass_.store(parameters.bypass, std::memory_order_relaxed);
}

auto BassEnhancerProcessor::parameters() const noexcept -> BassEnhancerParameters {
  return {
      .amount_db = amount_db_.load(std::memory_order_relaxed),
      .harmonics = harmonics_.load(std::memory_order_relaxed),
      .scope_hz = scope_hz_.load(std::memory_order_relaxed),
      .floor_hz = floor_hz_.load(std::memory_order_relaxed),
      .blend = blend_.load(std::memory_order_relaxed),
      .floor_active = floor_active_.load(std::memory_order_relaxed),
      .listen = listen_.load(std::memory_order_relaxed),
      .input_gain_db = input_gain_db_.load(std::memory_order_relaxed),
      .output_gain_db = output_gain_db_.load(std::memory_order_relaxed),
      .bypass = bypass_.load(std::memory_order_relaxed),
  };
}

void BassEnhancerProcessor::reset() noexcept {
  set_parameters(BassEnhancerParameters{});
}

void BassEnhancerProcessor::process(float* const interleaved_stereo, const std::size_t frames) noexcept {
  if (interleaved_stereo == nullptr || frames == 0U || !(sample_rate_value_ > 0.0F)) {
    return;
  }

  params_changed();

  if (bypass_.load(std::memory_order_relaxed)) {
    return;
  }

  const float amount = amount_linear_.load(std::memory_order_relaxed);
  const float input_gain = input_gain_linear_.load(std::memory_order_relaxed);
  const float output_gain = output_gain_linear_.load(std::memory_order_relaxed);
  const bool listen = listen_.load(std::memory_order_relaxed);
  const bool floor_active = floor_active_.load(std::memory_order_relaxed);

  float input_peak_left = 0.0F;
  float input_peak_right = 0.0F;
  float output_peak_left = 0.0F;
  float output_peak_right = 0.0F;

  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto left_index = frame * 2U;
    const auto right_index = left_index + 1U;

    const float in_left = interleaved_stereo[left_index] * input_gain;
    const float in_right = interleaved_stereo[right_index] * input_gain;

    float proc_left = in_left;
    proc_left = static_cast<float>(lp_[0][1].process(lp_[0][0].process(proc_left)));
    proc_left = dist_[0].process(proc_left);
    proc_left = static_cast<float>(lp_[0][2].process(lp_[0][3].process(proc_left)));
    if (floor_active) {
      proc_left = static_cast<float>(hp_[0][0].process(hp_[0][1].process(proc_left)));
    }

    float proc_right = in_right;
    proc_right = static_cast<float>(lp_[1][1].process(lp_[1][0].process(proc_right)));
    proc_right = dist_[1].process(proc_right);
    proc_right = static_cast<float>(lp_[1][2].process(lp_[1][3].process(proc_right)));
    if (floor_active) {
      proc_right = static_cast<float>(hp_[1][0].process(hp_[1][1].process(proc_right)));
    }

    const float out_left = (listen ? proc_left * amount : (proc_left * amount) + in_left) * output_gain;
    const float out_right = (listen ? proc_right * amount : (proc_right * amount) + in_right) * output_gain;

    interleaved_stereo[left_index] = out_left;
    interleaved_stereo[right_index] = out_right;

    input_peak_left = std::max(input_peak_left, std::abs(in_left));
    input_peak_right = std::max(input_peak_right, std::abs(in_right));
    output_peak_left = std::max(output_peak_left, std::abs(out_left));
    output_peak_right = std::max(output_peak_right, std::abs(out_right));

    // Calf feeds max(dist0, dist1) * amount to a peak-hold meter each sample.
    harmonics_level_linear_ = std::max(harmonics_level_linear_, std::max(dist_[0].meter, dist_[1].meter) * amount);
  }

  for (auto& filter : lp_[0]) filter.sanitize();
  for (auto& filter : lp_[1]) filter.sanitize();
  for (auto& filter : hp_[0]) filter.sanitize();
  for (auto& filter : hp_[1]) filter.sanitize();

  harmonics_level_linear_ *= static_cast<float>(std::pow(static_cast<double>(meter_falloff_),
                                                          static_cast<double>(frames)));
  if (std::abs(harmonics_level_linear_) < static_cast<float>(small_value)) {
    harmonics_level_linear_ = 0.0F;
  }

  peak_db_[0].store(linear_to_db(input_peak_left), std::memory_order_relaxed);
  peak_db_[1].store(linear_to_db(input_peak_right), std::memory_order_relaxed);
  peak_db_[2].store(linear_to_db(output_peak_left), std::memory_order_relaxed);
  peak_db_[3].store(linear_to_db(output_peak_right), std::memory_order_relaxed);
  harmonics_level_db_.store(linear_to_db(harmonics_level_linear_), std::memory_order_relaxed);
}

auto BassEnhancerProcessor::levels() const noexcept -> BassEnhancerLevels {
  return {
      .input_left_db = peak_db_[0].load(std::memory_order_relaxed),
      .input_right_db = peak_db_[1].load(std::memory_order_relaxed),
      .output_left_db = peak_db_[2].load(std::memory_order_relaxed),
      .output_right_db = peak_db_[3].load(std::memory_order_relaxed),
      .harmonics_level_db = harmonics_level_db_.load(std::memory_order_relaxed),
  };
}

}  // namespace ee::mobile
