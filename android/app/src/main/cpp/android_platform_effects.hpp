/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>

namespace ee::mobile {

enum class PlatformEffectId : int { bass_boost = 0, virtualizer = 1, loudness_enhancer = 2 };

struct PlatformEffectParameterRange {
  int minimum;
  int maximum;
};

inline constexpr std::array<PlatformEffectParameterRange, 3U> platform_effect_parameter_ranges{
    PlatformEffectParameterRange{0, 1000},      // BassBoost strength
    PlatformEffectParameterRange{0, 1000},      // Virtualizer strength
    PlatformEffectParameterRange{0, 2400},      // LoudnessEnhancer target gain, millibels
};

[[nodiscard]] constexpr auto valid_platform_effect_parameter(const int effect, const int value) noexcept -> bool {
  if (effect < 0 || effect >= static_cast<int>(platform_effect_parameter_ranges.size())) {
    return false;
  }
  const auto& range = platform_effect_parameter_ranges[static_cast<unsigned int>(effect)];
  return value >= range.minimum && value <= range.maximum;
}

}  // namespace ee::mobile
