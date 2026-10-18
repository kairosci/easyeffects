/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "parametric_equalizer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ee::mobile {
namespace {
constexpr float pi = 3.14159265358979323846F;

}  // namespace

auto graph_frequency_from_x(const float normalized_x) noexcept -> float {
  return 20.0F * std::pow(1000.0F, std::clamp(normalized_x, 0.0F, 1.0F));
}

auto graph_gain_for_response(const float current_band_gain_db,
                             const float current_total_response_db,
                             const float target_total_response_db) noexcept -> float {
  return std::clamp(current_band_gain_db + target_total_response_db - current_total_response_db, -24.0F, 24.0F);
}

ParametricEqualizer::ParametricEqualizer() noexcept {
  for (std::size_t i = 0; i < band_count; ++i) {
    const float ratio = static_cast<float>(i) / static_cast<float>(band_count - 1U);
    const float frequency = 20.0F * std::pow(1000.0F, ratio);
    set_band(i, {.type = FilterType::peaking, .frequency_hz = frequency, .q = 1.4F});
  }
}

auto ParametricEqualizer::snapshot(const AtomicBand& band) noexcept -> BandSettings {
  return {static_cast<FilterType>(band.type.load(std::memory_order_relaxed)),
          band.frequency_hz.load(std::memory_order_relaxed),
          band.q.load(std::memory_order_relaxed),
          band.gain_db.load(std::memory_order_relaxed),
          band.enabled.load(std::memory_order_relaxed),
          band.channel_mode.load(std::memory_order_relaxed)};
}

void ParametricEqualizer::set_band(const std::size_t index, const BandSettings settings) noexcept {
  if (index >= band_count || !std::isfinite(settings.frequency_hz) || !std::isfinite(settings.q) ||
      !std::isfinite(settings.gain_db)) {
    return;
  }

  auto& band = bands_[index];
  band.type.store(static_cast<int>(settings.type), std::memory_order_relaxed);
  band.frequency_hz.store(std::clamp(settings.frequency_hz, 10.0F, 40000.0F), std::memory_order_relaxed);
  band.q.store(std::clamp(settings.q, 0.05F, 30.0F), std::memory_order_relaxed);
  band.gain_db.store(std::clamp(settings.gain_db, -24.0F, 24.0F), std::memory_order_relaxed);
  band.enabled.store(settings.enabled, std::memory_order_relaxed);
  band.channel_mode.store(std::clamp(settings.channel_mode, 0, 2), std::memory_order_relaxed);
  band.revision.fetch_add(1U, std::memory_order_release);
}

void ParametricEqualizer::set_bypass(const bool bypass) noexcept {
  bypass_.store(bypass, std::memory_order_relaxed);
}

void ParametricEqualizer::set_preamp(const float gain_db) noexcept {
  if (std::isfinite(gain_db)) {
    preamp_db_.store(std::clamp(gain_db, -24.0F, 24.0F), std::memory_order_relaxed);
  }
}

void ParametricEqualizer::reset() noexcept {
  states_ = {};
  applied_channels_ = 0;
}

