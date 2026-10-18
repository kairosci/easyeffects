/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * Adapted from LSP Compensation Delay semantics (LGPL-3.0-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "delay_processor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ee::mobile {
namespace {
constexpr float min_gain_db = -36.0F;
constexpr float max_gain_db = 36.0F;
constexpr float min_plugin_gain_db = -80.01F;
constexpr float max_plugin_gain_db = 20.0F;
constexpr float max_time_ms = 1000.0F;
constexpr float max_samples = 10000.0F;
constexpr float max_meters = 200.0F;
constexpr float max_centimeters = 100.0F;
constexpr float min_temperature_c = -60.0F;
constexpr float max_temperature_c = 60.0F;

auto clamp_finite(const float value, const float fallback, const float minimum, const float maximum) noexcept -> float {
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

// LSP dsp-units sound_speed(): sqrt(gamma * R * (T - absolute_zero) * 1000 / molar_mass).
auto sound_speed(const float temperature_c) noexcept -> float {
  constexpr float adiabatic_index = 1.4F;
  constexpr float gas_constant = 8.3144598F;
  constexpr float absolute_zero_c = -273.15F;
  constexpr float molar_mass_g_per_mol = 28.98F;
  return std::sqrt(adiabatic_index * gas_constant * (temperature_c - absolute_zero_c) * 1000.0F /
                   molar_mass_g_per_mol);
}
}  // namespace

DelayProcessor::DelayProcessor() {
  set_parameters(DelayParameters{});
}

auto DelayProcessor::clamp_channel(DelayChannelParameters p) noexcept -> DelayChannelParameters {
  if (p.mode != DelayMode::samples && p.mode != DelayMode::distance && p.mode != DelayMode::time) {
    p.mode = DelayMode::time;
  }
  p.time_ms = clamp_finite(p.time_ms, 0.0F, 0.0F, max_time_ms);
  p.samples = clamp_finite(p.samples, 0.0F, 0.0F, max_samples);
  p.meters = clamp_finite(p.meters, 0.0F, 0.0F, max_meters);
  p.centimeters = clamp_finite(p.centimeters, 0.0F, 0.0F, max_centimeters);
  p.temperature_c = clamp_finite(p.temperature_c, 20.0F, min_temperature_c, max_temperature_c);
  p.dry_db = clamp_finite(p.dry_db, min_plugin_gain_db, min_plugin_gain_db, max_plugin_gain_db);
  p.wet_db = clamp_finite(p.wet_db, 0.0F, min_plugin_gain_db, max_plugin_gain_db);
  return p;
}

auto DelayProcessor::delay_samples(const DelayChannelParameters& p, const float sample_rate) noexcept -> std::uint32_t {
  float samples = 0.0F;
  switch (p.mode) {
    case DelayMode::samples:
      samples = p.samples;
      break;
    case DelayMode::distance:
      samples = sample_rate * (p.meters + p.centimeters * 0.01F) / sound_speed(p.temperature_c);
      break;
    case DelayMode::time:
      samples = sample_rate * (p.time_ms * 0.001F);
      break;
  }
  if (!std::isfinite(samples) || samples <= 0.0F) return 0U;
  return static_cast<std::uint32_t>(std::min(samples, sample_rate));
}

auto DelayProcessor::gain_from_db(const float db, const bool silence_at_minimum) noexcept -> float {
  if (!std::isfinite(db)) return silence_at_minimum ? 0.0F : 1.0F;
  if (silence_at_minimum && db <= min_plugin_gain_db) return 0.0F;
  const float bounded = std::clamp(db, silence_at_minimum ? min_plugin_gain_db : min_gain_db,
                                  silence_at_minimum ? max_plugin_gain_db : max_gain_db);
  return std::pow(10.0F, bounded / 20.0F);
}

auto DelayProcessor::prepare(const float sample_rate) -> bool {
  if (!std::isfinite(sample_rate) || sample_rate < 8000.0F || sample_rate > 768000.0F) return false;
  const auto history_size = static_cast<std::size_t>(std::ceil(sample_rate * 1.0F)) + 1U;
  try {
    for (auto& buffer : buffers_) buffer.assign(history_size, 0.0F);
  } catch (...) {
    for (auto& buffer : buffers_) buffer.clear();
    sample_rate_ = 0.0F;
    return false;
  }
  write_indices_.fill(0U);
  sample_rate_ = sample_rate;
  return true;
}

void DelayProcessor::set_parameters(const DelayParameters& parameters) noexcept {
  for (std::size_t channel = 0; channel < channel_count; ++channel) {
    const auto p = clamp_channel(parameters.channels[channel]);
    mode_[channel].store(p.mode, std::memory_order_relaxed);
    time_ms_[channel].store(p.time_ms, std::memory_order_relaxed);
    samples_[channel].store(p.samples, std::memory_order_relaxed);
    meters_[channel].store(p.meters, std::memory_order_relaxed);
    centimeters_[channel].store(p.centimeters, std::memory_order_relaxed);
    temperature_c_[channel].store(p.temperature_c, std::memory_order_relaxed);
    dry_db_[channel].store(p.dry_db, std::memory_order_relaxed);
    wet_db_[channel].store(p.wet_db, std::memory_order_relaxed);
    invert_phase_[channel].store(p.invert_phase, std::memory_order_relaxed);
  }
  input_gain_db_.store(clamp_finite(parameters.input_gain_db, 0.0F, min_gain_db, max_gain_db),
                       std::memory_order_relaxed);
  output_gain_db_.store(clamp_finite(parameters.output_gain_db, 0.0F, min_gain_db, max_gain_db),
                        std::memory_order_relaxed);
  bypass_.store(parameters.bypass, std::memory_order_relaxed);
}

auto DelayProcessor::parameters() const noexcept -> DelayParameters {
  DelayParameters p;
  for (std::size_t channel = 0; channel < channel_count; ++channel) {
    p.channels[channel] = {
        .mode = mode_[channel].load(std::memory_order_relaxed),
        .time_ms = time_ms_[channel].load(std::memory_order_relaxed),
        .samples = samples_[channel].load(std::memory_order_relaxed),
        .meters = meters_[channel].load(std::memory_order_relaxed),
        .centimeters = centimeters_[channel].load(std::memory_order_relaxed),
        .temperature_c = temperature_c_[channel].load(std::memory_order_relaxed),
        .dry_db = dry_db_[channel].load(std::memory_order_relaxed),
        .wet_db = wet_db_[channel].load(std::memory_order_relaxed),
        .invert_phase = invert_phase_[channel].load(std::memory_order_relaxed),
    };
  }
  p.input_gain_db = input_gain_db_.load(std::memory_order_relaxed);
  p.output_gain_db = output_gain_db_.load(std::memory_order_relaxed);
  p.bypass = bypass_.load(std::memory_order_relaxed);
  return p;
}

auto DelayProcessor::levels() const noexcept -> DelayLevels {
  return {
      .input_left_db = levels_db_[0].load(std::memory_order_relaxed),
      .input_right_db = levels_db_[1].load(std::memory_order_relaxed),
      .output_left_db = levels_db_[2].load(std::memory_order_relaxed),
      .output_right_db = levels_db_[3].load(std::memory_order_relaxed),
  };
}

auto DelayProcessor::effective_delay_samples(const std::size_t channel) const noexcept -> std::uint32_t {
  if (channel >= channel_count || sample_rate_ <= 0.0F) return 0U;
  return delay_samples(parameters().channels[channel], sample_rate_);
}

void DelayProcessor::reset() noexcept {
  set_parameters(DelayParameters{});
  reset_requested_.store(true, std::memory_order_relaxed);
}

void DelayProcessor::process(float* const interleaved_stereo, const std::size_t frames) noexcept {
  if (interleaved_stereo == nullptr || frames == 0U || sample_rate_ <= 0.0F || buffers_[0].empty() || buffers_[1].empty()) {
    return;
  }

  const auto p = parameters();
  if (reset_requested_.exchange(false, std::memory_order_relaxed)) {
    for (auto& buffer : buffers_) std::fill(buffer.begin(), buffer.end(), 0.0F);
    write_indices_.fill(0U);
    for (auto& level : levels_db_) level.store(-100.0F, std::memory_order_relaxed);
  }
  const float input_gain = gain_from_db(p.input_gain_db, false);
  const float output_gain = gain_from_db(p.output_gain_db, false);
  const std::size_t ring_size = buffers_[0].size();
  const std::array<std::uint32_t, channel_count> delays{
      delay_samples(p.channels[0], sample_rate_), delay_samples(p.channels[1], sample_rate_)};
  const std::array<float, channel_count> dry_gains{
      gain_from_db(p.channels[0].dry_db, true), gain_from_db(p.channels[1].dry_db, true)};
  const std::array<float, channel_count> wet_gains{
      gain_from_db(p.channels[0].wet_db, true), gain_from_db(p.channels[1].wet_db, true)};
  const std::array<float, channel_count> phase{
      p.channels[0].invert_phase ? -1.0F : 1.0F, p.channels[1].invert_phase ? -1.0F : 1.0F};
  std::array<float, 4> peak_values{};

  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < channel_count; ++channel) {
      const std::size_t index = frame * channel_count + channel;
      const float input = interleaved_stereo[index];
      const float processed_input = input * input_gain;
      peak_values[channel] = std::max(peak_values[channel], std::abs(processed_input));
      auto& ring = buffers_[channel];
      auto& write = write_indices_[channel];
      ring[write] = processed_input;
      const std::size_t delay = delays[channel];
      const std::size_t read = (write + ring_size - delay) % ring_size;
      const float delayed = ring[read];
      write = (write + 1U) % ring_size;
      const float processed =
          (processed_input * dry_gains[channel] + delayed * wet_gains[channel]) * phase[channel] * output_gain;
      interleaved_stereo[index] = p.bypass ? input : processed;
      peak_values[channel + 2U] = std::max(peak_values[channel + 2U], std::abs(interleaved_stereo[index]));
    }
  }
  for (std::size_t meter = 0; meter < peak_values.size(); ++meter) {
    const float value = peak_values[meter] >= 0.00001F ? 20.0F * std::log10(peak_values[meter]) : -100.0F;
    levels_db_[meter].store(value, std::memory_order_relaxed);
  }
}

}  // namespace ee::mobile
