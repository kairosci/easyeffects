/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ee::mobile {

[[nodiscard]] auto graph_frequency_from_x(float normalized_x) noexcept -> float;
[[nodiscard]] auto graph_gain_for_response(float current_band_gain_db,
                                           float current_total_response_db,
                                           float target_total_response_db) noexcept -> float;

enum class FilterType : int {
  peaking = 0,
  low_shelf = 1,
  high_shelf = 2,
  low_pass = 3,
  high_pass = 4,
  band_pass = 5,
  notch = 6,
  all_pass = 7,
  off = 8,
};

struct BandSettings {
  FilterType type = FilterType::peaking;
  float frequency_hz = 1000.0F;
  float q = 1.0F;
  float gain_db = 0.0F;
  bool enabled = true;
  int channel_mode = 0;  // 0: stereo, 1: left, 2: right
};

class ParametricEqualizer final {
 public:
  static constexpr std::size_t band_count = 32U;
  static constexpr std::size_t max_channels = 8U;

  ParametricEqualizer() noexcept;
  void set_band(std::size_t index, BandSettings settings) noexcept;
  void set_bypass(bool bypass) noexcept;
  void set_preamp(float gain_db) noexcept;
  void reset() noexcept;
  void process(float* interleaved, std::size_t frames, int channels, float sample_rate) noexcept;
  void process(std::int16_t* interleaved, std::size_t frames, int channels, float sample_rate) noexcept;
  [[nodiscard]] auto response_db(float frequency_hz,
                                 float sample_rate,
                                 int channel = 0,
                                 bool honor_bypass = true) const noexcept -> float;
  void response_curve(float* values, std::size_t count, float sample_rate, int channel) const noexcept;

 private:
  struct AtomicBand {
    std::atomic<int> type{static_cast<int>(FilterType::peaking)};
    std::atomic<float> frequency_hz{1000.0F};
    std::atomic<float> q{1.0F};
    std::atomic<float> gain_db{0.0F};
    std::atomic<bool> enabled{true};
    std::atomic<int> channel_mode{0};
    std::atomic<unsigned int> revision{1U};
  };

  struct Coefficients {
    float b0 = 1.0F;
    float b1 = 0.0F;
    float b2 = 0.0F;
    float a1 = 0.0F;
    float a2 = 0.0F;
  };

  struct State {
    float z1 = 0.0F;
    float z2 = 0.0F;
  };

  static auto design(const BandSettings& band, float sample_rate) noexcept -> Coefficients;
  static auto snapshot(const AtomicBand& band) noexcept -> BandSettings;
  static auto run(float input, const Coefficients& coefficients, State& state) noexcept -> float;
  void refresh_coefficients(float sample_rate) noexcept;

  std::array<AtomicBand, band_count> bands_{};
  std::array<BandSettings, band_count> applied_settings_{};
  std::array<Coefficients, band_count> coefficients_{};
  std::array<std::array<State, band_count>, max_channels> states_{};
  std::array<unsigned int, band_count> applied_revision_{};
  std::atomic<bool> bypass_{false};
  std::atomic<float> preamp_db_{0.0F};
  float applied_sample_rate_ = 0.0F;
  int applied_channels_ = 0;
};

}  // namespace ee::mobile