auto ParametricEqualizer::design(const BandSettings& band, const float sample_rate) noexcept -> Coefficients {
  const float rate = std::max(sample_rate, 1.0F);
  const float frequency = std::clamp(band.frequency_hz, 1.0F, rate * 0.49F);
  const float q = std::clamp(band.q, 0.05F, 30.0F);
  const float omega = 2.0F * pi * frequency / rate;
  const float cosine = std::cos(omega);
  const float sine = std::sin(omega);
  const float alpha = sine / (2.0F * q);
  const float amplitude = std::pow(10.0F, band.gain_db / 40.0F);
  float b0 = 1.0F;
  float b1 = 0.0F;
  float b2 = 0.0F;
  float a0 = 1.0F;
  float a1 = 0.0F;
  float a2 = 0.0F;

  switch (band.type) {
    case FilterType::peaking:
      b0 = 1.0F + alpha * amplitude;
      b1 = -2.0F * cosine;
      b2 = 1.0F - alpha * amplitude;
      a0 = 1.0F + alpha / amplitude;
      a1 = -2.0F * cosine;
      a2 = 1.0F - alpha / amplitude;
      break;
    case FilterType::low_pass:
      b0 = (1.0F - cosine) * 0.5F;
      b1 = 1.0F - cosine;
      b2 = b0;
      a0 = 1.0F + alpha;
      a1 = -2.0F * cosine;
      a2 = 1.0F - alpha;
      break;
    case FilterType::high_pass:
      b0 = (1.0F + cosine) * 0.5F;
      b1 = -(1.0F + cosine);
      b2 = b0;
      a0 = 1.0F + alpha;
      a1 = -2.0F * cosine;
      a2 = 1.0F - alpha;
      break;
    case FilterType::band_pass:
      b0 = alpha;
      b1 = 0.0F;
      b2 = -alpha;
      a0 = 1.0F + alpha;
      a1 = -2.0F * cosine;
      a2 = 1.0F - alpha;
      break;
    case FilterType::notch:
      b0 = 1.0F;
      b1 = -2.0F * cosine;
      b2 = 1.0F;
      a0 = 1.0F + alpha;
      a1 = -2.0F * cosine;
      a2 = 1.0F - alpha;
      break;
    case FilterType::all_pass:
      b0 = 1.0F - alpha;
      b1 = -2.0F * cosine;
      b2 = 1.0F + alpha;
      a0 = 1.0F + alpha;
      a1 = -2.0F * cosine;
      a2 = 1.0F - alpha;
      break;
    case FilterType::low_shelf:
    case FilterType::high_shelf: {
      const float shelf_alpha = sine * 0.5F * std::sqrt(2.0F);
      const float root = 2.0F * std::sqrt(amplitude) * shelf_alpha;
      if (band.type == FilterType::low_shelf) {
        b0 = amplitude * ((amplitude + 1.0F) - (amplitude - 1.0F) * cosine + root);
        b1 = 2.0F * amplitude * ((amplitude - 1.0F) - (amplitude + 1.0F) * cosine);
        b2 = amplitude * ((amplitude + 1.0F) - (amplitude - 1.0F) * cosine - root);
        a0 = (amplitude + 1.0F) + (amplitude - 1.0F) * cosine + root;
        a1 = -2.0F * ((amplitude - 1.0F) + (amplitude + 1.0F) * cosine);
        a2 = (amplitude + 1.0F) + (amplitude - 1.0F) * cosine - root;
      } else {
        b0 = amplitude * ((amplitude + 1.0F) + (amplitude - 1.0F) * cosine + root);
        b1 = -2.0F * amplitude * ((amplitude - 1.0F) + (amplitude + 1.0F) * cosine);
        b2 = amplitude * ((amplitude + 1.0F) + (amplitude - 1.0F) * cosine - root);
        a0 = (amplitude + 1.0F) - (amplitude - 1.0F) * cosine + root;
        a1 = 2.0F * ((amplitude - 1.0F) - (amplitude + 1.0F) * cosine);
        a2 = (amplitude + 1.0F) - (amplitude - 1.0F) * cosine - root;
      }
      break;
    }
    case FilterType::off:
      return {};
  }

  if (!band.enabled || std::abs(a0) < 1.0e-12F) {
    return {};
  }
  const float inverse_a0 = 1.0F / a0;
  return {b0 * inverse_a0, b1 * inverse_a0, b2 * inverse_a0, a1 * inverse_a0, a2 * inverse_a0};
}

auto ParametricEqualizer::run(const float input, const Coefficients& c, State& state) noexcept -> float {
  const float output = c.b0 * input + state.z1;
  state.z1 = c.b1 * input - c.a1 * output + state.z2;
  state.z2 = c.b2 * input - c.a2 * output;
  return std::isfinite(output) ? output : 0.0F;
}

void ParametricEqualizer::refresh_coefficients(const float sample_rate) noexcept {
  const bool rate_changed = sample_rate != applied_sample_rate_;
  for (std::size_t i = 0; i < band_count; ++i) {
    const unsigned int revision = bands_[i].revision.load(std::memory_order_acquire);
    if (rate_changed || revision != applied_revision_[i]) {
      applied_settings_[i] = snapshot(bands_[i]);
      coefficients_[i] = design(applied_settings_[i], sample_rate);
      applied_revision_[i] = revision;
    }
  }
  applied_sample_rate_ = sample_rate;
}

void ParametricEqualizer::process(float* const samples,
                                  const std::size_t frames,
                                  const int channels,
                                  const float sample_rate) noexcept {
  if (samples == nullptr || channels < 1 || channels > static_cast<int>(max_channels) || !std::isfinite(sample_rate) ||
      sample_rate <= 0.0F || bypass_.load(std::memory_order_relaxed)) {
    return;
  }
  refresh_coefficients(sample_rate);
  const float preamp = std::pow(10.0F, preamp_db_.load(std::memory_order_relaxed) / 20.0F);
  if (channels != applied_channels_) {
    states_ = {};
    applied_channels_ = channels;
  }

  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (int channel = 0; channel < channels; ++channel) {
      float sample = samples[frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)];
      for (std::size_t band = 0; band < band_count; ++band) {
        const int mode = applied_settings_[band].channel_mode;
        if (mode == 0 || (mode == 1 && channel == 0) || (mode == 2 && channel == 1)) {
          sample = run(sample, coefficients_[band], states_[static_cast<std::size_t>(channel)][band]);
        }
      }
      samples[frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)] = sample * preamp;
    }
  }
}

