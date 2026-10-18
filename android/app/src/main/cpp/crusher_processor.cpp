/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Adapted from Calf Studio Gear Crusher DSP by Markus Schmidt and Christian Holschuh.
 * Copyright © 2001-2010 Calf contributors. LGPL-2.1-only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "crusher_processor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ee::mobile {
namespace {
constexpr float min_gain_db = -36.0F;
constexpr float max_gain_db = 36.0F;
constexpr double pi = std::numbers::pi_v<double>;

auto finite_clamp(const float value, const float fallback, const float minimum, const float maximum) noexcept -> float {
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
}  // namespace

void CrusherProcessor::SampleReducer::set_amount(const float value) noexcept {
  amount = std::clamp(value, 1.0F, 250.0F);
  round = static_cast<unsigned int>(std::lround(amount));
}

auto CrusherProcessor::SampleReducer::process(const double value) noexcept -> double {
  ++samples;
  if (samples >= round) {
    target += amount;
    real += static_cast<float>(round);
    if (target + amount >= real + 1.0F) {
      last = value;
      target = 0.0F;
      real = 0.0F;
    }
    samples = 0U;
  }
  return last;
}

auto CrusherProcessor::clamp_parameters(CrusherParameters p) noexcept -> CrusherParameters {
  p.mode = p.mode == CrusherMode::logarithmic ? CrusherMode::logarithmic : CrusherMode::linear;
  p.bit_reduction = finite_clamp(p.bit_reduction, 4.0F, 1.0F, 16.0F);
  p.morph = finite_clamp(p.morph, 0.5F, 0.0F, 1.0F);
  p.dc_db = finite_clamp(p.dc_db, 0.0F, -12.041F, 12.041F);
  p.anti_aliasing = finite_clamp(p.anti_aliasing, 0.5F, 0.0F, 1.0F);
  p.sample_reduction = finite_clamp(p.sample_reduction, 1.0F, 1.0F, 250.0F);
  p.lfo_range = finite_clamp(p.lfo_range, 20.0F, 1.0F, 250.0F);
  p.lfo_rate_hz = finite_clamp(p.lfo_rate_hz, 0.3F, 0.01F, 200.0F);
  p.input_gain_db = finite_clamp(p.input_gain_db, 0.0F, min_gain_db, max_gain_db);
  p.output_gain_db = finite_clamp(p.output_gain_db, 0.0F, min_gain_db, max_gain_db);
  return p;
}

void CrusherProcessor::set_parameters(const CrusherParameters& parameters) noexcept {
  const auto p = clamp_parameters(parameters);
  mode_.store(p.mode, std::memory_order_relaxed);
  bit_reduction_.store(p.bit_reduction, std::memory_order_relaxed);
  morph_.store(p.morph, std::memory_order_relaxed);
  dc_db_.store(p.dc_db, std::memory_order_relaxed);
  anti_aliasing_.store(p.anti_aliasing, std::memory_order_relaxed);
  sample_reduction_.store(p.sample_reduction, std::memory_order_relaxed);
  lfo_active_.store(p.lfo_active, std::memory_order_relaxed);
  lfo_range_.store(p.lfo_range, std::memory_order_relaxed);
  lfo_rate_hz_.store(p.lfo_rate_hz, std::memory_order_relaxed);
  input_gain_db_.store(p.input_gain_db, std::memory_order_relaxed);
  output_gain_db_.store(p.output_gain_db, std::memory_order_relaxed);
  bypass_.store(p.bypass, std::memory_order_relaxed);
}

auto CrusherProcessor::parameters() const noexcept -> CrusherParameters {
  return {
      .mode = mode_.load(std::memory_order_relaxed),
      .bit_reduction = bit_reduction_.load(std::memory_order_relaxed),
      .morph = morph_.load(std::memory_order_relaxed),
      .dc_db = dc_db_.load(std::memory_order_relaxed),
      .anti_aliasing = anti_aliasing_.load(std::memory_order_relaxed),
      .sample_reduction = sample_reduction_.load(std::memory_order_relaxed),
      .lfo_active = lfo_active_.load(std::memory_order_relaxed),
      .lfo_range = lfo_range_.load(std::memory_order_relaxed),
      .lfo_rate_hz = lfo_rate_hz_.load(std::memory_order_relaxed),
      .input_gain_db = input_gain_db_.load(std::memory_order_relaxed),
      .output_gain_db = output_gain_db_.load(std::memory_order_relaxed),
      .bypass = bypass_.load(std::memory_order_relaxed),
  };
}

auto CrusherProcessor::levels() const noexcept -> CrusherLevels {
  return {
      .input_left_db = levels_db_[0].load(std::memory_order_relaxed),
      .input_right_db = levels_db_[1].load(std::memory_order_relaxed),
      .output_left_db = levels_db_[2].load(std::memory_order_relaxed),
      .output_right_db = levels_db_[3].load(std::memory_order_relaxed),
  };
}

void CrusherProcessor::reset() noexcept {
  set_parameters(CrusherParameters{});
  reset_requested_.store(true, std::memory_order_relaxed);
}

auto CrusherProcessor::gain_from_db(const float db) noexcept -> float {
  return std::pow(10.0F, db / 20.0F);
}

auto CrusherProcessor::quantize(float input, const CrusherParameters& p) noexcept -> float {
  const float dc = gain_from_db(p.dc_db);
  input = input > 0.0F ? input * dc : input / dc;

  const float coefficient = std::pow(2.0F, p.bit_reduction) - 1.0F;
  const float scale = std::sqrt(coefficient / 2.0F);
  const float aa1 = (1.0F - p.anti_aliasing) / 2.0F;
  double rounded = 0.0;

  if (p.mode == CrusherMode::linear) {
    const double y = static_cast<double>(input) * coefficient;
    const double k = std::round(y);
    if (p.anti_aliasing == 0.0F || (k - aa1 <= y && y <= k + aa1)) {
      rounded = k / coefficient;
    } else if (y > k + aa1) {
      const double lo = k / coefficient;
      rounded = lo + ((k + 1.0) / coefficient - lo) * 0.5 *
                            (std::sin(pi * (std::abs(y - k) - aa1) / p.anti_aliasing - pi / 2.0) + 1.0);
    } else {
      const double hi = k / coefficient;
      rounded = hi - (hi - (k - 1.0) / coefficient) * 0.5 *
                            (std::sin(pi * (std::abs(y - k) - aa1) / p.anti_aliasing - pi / 2.0) + 1.0);
    }
  } else {
    const double y = input == 0.0F ? 0.0 : scale * std::log(std::abs(input)) + scale * scale;
    const double k = std::round(y);
    if (input == 0.0F) {
      rounded = 0.0;
    } else if (p.anti_aliasing == 0.0F || (k - aa1 <= y && y <= k + aa1)) {
      rounded = std::copysign(std::exp(k / scale - scale), input);
    } else if (y > k + aa1) {
      const double v0 = std::exp(k / scale - scale);
      const double v1 = std::exp((k + 1.0) / scale - scale);
      rounded = std::copysign(v0 + (v1 - v0) * 0.5 *
                                      (std::sin(pi * (std::abs(y - k) - aa1) / p.anti_aliasing - pi / 2.0) + 1.0), input);
    } else {
      const double v0 = std::exp(k / scale - scale);
      const double v1 = std::exp((k - 1.0) / scale - scale);
      rounded = std::copysign(v0 - (v0 - v1) * 0.5 *
                                      (std::sin(pi * (std::abs(y - k) - aa1) / p.anti_aliasing - pi / 2.0) + 1.0), input);
    }
  }

  float wet = static_cast<float>(rounded);
  wet += (input - wet) * (1.0F - p.morph);
  return wet > 0.0F ? wet / dc : wet * dc;
}

void CrusherProcessor::process(float* const buffer, const std::size_t frames, const float sample_rate) noexcept {
  if (buffer == nullptr || frames == 0U || !std::isfinite(sample_rate) || sample_rate < 1.0F ||
      sample_rate > 768000.0F) return;
  if (bypass_.load(std::memory_order_relaxed)) {
    for (auto& level : levels_db_) level.store(-100.0F, std::memory_order_relaxed);
    return;
  }
  if (reset_requested_.exchange(false, std::memory_order_relaxed)) {
    left_reducer_ = {};
    right_reducer_ = {};
    lfo_phase_ = 0.0;
  }

  const auto p = parameters();
  const float input_gain = gain_from_db(p.input_gain_db);
  const float output_gain = gain_from_db(p.output_gain_db);
  const float half_range = p.lfo_range / 2.0F;
  const float min_samples = std::max(p.sample_reduction - half_range, 1.0F);
  const float below_min = p.sample_reduction - half_range - min_samples;
  const float max_samples = std::min(p.sample_reduction + half_range, 250.0F);
  const float above_max = p.sample_reduction + half_range - max_samples;
  const float amount_min = min_samples - above_max;
  const float amount_delta = max_samples - below_min - amount_min;
  const double phase_step = p.lfo_rate_hz / sample_rate;
  std::array<float, 4> peak_values{};

  for (std::size_t frame = 0; frame < frames; ++frame) {
    float amount = p.sample_reduction;
    if (p.lfo_active) {
      amount = amount_min + amount_delta *
          (static_cast<float>(std::sin(2.0 * pi * lfo_phase_)) + 0.5F);
      lfo_phase_ += phase_step;
      if (lfo_phase_ >= 1.0) lfo_phase_ -= std::floor(lfo_phase_);
    }
    left_reducer_.set_amount(amount);
    right_reducer_.set_amount(amount);
    for (std::size_t channel = 0; channel < 2U; ++channel) {
      const std::size_t index = frame * 2U + channel;
      const float dry = buffer[index] * input_gain;
      peak_values[channel] = std::max(peak_values[channel], std::abs(dry));
      const double held = channel == 0U ? left_reducer_.process(dry) : right_reducer_.process(dry);
      const float morphed = static_cast<float>(held) * p.morph + dry * (1.0F - p.morph);
      buffer[index] = quantize(morphed, p) * output_gain;
      peak_values[channel + 2U] = std::max(peak_values[channel + 2U], std::abs(buffer[index]));
    }
  }
  for (std::size_t meter = 0; meter < peak_values.size(); ++meter) {
    const float value = peak_values[meter] >= 0.00001F ? 20.0F * std::log10(peak_values[meter]) : -100.0F;
    levels_db_[meter].store(value, std::memory_order_relaxed);
  }
}

}  // namespace ee::mobile
