/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "crossfeed_processor.hpp"

#include "bs2b.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace ee::mobile {
namespace {
constexpr int minimum_cutoff_hz = BS2B_MINFCUT;
constexpr int maximum_cutoff_hz = BS2B_MAXFCUT;
constexpr int minimum_feed_tenths_db = BS2B_MINFEED;
constexpr int maximum_feed_tenths_db = BS2B_MAXFEED;
constexpr float minimum_gain_db = -36.0F;
constexpr float maximum_gain_db = 36.0F;
constexpr int minimum_sample_rate = BS2B_MINSRATE;
constexpr int maximum_sample_rate = BS2B_MAXSRATE;
constexpr std::uint32_t feed_mask = 0xFFU;

auto clamp_gain(const float gain_db) noexcept -> float {
  if (!std::isfinite(gain_db)) return 0.0F;
  return std::clamp(gain_db, minimum_gain_db, maximum_gain_db);
}

auto level_db(const float peak) noexcept -> float {
  return peak >= 0.00001F ? 20.0F * std::log10(peak) : -100.0F;
}
}  // namespace

CrossfeedProcessor::CrossfeedProcessor() : state_(bs2b_open()) {
  if (state_ == nullptr) throw std::bad_alloc();
  bs2b_set_level(state_, packed_parameters_.load(std::memory_order_relaxed));
  applied_parameters_ = packed_parameters_.load(std::memory_order_relaxed);
}

CrossfeedProcessor::~CrossfeedProcessor() {
  if (state_ != nullptr) bs2b_close(state_);
}

auto CrossfeedProcessor::pack_parameters(const int cutoff_hz, const int feed_tenths_db) noexcept
    -> std::uint32_t {
  const auto cutoff = static_cast<std::uint32_t>(std::clamp(cutoff_hz, minimum_cutoff_hz, maximum_cutoff_hz));
  const auto feed = static_cast<std::uint32_t>(
      std::clamp(feed_tenths_db, minimum_feed_tenths_db, maximum_feed_tenths_db));
  return cutoff | (feed << 16U);
}

auto CrossfeedProcessor::linear_gain(const float gain_db) noexcept -> float {
  return std::pow(10.0F, clamp_gain(gain_db) / 20.0F);
}

void CrossfeedProcessor::set_cutoff_hz(const int cutoff_hz) noexcept {
  std::uint32_t current = packed_parameters_.load(std::memory_order_relaxed);
  for (;;) {
    const auto feed = static_cast<int>((current >> 16U) & feed_mask);
    const auto next = pack_parameters(cutoff_hz, feed);
    if (packed_parameters_.compare_exchange_weak(current, next, std::memory_order_relaxed)) return;
  }
}

void CrossfeedProcessor::set_feed_db(const float feed_db) noexcept {
  if (!std::isfinite(feed_db)) return;
  const int feed_tenths_db = static_cast<int>(std::lround(std::clamp(feed_db, 1.0F, 15.0F) * 10.0F));
  std::uint32_t current = packed_parameters_.load(std::memory_order_relaxed);
  for (;;) {
    const int cutoff = static_cast<int>(current & 0xFFFFU);
    const auto next = pack_parameters(cutoff, feed_tenths_db);
    if (packed_parameters_.compare_exchange_weak(current, next, std::memory_order_relaxed)) return;
  }
}

void CrossfeedProcessor::set_input_gain_db(const float gain_db) noexcept {
  const float value = clamp_gain(gain_db);
  input_gain_db_.store(value, std::memory_order_relaxed);
  input_gain_linear_.store(linear_gain(value), std::memory_order_relaxed);
}

void CrossfeedProcessor::set_output_gain_db(const float gain_db) noexcept {
  const float value = clamp_gain(gain_db);
  output_gain_db_.store(value, std::memory_order_relaxed);
  output_gain_linear_.store(linear_gain(value), std::memory_order_relaxed);
}

void CrossfeedProcessor::set_bypass(const bool bypass) noexcept {
  bypass_.store(bypass, std::memory_order_relaxed);
}

void CrossfeedProcessor::apply_preset(const CrossfeedPreset preset) noexcept {
  switch (preset) {
    case CrossfeedPreset::default_:
      set_cutoff_hz(700);
      set_feed_db(4.5F);
      break;
    case CrossfeedPreset::cmoy:
      set_cutoff_hz(700);
      set_feed_db(6.0F);
      break;
    case CrossfeedPreset::jmeier:
      set_cutoff_hz(650);
      set_feed_db(9.5F);
      break;
  }
}

void CrossfeedProcessor::reset() noexcept {
  apply_preset(CrossfeedPreset::default_);
  set_input_gain_db(0.0F);
  set_output_gain_db(0.0F);
  set_bypass(false);
  reset_requested_.store(true, std::memory_order_relaxed);
  for (auto& level : levels_db_) level.store(-100.0F, std::memory_order_relaxed);
}