void ParametricEqualizer::process(std::int16_t* const samples,
                                  const std::size_t frames,
                                  const int channels,
                                  const float sample_rate) noexcept {
  if (samples == nullptr || channels < 1 || channels > static_cast<int>(max_channels) || !std::isfinite(sample_rate) ||
      sample_rate <= 0.0F || bypass_.load(std::memory_order_relaxed)) {
    return;
  }
  refresh_coefficients(sample_rate);
  const float preamp = std::pow(10.0F, preamp_db_.load(std::memory_order_relaxed) / 20.0F);
  if (channels != applied_channels_) {
    states_ = {};
    applied_channels_ = channels;
  }
  constexpr float scale = 1.0F / 32768.0F;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (int channel = 0; channel < channels; ++channel) {
      const auto offset = frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
      float sample = static_cast<float>(samples[offset]) * scale;
      for (std::size_t band = 0; band < band_count; ++band) {
        const int mode = applied_settings_[band].channel_mode;
        if (mode == 0 || (mode == 1 && channel == 0) || (mode == 2 && channel == 1)) {
          sample = run(sample, coefficients_[band], states_[static_cast<std::size_t>(channel)][band]);
        }
      }
      samples[offset] = static_cast<std::int16_t>(std::lrint(std::clamp(sample * preamp, -1.0F, 0.999969F) * 32768.0F));
    }
  }
}

auto ParametricEqualizer::response_db(const float frequency_hz,
                                      const float sample_rate,
                                      const int channel,
                                      const bool honor_bypass) const noexcept -> float {
  if (!std::isfinite(frequency_hz) || !std::isfinite(sample_rate) || frequency_hz <= 0.0F || sample_rate <= 0.0F) {
    return 0.0F;
  }
  if (channel < 0 || channel > 1) {
    return 0.0F;
  }
  if (honor_bypass && bypass_.load(std::memory_order_relaxed)) {
    return 0.0F;
  }
  const std::complex<float> z = std::polar(1.0F, -2.0F * pi * frequency_hz / sample_rate);
  std::complex<float> response{1.0F, 0.0F};
  for (const auto& atom : bands_) {
    const BandSettings band = snapshot(atom);
    if (!band.enabled || band.type == FilterType::off ||
        (band.channel_mode != 0 && band.channel_mode != channel + 1)) {
      continue;
    }
    const Coefficients c = design(band, sample_rate);
    const auto z2 = z * z;
    const auto numerator = c.b0 + c.b1 * z + c.b2 * z2;
    const auto denominator = 1.0F + c.a1 * z + c.a2 * z2;
    if (std::abs(denominator) > 1.0e-12F) {
      response *= numerator / denominator;
    }
  }
  const float preamp = std::pow(10.0F, preamp_db_.load(std::memory_order_relaxed) / 20.0F);
  return 20.0F * std::log10(std::max(std::abs(response) * preamp, 1.0e-12F));
}

void ParametricEqualizer::response_curve(float* const values,
                                         const std::size_t count,
                                         const float sample_rate,
                                         const int channel) const noexcept {
  if (values == nullptr || count == 0U || !std::isfinite(sample_rate) || sample_rate <= 0.0F || channel < 0 ||
      channel > 1) {
    return;
  }
  if (bypass_.load(std::memory_order_relaxed)) {
    std::fill_n(values, count, 0.0F);
    return;
  }
  std::array<BandSettings, band_count> settings{};
  std::array<Coefficients, band_count> coefficients{};
  for (std::size_t band = 0; band < band_count; ++band) {
    settings[band] = snapshot(bands_[band]);
    coefficients[band] = design(settings[band], sample_rate);
  }

  const float preamp = std::pow(10.0F, preamp_db_.load(std::memory_order_relaxed) / 20.0F);
  for (std::size_t point = 0; point < count; ++point) {
    const float ratio = count == 1U ? 0.0F : static_cast<float>(point) / static_cast<float>(count - 1U);
    const float frequency = 20.0F * std::pow(1000.0F, ratio);
    const std::complex<float> z = std::polar(1.0F, -2.0F * pi * frequency / sample_rate);
    const auto z2 = z * z;
    std::complex<float> response{1.0F, 0.0F};
    for (std::size_t band = 0; band < band_count; ++band) {
      const int channel_mode = settings[band].channel_mode;
      if (!settings[band].enabled || settings[band].type == FilterType::off ||
          (channel_mode != 0 && channel_mode != channel + 1)) {
        continue;
      }
      const auto& c = coefficients[band];
      const auto numerator = c.b0 + c.b1 * z + c.b2 * z2;
      const auto denominator = 1.0F + c.a1 * z + c.a2 * z2;
      if (std::abs(denominator) > 1.0e-12F) {
        response *= numerator / denominator;
      }
    }
    values[point] = 20.0F * std::log10(std::max(std::abs(response) * preamp, 1.0e-12F));
  }
}

}  // namespace ee::mobile
