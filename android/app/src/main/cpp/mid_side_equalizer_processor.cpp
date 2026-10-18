/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mid_side_equalizer_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ee::mobile {
namespace {
constexpr float max_gain_db = 24.0F;
constexpr float min_frequency_hz = 10.0F;
constexpr float max_frequency_hz = 40000.0F;

auto linear_gain(const float gain_db) noexcept -> float {
  return std::pow(10.0F, gain_db / 20.0F);
}
}  // namespace

MidSideEqualizerProcessor::MidSideEqualizerProcessor() noexcept {
  for (std::size_t index = 0; index < band_count; ++index) {
    const float fraction = static_cast<float>(index) / static_cast<float>(band_count - 1U);
    const float frequency = 20.0F * std::pow(1000.0F, fraction);
    const BandSettings defaults{.type = FilterType::peaking,
                                .frequency_hz = frequency,
                                .q = 1.4F,
                                .gain_db = 0.0F,
                                .enabled = true};
    mid_bands_[index] = defaults;
    side_bands_[index] = defaults;
  }
}

auto MidSideEqualizerProcessor::shifted_band(BandSettings settings, const float semitones) noexcept
    -> BandSettings {
  settings.frequency_hz = std::clamp(settings.frequency_hz * std::pow(2.0F, semitones / 12.0F),
                                     min_frequency_hz, max_frequency_hz);
  return settings;
}

void MidSideEqualizerProcessor::apply_mid_band(const std::size_t index) noexcept {
  mid_eq_.set_band(index, shifted_band(mid_bands_[index], pitch_mid_.load(std::memory_order_relaxed)));
}

void MidSideEqualizerProcessor::apply_side_band(const std::size_t index) noexcept {
  side_eq_.set_band(index, shifted_band(side_bands_[index], pitch_side_.load(std::memory_order_relaxed)));
}

void MidSideEqualizerProcessor::set_mid_band(const std::size_t index, const BandSettings settings) noexcept {
  if (index >= band_count || !std::isfinite(settings.frequency_hz) || !std::isfinite(settings.q) ||
      !std::isfinite(settings.gain_db)) {
    return;
  }
  std::lock_guard lock(settings_mutex_);
  mid_bands_[index] = settings;
  apply_mid_band(index);
  if (!split_channels_.load(std::memory_order_relaxed)) {
    side_bands_[index] = settings;
    apply_side_band(index);
  }
}

void MidSideEqualizerProcessor::set_side_band(const std::size_t index, const BandSettings settings) noexcept {
  if (index >= band_count || !std::isfinite(settings.frequency_hz) || !std::isfinite(settings.q) ||
      !std::isfinite(settings.gain_db)) {
    return;
  }
  std::lock_guard lock(settings_mutex_);
  if (!split_channels_.load(std::memory_order_relaxed)) return;
  side_bands_[index] = settings;
  apply_side_band(index);
}

void MidSideEqualizerProcessor::set_split_channels(const bool split) noexcept {
  std::lock_guard lock(settings_mutex_);
  split_channels_.store(split, std::memory_order_relaxed);
  if (!split) {
    for (std::size_t index = 0; index < band_count; ++index) {
      side_bands_[index] = mid_bands_[index];
      apply_side_band(index);
    }
  }
}

void MidSideEqualizerProcessor::set_pitch_mid(const float semitones) noexcept {
  if (!std::isfinite(semitones)) return;
  const float bounded = std::clamp(semitones, -120.0F, 120.0F);
  std::lock_guard lock(settings_mutex_);
  pitch_mid_.store(bounded, std::memory_order_relaxed);
  for (std::size_t index = 0; index < band_count; ++index) apply_mid_band(index);
}

void MidSideEqualizerProcessor::set_pitch_side(const float semitones) noexcept {
  if (!std::isfinite(semitones)) return;
  const float bounded = std::clamp(semitones, -120.0F, 120.0F);
  std::lock_guard lock(settings_mutex_);
  pitch_side_.store(bounded, std::memory_order_relaxed);
  for (std::size_t index = 0; index < band_count; ++index) apply_side_band(index);
}

void MidSideEqualizerProcessor::set_bypass(const bool bypass) noexcept {
  bypass_.store(bypass, std::memory_order_relaxed);
}

void MidSideEqualizerProcessor::set_balance(const float percent) noexcept {
  if (std::isfinite(percent)) balance_.store(std::clamp(percent, -100.0F, 100.0F), std::memory_order_relaxed);
}

void MidSideEqualizerProcessor::set_input_gain_db(const float gain_db) noexcept {
  if (std::isfinite(gain_db)) input_gain_db_.store(std::clamp(gain_db, -max_gain_db, max_gain_db), std::memory_order_relaxed);
}

void MidSideEqualizerProcessor::set_output_gain_db(const float gain_db) noexcept {
  if (std::isfinite(gain_db)) output_gain_db_.store(std::clamp(gain_db, -max_gain_db, max_gain_db), std::memory_order_relaxed);
}

void MidSideEqualizerProcessor::clear_audio_state() noexcept {
  mid_eq_.reset();
  side_eq_.reset();
}

void MidSideEqualizerProcessor::process(float* const samples,
                                        const std::size_t frames,
                                        const float sample_rate) noexcept {
  if (samples == nullptr || !std::isfinite(sample_rate) || sample_rate <= 0.0F) return;
  if (bypass_.load(std::memory_order_relaxed)) return;

  const float input_gain = linear_gain(input_gain_db_.load(std::memory_order_relaxed));
  const float output_gain = linear_gain(output_gain_db_.load(std::memory_order_relaxed));
  const float balance = balance_.load(std::memory_order_relaxed) / 100.0F;
  const float left_gain = balance > 0.0F ? 1.0F - balance : 1.0F;
  const float right_gain = balance < 0.0F ? 1.0F + balance : 1.0F;
  std::array<float, block_frames> mid;
  std::array<float, block_frames> side;

  for (std::size_t offset = 0; offset < frames; offset += block_frames) {
    const std::size_t count = std::min(block_frames, frames - offset);
    for (std::size_t frame = 0; frame < count; ++frame) {
      const float left = samples[(offset + frame) * 2U] * input_gain;
      const float right = samples[(offset + frame) * 2U + 1U] * input_gain;
      mid[frame] = 0.5F * (left + right);
      side[frame] = 0.5F * (left - right);
    }

    mid_eq_.process(mid.data(), count, 1, sample_rate);
    side_eq_.process(side.data(), count, 1, sample_rate);

    for (std::size_t frame = 0; frame < count; ++frame) {
      samples[(offset + frame) * 2U] = (mid[frame] + side[frame]) * output_gain * left_gain;
      samples[(offset + frame) * 2U + 1U] = (mid[frame] - side[frame]) * output_gain * right_gain;
    }
  }
}

auto MidSideEqualizerProcessor::mid_response_db(const float frequency_hz,
                                                 const float sample_rate) const noexcept -> float {
  return mid_eq_.response_db(frequency_hz, sample_rate);
}

auto MidSideEqualizerProcessor::side_response_db(const float frequency_hz,
                                                  const float sample_rate) const noexcept -> float {
  return side_eq_.response_db(frequency_hz, sample_rate);
}

}  // namespace ee::mobile
