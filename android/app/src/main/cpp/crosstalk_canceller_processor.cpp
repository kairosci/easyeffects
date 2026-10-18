/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Crosstalk Canceller algorithm by Antti S. Lankila <alankila@bel.fi>.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "crosstalk_canceller_processor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ee::mobile {
namespace {
constexpr double pi = std::numbers::pi_v<double>;
constexpr float minimum_gain_db = -36.0F;
constexpr float maximum_gain_db = 36.0F;

auto level_db(const float peak) noexcept -> float {
  return peak >= 0.00001F ? 20.0F * std::log10(peak) : -100.0F;
}

auto finite_clamp(const float value, const float fallback, const float low, const float high) noexcept -> float {
  return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
}  // namespace

void CrosstalkCancellerProcessor::Biquad::set_coefficients(const double a0,
                                                            const double a1,
                                                            const double a2,
                                                            const double b0,
                                                            const double b1,
                                                            const double b2) noexcept {
  a1_ = static_cast<float>(a1 / a0);
  a2_ = static_cast<float>(a2 / a0);
  b0_ = static_cast<float>(b0 / a0);
  b1_ = static_cast<float>(b1 / a0);
  b2_ = static_cast<float>(b2 / a0);
}

void CrosstalkCancellerProcessor::Biquad::set_high_pass(const double frequency,
                                                         const double sample_rate,
                                                         const double quality) noexcept {
  const double w0 = 2.0 * pi * frequency / sample_rate;
  const double alpha = std::sin(w0) / (2.0 * quality);
  const double cosine = std::cos(w0);
  set_coefficients(1.0 + alpha, -2.0 * cosine, 1.0 - alpha,
                   (1.0 + cosine) / 2.0, -(1.0 + cosine), (1.0 + cosine) / 2.0);
}

void CrosstalkCancellerProcessor::Biquad::set_low_pass(const double frequency,
                                                        const double sample_rate,
                                                        const double quality) noexcept {
  const double w0 = 2.0 * pi * frequency / sample_rate;
  const double alpha = std::sin(w0) / (2.0 * quality);
  const double cosine = std::cos(w0);
  set_coefficients(1.0 + alpha, -2.0 * cosine, 1.0 - alpha,
                   (1.0 - cosine) / 2.0, 1.0 - cosine, (1.0 - cosine) / 2.0);
}

void CrosstalkCancellerProcessor::Biquad::set_peaking(const double frequency,
                                                       const double sample_rate,
                                                       const double gain_db,
                                                       const double quality) noexcept {
  const double w0 = 2.0 * pi * frequency / sample_rate;
  const double amplitude = std::pow(10.0, gain_db / 40.0);
  const double alpha = std::sin(w0) / (2.0 * quality);
  set_coefficients(1.0 + alpha / amplitude, -2.0 * std::cos(w0), 1.0 - alpha / amplitude,
                   1.0 + alpha * amplitude, -2.0 * std::cos(w0), 1.0 - alpha * amplitude);
}

void CrosstalkCancellerProcessor::Biquad::clear() noexcept {
  x1_ = 0.0F;
  x2_ = 0.0F;
  y1_ = 0.0F;
  y2_ = 0.0F;
}

auto CrosstalkCancellerProcessor::Biquad::process(const float input) noexcept -> float {
  const float output = b0_ * input + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
  y2_ = y1_;
  y1_ = output;
  x2_ = x1_;
  x1_ = input;
  return output;
}

void CrosstalkCancellerProcessor::FilteredDelay::configure_filters(const float sample_rate) noexcept {
  filters[0].set_high_pass(250.0, sample_rate, 0.710);
  filters[1].set_low_pass(3245.0, sample_rate, 0.710);
  filters[2].set_peaking(688.0, sample_rate, -2.9, 1.000);
  filters[3].set_peaking(1066.0, sample_rate, -6.4, 3.352);
  filters[4].set_peaking(2190.0, sample_rate, -5.6, 2.037);
  filters[5].set_peaking(3792.0, sample_rate, -4.5, 3.232);
  for (auto& filter : filters) filter.clear();
}

void CrosstalkCancellerProcessor::FilteredDelay::clear() noexcept {
  std::fill(history.begin(), history.end(), 0.0F);
  write_index = 0U;
  for (auto& filter : filters) filter.clear();
}

auto CrosstalkCancellerProcessor::FilteredDelay::delayed(const std::size_t delay) const noexcept -> float {
  if (history.empty() || delay == 0U || delay >= history.size()) return 0.0F;
  const auto read_index = (write_index + history.size() - delay) % history.size();
  return history[read_index];
}

void CrosstalkCancellerProcessor::FilteredDelay::push(float value) noexcept {
  for (auto& filter : filters) value = filter.process(value);
  if (history.empty()) return;
  history[write_index] = value;
  write_index = (write_index + 1U) % history.size();
}

auto CrosstalkCancellerProcessor::clamp_parameters(CrosstalkParameters p) noexcept -> CrosstalkParameters {
  p.delay_us = finite_clamp(p.delay_us, 313.0F, 200.0F, 500.0F);
  p.decay_db = finite_clamp(p.decay_db, -2.0F, -6.0F, 0.0F);
  p.input_gain_db = finite_clamp(p.input_gain_db, 0.0F, minimum_gain_db, maximum_gain_db);
  p.output_gain_db = finite_clamp(p.output_gain_db, 0.0F, minimum_gain_db, maximum_gain_db);
  return p;
}

auto CrosstalkCancellerProcessor::gain_from_db(const float gain_db) noexcept -> float {
  return std::pow(10.0F, gain_db / 20.0F);
}

auto CrosstalkCancellerProcessor::prepare(const float sample_rate) -> bool {
  if (!std::isfinite(sample_rate) || sample_rate < 8000.0F || sample_rate > 768000.0F) return false;
  const std::size_t history_size = static_cast<std::size_t>(std::ceil(sample_rate * 0.0005F)) + 1U;
  try {
    for (auto& line : lines_) line.history.assign(history_size, 0.0F);
  } catch (...) {
    for (auto& line : lines_) line.history.clear();
    sample_rate_ = 0.0F;
    return false;
  }
  sample_rate_ = sample_rate;
  applied_delay_us_ = 0.0F;
  for (auto& line : lines_) line.configure_filters(sample_rate);
  return true;
}

void CrosstalkCancellerProcessor::set_parameters(const CrosstalkParameters& parameters) noexcept {
  const auto p = clamp_parameters(parameters);
  delay_us_.store(p.delay_us, std::memory_order_relaxed);
  decay_db_.store(p.decay_db, std::memory_order_relaxed);
  phantom_center_only_.store(p.phantom_center_only, std::memory_order_relaxed);
  input_gain_db_.store(p.input_gain_db, std::memory_order_relaxed);
  output_gain_db_.store(p.output_gain_db, std::memory_order_relaxed);
  bypass_.store(p.bypass, std::memory_order_relaxed);
}

auto CrosstalkCancellerProcessor::parameters() const noexcept -> CrosstalkParameters {
  return {
      .delay_us = delay_us_.load(std::memory_order_relaxed),
      .decay_db = decay_db_.load(std::memory_order_relaxed),
      .phantom_center_only = phantom_center_only_.load(std::memory_order_relaxed),
      .input_gain_db = input_gain_db_.load(std::memory_order_relaxed),
      .output_gain_db = output_gain_db_.load(std::memory_order_relaxed),
      .bypass = bypass_.load(std::memory_order_relaxed),
  };
}

auto CrosstalkCancellerProcessor::levels() const noexcept -> CrosstalkLevels {
  return {
      .input_left_db = levels_db_[0].load(std::memory_order_relaxed),
      .input_right_db = levels_db_[1].load(std::memory_order_relaxed),
      .output_left_db = levels_db_[2].load(std::memory_order_relaxed),
      .output_right_db = levels_db_[3].load(std::memory_order_relaxed),
  };
}

void CrosstalkCancellerProcessor::reset() noexcept {
  set_parameters(CrosstalkParameters{});
  reset_requested_.store(true, std::memory_order_relaxed);
}

void CrosstalkCancellerProcessor::process(float* const buffer, const std::size_t frames) noexcept {
  if (buffer == nullptr || frames == 0U || sample_rate_ <= 0.0F || lines_[0].history.empty() || lines_[1].history.empty()) {
    return;
  }

  const auto p = parameters();
  if (reset_requested_.exchange(false, std::memory_order_relaxed)) {
    for (auto& line : lines_) line.clear();
    applied_delay_us_ = 0.0F;
  }
  if (p.bypass) {
    std::array<float, 2> peak{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
      peak[0] = std::max(peak[0], std::abs(buffer[frame * 2U]));
      peak[1] = std::max(peak[1], std::abs(buffer[frame * 2U + 1U]));
    }
    levels_db_[0].store(level_db(peak[0]), std::memory_order_relaxed);
    levels_db_[1].store(level_db(peak[1]), std::memory_order_relaxed);
    levels_db_[2].store(level_db(peak[0]), std::memory_order_relaxed);
    levels_db_[3].store(level_db(peak[1]), std::memory_order_relaxed);
    return;
  }

  const std::size_t next_delay = static_cast<std::size_t>(std::llround(p.delay_us * 0.000001 * sample_rate_));
  if (next_delay != delay_samples_ || p.delay_us != applied_delay_us_) {
    for (auto& line : lines_) {
      std::fill(line.history.begin(), line.history.end(), 0.0F);
      line.write_index = 0U;
    }
    delay_samples_ = std::clamp<std::size_t>(next_delay, 1U, lines_[0].history.size() - 1U);
    applied_delay_us_ = p.delay_us;
  }

  const float input_gain = gain_from_db(p.input_gain_db);
  const float output_gain = gain_from_db(p.output_gain_db);
  const float decay = gain_from_db(p.decay_db);
  std::array<float, 4> peaks{};
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float left = buffer[frame * 2U] * input_gain;
    const float right = buffer[frame * 2U + 1U] * input_gain;
    peaks[0] = std::max(peaks[0], std::abs(left));
    peaks[1] = std::max(peaks[1], std::abs(right));
    float output_left = 0.0F;
    float output_right = 0.0F;

    if (p.phantom_center_only) {
      const float middle = left + right;
      const float side = left - right;
      const float middle_out = middle - decay * lines_[0].delayed(delay_samples_);
      output_left = (middle_out + side) * 0.5F;
      output_right = (middle_out - side) * 0.5F;
      lines_[0].push(middle_out);
    } else {
      output_left = left - decay * lines_[1].delayed(delay_samples_);
      output_right = right - decay * lines_[0].delayed(delay_samples_);
      lines_[0].push(output_left);
      lines_[1].push(output_right);
    }

    output_left *= output_gain;
    output_right *= output_gain;
    peaks[2] = std::max(peaks[2], std::abs(output_left));
    peaks[3] = std::max(peaks[3], std::abs(output_right));
    buffer[frame * 2U] = output_left;
    buffer[frame * 2U + 1U] = output_right;
  }
  for (std::size_t meter = 0; meter < peaks.size(); ++meter) {
    levels_db_[meter].store(level_db(peaks[meter]), std::memory_order_relaxed);
  }
}

}  // namespace ee::mobile