void CrossfeedProcessor::process(float* const interleaved_stereo,
                                 const std::size_t frames,
                                 const float sample_rate) noexcept {
  if (interleaved_stereo == nullptr || frames == 0U ||
      frames > static_cast<std::size_t>(std::numeric_limits<int>::max()) || !std::isfinite(sample_rate)) {
    return;
  }
  if (bypass_.load(std::memory_order_relaxed)) {
    std::array<float, 2> peak{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
      peak[0] = std::max(peak[0], std::abs(interleaved_stereo[frame * 2U]));
      peak[1] = std::max(peak[1], std::abs(interleaved_stereo[frame * 2U + 1U]));
    }
    levels_db_[0].store(level_db(peak[0]), std::memory_order_relaxed);
    levels_db_[1].store(level_db(peak[1]), std::memory_order_relaxed);
    levels_db_[2].store(level_db(peak[0]), std::memory_order_relaxed);
    levels_db_[3].store(level_db(peak[1]), std::memory_order_relaxed);
    return;
  }
  if (sample_rate < static_cast<float>(minimum_sample_rate) ||
      sample_rate > static_cast<float>(maximum_sample_rate)) {
    return;
  }
  const auto rate = static_cast<int>(std::lround(sample_rate));

  const std::uint32_t current_parameters = packed_parameters_.load(std::memory_order_relaxed);
  if (reset_requested_.exchange(false, std::memory_order_relaxed)) bs2b_clear(state_);
  if (current_parameters != applied_parameters_) {
    bs2b_set_level(state_, current_parameters);
    applied_parameters_ = current_parameters;
  }
  const auto unsigned_rate = static_cast<std::uint32_t>(rate);
  if (unsigned_rate != applied_sample_rate_) {
    bs2b_set_srate(state_, unsigned_rate);
    applied_sample_rate_ = unsigned_rate;
  }

  const float input_gain = input_gain_linear_.load(std::memory_order_relaxed);
  const float output_gain = output_gain_linear_.load(std::memory_order_relaxed);
  const std::size_t samples = frames * 2U;
  std::array<float, 2> input_peak{};
  for (std::size_t frame = 0; frame < frames; ++frame) {
    input_peak[0] = std::max(input_peak[0], std::abs(interleaved_stereo[frame * 2U] * input_gain));
    input_peak[1] = std::max(input_peak[1], std::abs(interleaved_stereo[frame * 2U + 1U] * input_gain));
  }
  if (input_gain != 1.0F) {
    for (std::size_t sample = 0; sample < samples; ++sample) interleaved_stereo[sample] *= input_gain;
  }
  bs2b_cross_feed_f(state_, interleaved_stereo, static_cast<int>(frames));
  if (output_gain != 1.0F) {
    for (std::size_t sample = 0; sample < samples; ++sample) interleaved_stereo[sample] *= output_gain;
  }
  std::array<float, 2> output_peak{};
  for (std::size_t frame = 0; frame < frames; ++frame) {
    output_peak[0] = std::max(output_peak[0], std::abs(interleaved_stereo[frame * 2U]));
    output_peak[1] = std::max(output_peak[1], std::abs(interleaved_stereo[frame * 2U + 1U]));
  }
  levels_db_[0].store(level_db(input_peak[0]), std::memory_order_relaxed);
  levels_db_[1].store(level_db(input_peak[1]), std::memory_order_relaxed);
  levels_db_[2].store(level_db(output_peak[0]), std::memory_order_relaxed);
  levels_db_[3].store(level_db(output_peak[1]), std::memory_order_relaxed);
}

auto CrossfeedProcessor::parameters() const noexcept -> CrossfeedParameters {
  const auto packed = packed_parameters_.load(std::memory_order_relaxed);
  return {
      .cutoff_hz = static_cast<int>(packed & 0xFFFFU),
      .feed_db = static_cast<float>((packed >> 16U) & feed_mask) / 10.0F,
      .input_gain_db = input_gain_db_.load(std::memory_order_relaxed),
      .output_gain_db = output_gain_db_.load(std::memory_order_relaxed),
      .bypass = bypass_.load(std::memory_order_relaxed),
  };
}

auto CrossfeedProcessor::levels() const noexcept -> CrossfeedLevels {
  return {
      .input_left_db = levels_db_[0].load(std::memory_order_relaxed),
      .input_right_db = levels_db_[1].load(std::memory_order_relaxed),
      .output_left_db = levels_db_[2].load(std::memory_order_relaxed),
      .output_right_db = levels_db_[3].load(std::memory_order_relaxed),
  };
}

}  // namespace ee::mobile
