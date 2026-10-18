/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_global_equalizer.hpp"
#include "android_platform_effects_backend.hpp"
#include "parametric_equalizer.hpp"

#include <android/log.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

namespace {
auto engine_from(const jlong handle) noexcept -> ee::mobile::ParametricEqualizer* {
  return reinterpret_cast<ee::mobile::ParametricEqualizer*>(static_cast<std::uintptr_t>(handle));
}

void require(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeRunAndroidBackendChecks(JNIEnv* env,
                                                                              jobject,
                                                                              const jlong handle) {
  auto* const equalizer = engine_from(handle);
  if (equalizer == nullptr) {
    return env->NewStringUTF("FAIL: native C++ equalizer is not initialized");
  }
  using namespace ee::mobile::android_global_equalizer;
  const bool was_enabled = is_enabled(env, equalizer);
  std::string result;
  try {
    const int band_count = enable(env, *equalizer, true);
    require(band_count > 0, "Android exposed no global EQ bands");
    require(active_band_count(equalizer) == band_count, "C++/Android band counts disagree");
    require(is_enabled(env, equalizer), "Android did not retain the enabled state");
    for (const int band : {0, band_count / 2, band_count - 1}) {
      const float gain = configured_band_gain_db(env, equalizer, 0, band);
      require(std::isfinite(gain), "Android returned a non-finite band gain");
    }
    update(env, *equalizer, true);
    require(is_enabled(env, equalizer), "Android lost effect ownership during update");
    result = "PASS: C++ JNI backend enabled; " + std::to_string(band_count) +
             " bands read back and update retained ownership";
    constexpr std::array<int, 3U> parameter_values{500, 500, 600};
    constexpr std::array<const char*, 3U> effect_names{"BassBoost", "Virtualizer", "LoudnessEnhancer"};
    std::string platform_results;
    bool every_platform_effect_worked = true;
    for (std::size_t effect = 0; effect < parameter_values.size(); ++effect) {
      try {
        ee::mobile::android_platform_effects_backend::configure(
            env, equalizer, static_cast<int>(effect), true, parameter_values[effect]);
        ee::mobile::android_platform_effects_backend::configure(
            env, equalizer, static_cast<int>(effect), false, parameter_values[effect]);
        platform_results += std::string("; ") + effect_names[effect] + " available and read back its active state";
      } catch (const std::exception& failure) {
        every_platform_effect_worked = false;
        try {
          ee::mobile::android_platform_effects_backend::configure(
              env, equalizer, static_cast<int>(effect), false, parameter_values[effect]);
        } catch (...) {
        }
        platform_results += std::string("; ") + effect_names[effect] + " unavailable: " + failure.what();
      }
    }
    result += platform_results;
    if (!every_platform_effect_worked) {
      result = "PARTIAL: " + result;
    }
    constexpr std::array<float, 4U> stage_thresholds{-18.0F, -60.0F, -1.0F, -60.0F};
    constexpr std::array<float, 4U> stage_ratios{3.0F, 1.0F, 10.0F, 2.0F};
    constexpr std::array<const char*, 4U> stage_names{"Compressor MBC", "Noise gate MBC", "Limiter", "Expander MBC"};
    bool every_dynamics_stage_worked = true;
    for (std::size_t stage = 0; stage < stage_thresholds.size(); ++stage) {
      try {
        configure_dynamics_stage(env, equalizer, static_cast<int>(stage), true,
                                 stage_thresholds[stage], stage_ratios[stage]);
        configure_dynamics_stage(env, equalizer, static_cast<int>(stage), false,
                                 stage_thresholds[stage], stage_ratios[stage]);
        result += std::string("; ") + stage_names[stage] + " configured and disabled";
      } catch (const std::exception& failure) {
        every_dynamics_stage_worked = false;
        try {
          configure_dynamics_stage(env, equalizer, static_cast<int>(stage), false,
                                   stage_thresholds[stage], stage_ratios[stage]);
        } catch (...) {
        }
        result += std::string("; ") + stage_names[stage] + " unavailable: " + failure.what();
      }
    }
    if (every_dynamics_stage_worked) {
      const int band_count_without_eq = enable(env, *equalizer, false);
      require(band_count_without_eq > 0, "DynamicsProcessing backend disappeared with EQ disabled");
      configure_dynamics_stage(env, equalizer, 0, true, -18.0F, 3.0F);
      require(is_effect_active(env, equalizer),
              "Compressor must activate Android DynamicsProcessing with the global EQ off");
      require(!is_enabled(env, equalizer), "Dynamics-only operation must not mark the EQ plugin enabled");
      configure_dynamics_stage(env, equalizer, 0, false, -18.0F, 3.0F);
      require(!is_effect_active(env, equalizer),
              "DynamicsProcessing must stop when its last stage and the EQ are disabled");
      result += "; Compressor runs independently with global EQ disabled";
    }
    if (!every_dynamics_stage_worked) {
      result = "PARTIAL: " + result;
    }
  } catch (const std::exception& failure) {
    result = std::string("FAIL: ") + failure.what();
  }
  if (was_enabled) {
    try {
      update(env, *equalizer, true);
    } catch (...) {
      __android_log_print(ANDROID_LOG_ERROR, "EasyEffectsNativeTests", "Could not restore enabled EQ after test");
    }
  } else {
    disable(env, equalizer);
  }
  __android_log_print(ANDROID_LOG_INFO, "EasyEffectsNativeTests", "%s", result.c_str());
  return env->NewStringUTF(result.c_str());
}
