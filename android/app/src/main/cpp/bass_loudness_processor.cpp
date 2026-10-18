/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Based on MDA Loudness by Paul Kellett and David Robillard (GPL-3.0-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bass_loudness_processor.hpp"

#include <algorithm>
#include <cmath>

namespace ee::mobile {
namespace {
struct Contour {
  float frequency;
  float resonance;
  float amount;
};

// Stevens-Davis equal-loudness contour coefficients from MDA Loudness.
constexpr std::array<Contour, 14> contours{{
    {402.0F, 0.0025F, 0.0F}, {334.0F, 0.0121F, 0.0F}, {256.0F, 0.0353F, 0.0F},
    {192.0F, 0.0900F, 0.0F}, {150.0F, 0.2116F, 0.0F}, {150.0F, 0.5185F, 0.0F},
    {1.0F, 0.0F, 0.0F}, {33.7F, 5.5F, 1.0F}, {92.0F, 8.7F, 0.62F},
    {63.7F, 18.4F, 0.44F}, {42.9F, 48.2F, 0.30F}, {37.6F, 116.2F, 0.18F},
    {22.9F, 428.7F, 0.09F}, {0.0F, 0.0F, 0.0F}}};

struct Coefficients {
  float smoothing;
  float resonance;
  float contour_amount;
  float gain;
  bool boost;
};

auto calculate(const BassLoudnessParameters& p, float sample_rate) noexcept -> Coefficients {
  // EasyEffects stores these controls in dB and converts them to linear LV2
  // values before MDA's normalized, quadratic parameter mapping.
  const float loudness_value = std::clamp(std::pow(10.0F, 0.05F * p.loudness_db), 0.0F, 1.0F);
  const float output_value = std::clamp(std::pow(10.0F, 0.05F * p.output_db), 0.0F, 1.0F);
  const float link_value = std::clamp(std::pow(10.0F, 0.05F * p.link_db), 0.0F, 1.0F);
  const float loudness = std::copysign(60.0F * std::pow(2.0F * loudness_value - 1.0F, 2.0F),
                                       loudness_value - 0.5F);
  const float output = std::copysign(60.0F * std::pow(2.0F * output_value - 1.0F, 2.0F),
                                     output_value - 0.5F);
  const float input_linear = std::clamp(p.input_gain_db, -36.0F, 36.0F);
  const float output_linear = std::clamp(p.output_gain_db, -36.0F, 36.0F);
  const float index = std::clamp(0.1F * loudness + 6.0F, 0.0F, 12.999F);
  const auto lower = static_cast<std::size_t>(index);
  const float fraction = index - static_cast<float>(lower);
  const auto interpolate = [lower, fraction](auto member) {
    const float a = contours[lower].*member;
    const float b = contours[lower + 1U].*member;
    return a + fraction * (b - a);
  };
  const float frequency = interpolate(&Contour::frequency);
  const float resonance = interpolate(&Contour::resonance);
  const float contour_amount = interpolate(&Contour::amount);
  const float link_compensation = link_value > 0.5F ? std::min(0.0F, output - loudness) : output;
  const float total_gain_db = link_compensation + input_linear + output_linear;
  const float smoothing = 1.0F - std::exp(-6.283153F * frequency / sample_rate);
  return {smoothing, resonance, contour_amount, std::pow(10.0F, 0.05F * total_gain_db), loudness > 0.0F};
}
}  // namespace

auto BassLoudnessProcessor::prepare(float sample_rate) noexcept -> bool {
  if (!std::isfinite(sample_rate) || sample_rate < 8000.0F || sample_rate > 384000.0F) return false;
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BassLoudnessProcessor::set_parameters(const BassLoudnessParameters& p) noexcept {
  parameter_values_[0].store(std::clamp(p.loudness_db, -100.0F, 0.0F), std::memory_order_relaxed);
  parameter_values_[1].store(std::clamp(p.output_db, -100.0F, 0.0F), std::memory_order_relaxed);
  parameter_values_[2].store(std::clamp(p.input_gain_db, -36.0F, 36.0F), std::memory_order_relaxed);
  parameter_values_[3].store(std::clamp(p.output_gain_db, -36.0F, 36.0F), std::memory_order_relaxed);
  link_db_.store(std::clamp(p.link_db, -100.0F, 0.0F), std::memory_order_relaxed);
  bypass_.store(p.bypass, std::memory_order_relaxed);
}

auto BassLoudnessProcessor::parameters() const noexcept -> BassLoudnessParameters {
  return {parameter_values_[0].load(std::memory_order_relaxed),
          parameter_values_[1].load(std::memory_order_relaxed),
          parameter_values_[2].load(std::memory_order_relaxed),
          parameter_values_[3].load(std::memory_order_relaxed),
          link_db_.load(std::memory_order_relaxed), bypass_.load(std::memory_order_relaxed)};
}

void BassLoudnessProcessor::reset() noexcept { state_.fill(0.0F); }

void BassLoudnessProcessor::process(float* pcm, std::size_t frames) noexcept {
  if (pcm == nullptr || sample_rate_ <= 0.0F) return;
  const auto p = parameters();
  const auto c = calculate(p, sample_rate_);
  const float post_gain = std::pow(10.0F, 0.05F * p.output_gain_db);
  if (p.bypass) return;
  const float pre_gain = std::pow(10.0F, 0.05F * p.input_gain_db);

  for (std::size_t i = 0; i < frames; ++i) {
    for (std::size_t channel = 0; channel < 2U; ++channel) {
      float x = pcm[i * 2U + channel] * pre_gain;
      auto& z0 = state_[channel * 2U];
      auto& z1 = state_[channel * 2U + 1U];
      if (c.boost) {
        z0 += c.smoothing * (x - z0);
        z1 += c.smoothing * (z0 - z1);
        x += c.resonance * (z1 - c.contour_amount * z0);
      } else {
        z0 += c.smoothing * (x - z0 + 0.3F * z1);
        x -= z0;
        z1 += c.smoothing * (x - z1);
        x -= z1 + z0 * c.resonance;
      }
      pcm[i * 2U + channel] = x * c.gain * post_gain;
    }
  }
  for (float& value : state_) {
    if (std::abs(value) < 1.0e-10F || std::abs(value) > 100.0F) value = 0.0F;
  }
}
}  // namespace ee::mobile
